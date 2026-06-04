// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKBridge.h"
#include "NwiroIKMCPServer.h"
#include "NwiroIKPanel.h"

// Engine — Core
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Json.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Engine — Editor / Slate
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "SWebBrowser.h"

// Engine — HTTP
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Engine — Assets
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"

// Engine — Scripting
#include "IPythonScriptPlugin.h"

// Nwiro — Zip extraction (miniz-based, no PowerShell dependency)
#include "ContentPipeline/NwiroIKZipExtractor.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#include <unistd.h>
#endif

/** Cross-platform "is this path a real file we could exec?". UE's
 *  FPaths::FileExists relies on stat() through a normaliser that has been
 *  observed to miss symlinks like `~/.local/bin/claude` on macOS (the
 *  claude installer always lays the CLI down as a symlink). access(F_OK)
 *  follows symlinks and matches what posix_spawn will actually try to do. */
static bool NwiroPathExists(const FString& Path)
{
#if PLATFORM_MAC || PLATFORM_LINUX
	const FTCHARToUTF8 Conv(*Path);
	return access(Conv.Get(), F_OK) == 0;
#else
	return FPaths::FileExists(Path);
#endif
}

// Minimal JSON string escape for embedding diagnostic text into the
// PushEvent JSON payloads. Filesystem paths on Windows carry backslashes,
// and miniz / FFileHelper error messages can contain quotes and newlines —
// without escaping, the frontend's JSON.parse would silently swallow the
// event (matching the very bug we're fixing). Not a general-purpose JSON
// encoder; covers the characters that actually appear in our error text.
static FString NwiroJsonEscape(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
	return Out;
}

// ─── Adapter timeout overrides (env-driven, no plugin rebuild required) ────
// Three stage timeouts (INITIALIZE / SESSION_NEW / FIRST_TOKEN) used by
// CheckAdapterTimeouts(). Each can be tuned via an OS env var WITHOUT
// rebuilding the plugin — set the variable before launching UE Editor:
//
//   setx NWIRO_FIRST_TOKEN_TIMEOUT_SECONDS 240   (Windows, persistent)
//   $env:NWIRO_FIRST_TOKEN_TIMEOUT_SECONDS = "240"   (Windows, current shell)
//   export NWIRO_FIRST_TOKEN_TIMEOUT_SECONDS=240    (POSIX)
//
// Values are cached on first read via static-local + lambda IIFE — thread-safe
// per C++17 §6.7/4 and free of synchronisation cost on the hot path. Trade-off:
// the env must be set before UE launches; changing it mid-session has no
// effect until the next editor restart. The cached value is logged once per
// session so deployments are auditable.
namespace
{
	static double ReadTimeoutEnvSeconds(const TCHAR* EnvName, const TCHAR* DisplayName, double DefaultSeconds)
	{
		const FString S = FPlatformMisc::GetEnvironmentVariable(EnvName);
		double V = S.IsEmpty() ? DefaultSeconds : FCString::Atod(*S);
		if (V <= 0.0) V = DefaultSeconds;
		UE_LOG(LogTemp, Log, TEXT("Nwiro IK: %s = %.1fs (env %s=%s)"),
			DisplayName, V, EnvName,
			S.IsEmpty() ? TEXT("<unset → default>") : *S);
		return V;
	}

	static double GetInitializeTimeoutSeconds()
	{
		static const double V = ReadTimeoutEnvSeconds(
			TEXT("NWIRO_INITIALIZE_TIMEOUT_SECONDS"), TEXT("INITIALIZE_TIMEOUT"), 60.0);
		return V;
	}
	static double GetSessionNewTimeoutSeconds()
	{
		static const double V = ReadTimeoutEnvSeconds(
			TEXT("NWIRO_SESSION_NEW_TIMEOUT_SECONDS"), TEXT("SESSION_NEW_TIMEOUT"), 30.0);
		return V;
	}
	static double GetFirstTokenTimeoutSeconds()
	{
		static const double V = ReadTimeoutEnvSeconds(
			TEXT("NWIRO_FIRST_TOKEN_TIMEOUT_SECONDS"), TEXT("FIRST_TOKEN_TIMEOUT"), 180.0);
		return V;
	}
}

UNwiroIKBridge* UNwiroIKBridge::Instance = nullptr;

// ============================================================
// Adapter registry — add new adapters here
// ============================================================

struct FAdapterInfo
{
	FString Id;
	FString BinaryName;
	FString DownloadUrl;
	FString EnvKey;       // Environment variable to set (e.g. CLAUDE_CODE_EXECUTABLE)
	TArray<FString> ExeCandidates; // Paths to search for the underlying CLI
};

/** Parse "2.1.128" into [2, 1, 128]. Returns empty TOptional if the string isn't
 *  exactly three dot-separated non-negative integers. Pre-release suffixes
 *  ("2.1.0-rc1") and v-prefixed strings ("v2.1.0") are intentionally rejected
 *  — the Anthropic installer uses pure semver paths, anything else is unknown. */
static TOptional<TArray<int32>> ParseSemverParts(const FString& VersionStr)
{
	TArray<FString> Parts;
	VersionStr.ParseIntoArray(Parts, TEXT("."), true);
	if (Parts.Num() != 3) return {};
	TArray<int32> Result;
	Result.Reserve(3);
	for (const FString& P : Parts)
	{
		if (P.IsEmpty()) return {};
		for (TCHAR C : P) if (!FChar::IsDigit(C)) return {};
		Result.Add(FCString::Atoi(*P));
	}
	return Result;
}

#if PLATFORM_WINDOWS
/** Enumerate Anthropic-installer Claude Code installs under
 *  %APPDATA%\Claude\claude-code\<semver>\claude.exe and return absolute paths
 *  sorted by semver descending (newest first). Empty array if the install root
 *  is missing or contains no valid versioned subdirs.
 *
 *  Rationale: stale `claude.exe` shims in %USERPROFILE%\.local\bin (typically
 *  left by older npm-style installers) silently shadow the Anthropic-installed
 *  binary. Older claude.exe + newer claude-agent-acp produces a silent
 *  TypeError ("H.effortLevel" / reasoning_effort) on session/prompt — the
 *  adapter never emits agent_message_chunk, surfacing as first_token_timeout
 *  after 180s. Searching the installer path FIRST avoids that trap. */
static TArray<FString> FindClaudeInstallerPaths()
{
	TArray<FString> Out;
	const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (AppData.IsEmpty()) return Out;

	const FString InstallRoot = FPaths::Combine(AppData, TEXT("Claude"), TEXT("claude-code"));
	if (!FPaths::DirectoryExists(InstallRoot)) return Out;

	TArray<FString> VersionDirs;
	IFileManager::Get().FindFiles(VersionDirs,
		*FPaths::Combine(InstallRoot, TEXT("*")), /*Files*/ false, /*Dirs*/ true);

	struct FCandidate { TArray<int32> Sem; FString DirName; };
	TArray<FCandidate> Parsed;
	for (const FString& Dir : VersionDirs)
	{
		TOptional<TArray<int32>> Sem = ParseSemverParts(Dir);
		if (Sem.IsSet()) Parsed.Add({MoveTemp(Sem.GetValue()), Dir});
	}
	// Descending: 2.1.138 wins over 2.1.5 (NOT alphabetical) and over 2.0.58.
	Parsed.Sort([](const FCandidate& A, const FCandidate& B) {
		if (A.Sem[0] != B.Sem[0]) return A.Sem[0] > B.Sem[0];
		if (A.Sem[1] != B.Sem[1]) return A.Sem[1] > B.Sem[1];
		return A.Sem[2] > B.Sem[2];
	});
	for (const FCandidate& C : Parsed)
	{
		Out.Add(FPaths::Combine(InstallRoot, C.DirName, TEXT("claude.exe")));
	}
	return Out;
}

/** Reverse of FindClaudeInstallerPaths: given an absolute .exe path, extract
 *  the semver from the parent directory IFF the path matches the installer
 *  layout (`...\Claude\claude-code\<semver>\claude.exe`). Empty for
 *  `.local\bin\claude.exe` and other non-installer locations. Used by the
 *  min-version warning emitted at adapter launch. */
static TOptional<TArray<int32>> ParseSemverFromInstallerPath(const FString& ExePath)
{
	const FString VersionDir   = FPaths::GetPath(ExePath);            // ...\<semver>
	const FString ClaudeCodeDir = FPaths::GetPath(VersionDir);        // ...\claude-code
	if (VersionDir.IsEmpty() || ClaudeCodeDir.IsEmpty()) return {};
	if (FPaths::GetCleanFilename(ClaudeCodeDir) != TEXT("claude-code")) return {};
	return ParseSemverParts(FPaths::GetCleanFilename(VersionDir));
}
#endif // PLATFORM_WINDOWS

/** adapter-reliability-w7: log a one-shot pre-flight summary of MCP servers
 *  configured in the user's Claude Code settings.
 *
 *  Why: when a user has e.g. "blender" or "unity" MCP servers configured but
 *  those servers aren't running, claude.exe may block ~180s on TCP timeout
 *  trying to enumerate them, producing the same `bytesThisTurn ≈ 1.9KB` +
 *  first_token_timeout signature as the version-mismatch bug. Logging the
 *  configured servers up front gives support a starting point for triage —
 *  "user has 3 MCP servers, did they all need to be running?"
 *
 *  We intentionally do NOT TCP-probe each server. Most MCP servers configured
 *  in Claude Code are stdio type (command/args), which can't be probed without
 *  spawning the command — too invasive for a diagnostic. HTTP/SSE servers
 *  could be probed but adding network ops to plugin startup is fragile (false
 *  alarms during transient blips). Configured-server enumeration is enough
 *  to triage; if probing becomes necessary, ship it as a separate iteration.
 *
 *  Tries known config paths in priority order. Silently skips if no config is
 *  found or parse fails — this is best-effort observability, not a gate. */
static void LogClaudeMcpServersConfig(const FString& AdapterId)
{
	if (AdapterId != TEXT("claude")) return;

	// Static-once: log on first claude adapter spawn per UE editor session.
	// Subsequent restarts (Restart Adapter button) don't re-log unless the
	// editor is fully restarted, preventing log spam on iterative testing.
	static bool bHasLogged = false;
	if (bHasLogged) return;

	TArray<FString> CandidatePaths;
#if PLATFORM_WINDOWS
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (!UserProfile.IsEmpty())
	{
		// Anthropic's claude-code stores MCP servers here on Windows.
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".claude.json")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".claude"), TEXT("settings.json")));
	}
	if (!AppData.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(AppData, TEXT("Claude"), TEXT("config.json")));
	}
#else
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!Home.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(Home, TEXT(".claude.json")));
		CandidatePaths.Add(FPaths::Combine(Home, TEXT(".claude"), TEXT("settings.json")));
	}
#endif

	FString ConfigContent;
	FString FoundPath;
	for (const FString& Path : CandidatePaths)
	{
		if (FFileHelper::LoadFileToString(ConfigContent, *Path))
		{
			FoundPath = Path;
			break;
		}
	}
	bHasLogged = true;  // mark done even if we skip — don't retry every spawn

	if (FoundPath.IsEmpty())
	{
		UE_LOG(LogTemp, Log,
			TEXT("Nwiro IK: Claude Code config not found in any known location — MCP pre-flight skipped (this is fine if user has no MCP servers configured)"));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Nwiro IK: Claude Code config at %s exists but isn't valid JSON — MCP pre-flight skipped"),
			*FoundPath);
		return;
	}

	const TSharedPtr<FJsonObject>* McpServersObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("mcpServers"), McpServersObj) || !McpServersObj || !McpServersObj->IsValid())
	{
		UE_LOG(LogTemp, Log,
			TEXT("Nwiro IK: Claude Code config at %s has no mcpServers field — no MCP servers configured"),
			*FoundPath);
		return;
	}

	TArray<FString> Summaries;
	for (const auto& Pair : (*McpServersObj)->Values)
	{
		if (!Pair.Value.IsValid()) continue;
		TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
		if (!Entry.IsValid()) continue;

		FString Type;
		if (!Entry->TryGetStringField(TEXT("type"), Type) || Type.IsEmpty())
		{
			// Anthropic config schema doesn't always include "type". Infer:
			//   has "url"     → http/sse server
			//   has "command" → stdio server (subprocess invocation)
			if (Entry->HasField(TEXT("url"))) Type = TEXT("http");
			else if (Entry->HasField(TEXT("command"))) Type = TEXT("stdio");
			else Type = TEXT("unknown");
		}
		Summaries.Add(FString::Printf(TEXT("%s [%s]"), *Pair.Key, *Type));
	}

	if (Summaries.Num() == 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("Nwiro IK: Claude Code mcpServers field is empty — no MCP servers configured"));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("Nwiro IK: Claude Code has %d MCP server(s) configured: %s. ")
		TEXT("If any are unreachable when claude.exe starts a session, you may see first_token_timeout. ")
		TEXT("Source: %s"),
		Summaries.Num(),
		*FString::Join(Summaries, TEXT(", ")),
		*FoundPath);
}

/** Build the platform-appropriate CLI search paths for a given CLI base name.
 *  Windows for "claude": %APPDATA%\Claude\claude-code\<latest-version>\claude.exe (installer)
 *           then %USERPROFILE%\.local\bin\{name}.{exe,cmd,(none)} + %APPDATA%\npm\{name}.cmd
 *  Windows other:        only .local\bin + npm fallbacks
 *  Unix:    $HOME/.local/bin, /usr/local/bin, /opt/homebrew/bin (Apple Silicon),
 *           and common npm-global prefixes. Order matters — first hit wins. */
static TArray<FString> BuildCliCandidates(const FString& CliBaseName)
{
	TArray<FString> Out;
#if PLATFORM_WINDOWS
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));

	// Claude only: prefer the Anthropic installer's versioned path over any stale
	// .local\bin shim. See FindClaudeInstallerPaths() for the failure mode this
	// fixes (TypeError on effortLevel → silent 180s first_token_timeout).
	if (CliBaseName == TEXT("claude"))
	{
		for (const FString& InstallerPath : FindClaudeInstallerPaths())
		{
			Out.Add(InstallerPath);
		}
	}

	Out.Add(FPaths::Combine(UserProfile, TEXT(".local"), TEXT("bin"), CliBaseName + TEXT(".exe")));
	Out.Add(FPaths::Combine(UserProfile, TEXT(".local"), TEXT("bin"), CliBaseName + TEXT(".cmd")));
	Out.Add(FPaths::Combine(UserProfile, TEXT(".local"), TEXT("bin"), CliBaseName));
	Out.Add(FPaths::Combine(AppData, TEXT("npm"), CliBaseName + TEXT(".cmd")));
#else
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	Out.Add(FPaths::Combine(Home, TEXT(".local"), TEXT("bin"), CliBaseName));
	Out.Add(FPaths::Combine(Home, TEXT(".npm-global"), TEXT("bin"), CliBaseName));
	Out.Add(FPaths::Combine(Home, TEXT(".volta"), TEXT("bin"), CliBaseName));
	Out.Add(FPaths::Combine(Home, TEXT(".nvm"), TEXT("versions"), TEXT("node")) /* nvm: caller can stick the latest under here */);
	Out.Add(TEXT("/opt/homebrew/bin/") + CliBaseName);   // Apple Silicon Homebrew
	Out.Add(TEXT("/usr/local/bin/") + CliBaseName);      // Intel Homebrew + plain npm -g
	Out.Add(TEXT("/usr/bin/") + CliBaseName);
#endif
	return Out;
}

static TArray<FAdapterInfo> GetAdapterRegistry()
{
	TArray<FAdapterInfo> Registry;

	// Claude
	{
		FAdapterInfo A;
		A.Id = TEXT("claude");
		A.BinaryName = TEXT("claude-agent-acp");
		// DownloadUrl is intentionally empty: the JS layer (app/src/utils/queries.ts)
		// fetches the correct GitHub release asset for the current platform/arch and
		// passes it to bridge.downloadadapter(adapter, url). Keeping it out of C++
		// means new releases / repo moves don't require a plugin recompile.
		A.DownloadUrl = TEXT("");
		A.EnvKey = TEXT("CLAUDE_CODE_EXECUTABLE");
		A.ExeCandidates = BuildCliCandidates(TEXT("claude"));
		Registry.Add(MoveTemp(A));
	}

	// Codex
	{
		FAdapterInfo A;
		A.Id = TEXT("codex");
		A.BinaryName = TEXT("codex-acp");
		// DownloadUrl supplied by JS layer — see Claude comment above.
		A.DownloadUrl = TEXT("");
		A.ExeCandidates = BuildCliCandidates(TEXT("codex"));
		Registry.Add(MoveTemp(A));
	}

	return Registry;
}

static const FAdapterInfo* FindAdapter(const FString& Id)
{
	static TArray<FAdapterInfo> Registry = GetAdapterRegistry();
	for (const FAdapterInfo& A : Registry)
	{
		if (A.Id == Id) return &A;
	}
	return nullptr;
}

// adapter-reliability-w0: deterministic preflight before EnsureProcess spawns
// the adapter binary. File-existence checks only — intentionally no `--version`
// probe, no auth detection, no MCP check; those land in Wave 3. Returns true
// when healthy. On false, OutCode/OutMessage describe the failure for an
// adapter_error event. Two failure modes:
//   - adapter_exe_missing: the resolved adapter binary (e.g. claude-agent-acp)
//     isn't on disk where FindAdapterBinary said it would be.
//   - cli_missing: the adapter is a shim (EnvKey set) and none of its
//     ExeCandidates exist. Without this check, the shim spawns and answers
//     `initialize` itself, then hangs `session/new` waiting for the absent
//     CLI to delegate to (Hypothesis #1 in tasks/todo.md).
static bool CheckAdapterBinaries(const FString& AdapterId, const FString& AdapterBinary,
	FString& OutCode, FString& OutMessage)
{
	// Empty AdapterBinary means FindAdapterBinary couldn't resolve a path —
	// treat as missing so the user gets a specific error instead of the
	// generic adapter_launch_failed that would fire after the spawn attempt.
	if (AdapterBinary.IsEmpty() || !NwiroPathExists(AdapterBinary))
	{
		OutCode = TEXT("adapter_exe_missing");
		OutMessage = AdapterBinary.IsEmpty()
			? FString::Printf(TEXT("Adapter binary for '%s' could not be located. Open Settings and click Reinstall Adapter."), *AdapterId)
			: FString::Printf(TEXT("Adapter binary not found at %s. Reinstall the adapter or pick a different one in Settings."), *AdapterBinary);
		return false;
	}

	const FAdapterInfo* AdapterInfo = FindAdapter(AdapterId);
	if (AdapterInfo && !AdapterInfo->EnvKey.IsEmpty())
	{
		for (const FString& Candidate : AdapterInfo->ExeCandidates)
		{
			if (NwiroPathExists(Candidate)) return true;
		}
		OutCode = TEXT("cli_missing");
#if PLATFORM_WINDOWS
		const TCHAR* SearchHint = TEXT("%APPDATA%\\Claude\\claude-code\\<version> (preferred — Anthropic installer), %USERPROFILE%\\.local\\bin, or %APPDATA%\\npm");
#else
		const TCHAR* SearchHint = TEXT("~/.local/bin, /opt/homebrew/bin or /usr/local/bin");
#endif
		OutMessage = FString::Printf(
			TEXT("%s adapter requires the underlying CLI to be installed (none of the expected paths under %s exist). Install the CLI from the upstream docs and retry."),
			*AdapterId, SearchHint);
		return false;
	}

	return true;
}

static FString GetOptionalStringField(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName)
{
	FString Value;
	return Obj.IsValid() && Obj->TryGetStringField(FieldName, Value) ? Value : TEXT("");
}

static TSharedPtr<FJsonObject> GetOptionalObjectField(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName)
{
	if (!Obj.IsValid()) return nullptr;
	const TSharedPtr<FJsonValue>* Field = Obj->Values.Find(FieldName);
	return Field && Field->IsValid() ? (*Field)->AsObject() : nullptr;
}

static bool HasTerminalContent(const TSharedPtr<FJsonObject>& Obj)
{
	const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
	if (!Obj.IsValid() || !Obj->TryGetArrayField(TEXT("content"), Content)) return false;

	for (const TSharedPtr<FJsonValue>& Item : *Content)
	{
		TSharedPtr<FJsonObject> ContentObj = Item.IsValid() ? Item->AsObject() : nullptr;
		if (!ContentObj.IsValid()) continue;
		if (GetOptionalStringField(ContentObj, TEXT("type")).Equals(TEXT("terminal"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

static bool HasShellInputFields(const TSharedPtr<FJsonObject>& Obj)
{
	return Obj.IsValid()
		&& (Obj->HasField(TEXT("command"))
			|| Obj->HasField(TEXT("cwd"))
			|| Obj->HasField(TEXT("terminalId")));
}

static bool LooksLikeShellTitle(const FString& Title)
{
	return Title.Contains(TEXT("terminal/"), ESearchCase::IgnoreCase)
		|| Title.Contains(TEXT("PowerShell"), ESearchCase::IgnoreCase)
		|| Title.Contains(TEXT("cmd.exe"), ESearchCase::IgnoreCase)
		|| Title.Contains(TEXT("bash"), ESearchCase::IgnoreCase)
		|| Title.Contains(TEXT("shell"), ESearchCase::IgnoreCase)
		|| Title.StartsWith(TEXT("Get-"), ESearchCase::IgnoreCase)
		|| Title.StartsWith(TEXT("Set-"), ESearchCase::IgnoreCase)
		|| Title.StartsWith(TEXT("New-"), ESearchCase::IgnoreCase)
		|| Title.StartsWith(TEXT("Remove-"), ESearchCase::IgnoreCase)
		|| Title.StartsWith(TEXT("Start-"), ESearchCase::IgnoreCase)
		|| Title.Equals(TEXT("pwd"), ESearchCase::IgnoreCase)
		|| Title.Equals(TEXT("dir"), ESearchCase::IgnoreCase)
		|| Title.Equals(TEXT("ls"), ESearchCase::IgnoreCase);
}

static FString MakeToolCallKey(const FString& SessionId, const FString& ToolCallId)
{
	if (ToolCallId.IsEmpty()) return TEXT("");
	return SessionId.IsEmpty()
		? ToolCallId
		: FString::Printf(TEXT("%s::%s"), *SessionId, *ToolCallId);
}

// adapter-reliability-w2 §8: classified-string table for non-JSON adapter
// stdout/stderr lines. Many adapter failures arrive as bare-text lines (auth
// errors, trust prompts, MCP connection failures) before any RPC times out.
// Mapping these to specific codes lets the user see "auth_required: ..."
// instead of waiting 30s for session_new_timeout. Patterns are case-insensitive
// substrings; first match wins. Keep narrow + adapter-agnostic.
struct FStdoutKeywordPattern
{
	const TCHAR* Needle;     // lowercase substring to match
	const TCHAR* Code;       // classified code (must be in shared/errors allowlist)
	const TCHAR* Stage;      // stage at which the failure conceptually occurred
	const TCHAR* Message;    // user-facing prose paired with the code
};

// Order matters: more specific phrases (longer, less ambiguous) come first
// so e.g. "permission denied" matches before bare "permission" would.
static const FStdoutKeywordPattern STDOUT_KEYWORD_TABLE[] = {
	{ TEXT("not logged in"),         TEXT("auth_required"),        TEXT("creating_session"),
	  TEXT("Adapter reported you are not logged in. Open Claude Code and sign in, then retry.") },
	{ TEXT("unauthorized"),          TEXT("auth_required"),        TEXT("creating_session"),
	  TEXT("Adapter reported unauthorized — Claude Code auth is missing or expired. Sign in via Claude Code and retry.") },
	{ TEXT("permission denied"),     TEXT("project_not_trusted"),  TEXT("creating_session"),
	  TEXT("Adapter reported permission denied — likely a project-trust issue. Open this project in Claude Code once to grant trust.") },
	{ TEXT("project not trusted"),   TEXT("project_not_trusted"),  TEXT("creating_session"),
	  TEXT("Adapter reported the project is not trusted. Open it in Claude Code once and confirm trust.") },
	{ TEXT("handshaking with mcp"),  TEXT("mcp_unavailable"),      TEXT("creating_session"),
	  TEXT("Adapter could not complete the MCP handshake — Nwiro MCP may not be running or the port is wrong.") },
	{ TEXT("mcp startup"),           TEXT("mcp_unavailable"),      TEXT("creating_session"),
	  TEXT("Adapter reported MCP startup failure — try Restart MCP from Settings.") },
	{ TEXT("econnrefused"),          TEXT("mcp_connection_failed"), TEXT("creating_session"),
	  TEXT("Adapter could not connect to MCP server (ECONNREFUSED) — check that Nwiro MCP is running on the expected port.") },
	{ TEXT("connection refused"),    TEXT("mcp_connection_failed"), TEXT("creating_session"),
	  TEXT("Adapter could not connect to MCP server. Check that Nwiro MCP is running.") },
	{ TEXT("failed to start"),       TEXT("adapter_launch_failed"), TEXT("launching_process"),
	  TEXT("Adapter reported a launch failure on stdout. Check the Output Log for details.") },
	// adapter-reliability-w7: catch the farias-documented version-mismatch crash.
	// Older Claude Code (< 2.1.0) throws "TypeError: null is not an object (evaluating 'H.effortLevel')"
	// when newer claude-agent-acp passes the reasoning_effort field it doesn't know about.
	// Manifests as silent first_token_timeout 180s after session/prompt unless caught here.
	// 'effortlevel' (lowercase, minified) is highly specific — extremely low false-positive risk.
	{ TEXT("effortlevel"),           TEXT("adapter_launch_failed"), TEXT("sending_prompt"),
	  TEXT("Claude Code threw an internal error on the reasoning_effort field. ")
	  TEXT("This usually means Claude Code is older than 2.1.0 — incompatible with the current claude-agent-acp. ")
	  TEXT("Update Claude Code via the Anthropic installer (path: %APPDATA%\\Claude\\claude-code\\).") },
};

static const FStdoutKeywordPattern* MatchStdoutKeyword(const FString& Lower)
{
	for (const FStdoutKeywordPattern& Entry : STDOUT_KEYWORD_TABLE)
	{
		if (Lower.Contains(Entry.Needle)) return &Entry;
	}
	return nullptr;
}

static bool IsShellToolCall(const TSharedPtr<FJsonObject>& ToolCall)
{
	if (!ToolCall.IsValid()) return false;

	const FString Kind = GetOptionalStringField(ToolCall, TEXT("kind"));
	if (Kind.Equals(TEXT("execute"), ESearchCase::IgnoreCase)) return true;
	if (HasTerminalContent(ToolCall) || HasShellInputFields(ToolCall)) return true;

	TSharedPtr<FJsonObject> RawInput = GetOptionalObjectField(ToolCall, TEXT("rawInput"));
	if (HasShellInputFields(RawInput)) return true;

	return LooksLikeShellTitle(GetOptionalStringField(ToolCall, TEXT("title")));
}

void UNwiroIKBridge::Log(const FString& Text)
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Text]()
	{
		UE_LOG(LogTemp, Log, TEXT("Nwiro [JS]: %s"), *Text);
	});
}

bool UNwiroIKBridge::IsTCPRunning() const { return FNwiroIKMCPServer::IsRunning(); }
bool UNwiroIKBridge::IsProcessing() const
{
	const FChatSession* S = ChatSessions.Find(ActiveChatId);
	return S ? S->bProcessing : false;
}
void UNwiroIKBridge::SetActiveChat(const FString& ChatId)
{
	ActiveChatId = ChatId;
	if (!ChatId.IsEmpty())
	{
		FChatSession& S = ChatSessions.FindOrAdd(ChatId);
		S.ChatId = ChatId;
		if (S.AdapterId.IsEmpty())
		{
			S.AdapterId = CurrentAdapter;
		}
		else if (S.AdapterId != CurrentAdapter)
		{
			if (!S.SessionId.IsEmpty()) SessionToChatId.Remove(S.SessionId);
			S.AdapterId = CurrentAdapter;
			S.SessionId.Empty();
			S.PendingMessage.Empty();
			S.SessionRpcId = 0;
			S.PromptRpcId = 0;
			S.LastModel.Empty();
			S.SeenToolCallIds.Empty();
		}
		// If adapter is initialized and session not created yet, create it now
		FAdapterProcess* AP = AdapterProcesses.Find(S.AdapterId);
		if (AP && AP->bInitialized && S.SessionId.IsEmpty())
		{
			DoCreateSession(ChatId);
		}
	}
}
void UNwiroIKBridge::SetMode(const FString& Mode) { CurrentMode = Mode; }
void UNwiroIKBridge::SetModel(const FString& Model) { CurrentModel = Model; }
FString UNwiroIKBridge::GetMode() const { return CurrentMode; }
FString UNwiroIKBridge::GetModel() const { return CurrentModel; }
void UNwiroIKBridge::SetAdapter(const FString& Adapter) { CurrentAdapter = Adapter; }
FString UNwiroIKBridge::GetAdapter() const { return CurrentAdapter; }
void UNwiroIKBridge::AddImagePath(const FString& Path) { PendingImages.Add(Path); }
void UNwiroIKBridge::ClearImages() { PendingImages.Empty(); }

FChatSession* UNwiroIKBridge::GetActiveSession()
{
	return ChatSessions.Find(ActiveChatId);
}

FChatSession* UNwiroIKBridge::FindSessionByAcpId(const FString& AcpSessionId)
{
	FString* ChatId = SessionToChatId.Find(AcpSessionId);
	if (ChatId) return ChatSessions.Find(*ChatId);
	return nullptr;
}

FString UNwiroIKBridge::FindChatIdByRpcId(int32 RpcId)
{
	FString* ChatId = RpcToChatId.Find(RpcId);
	return ChatId ? *ChatId : ActiveChatId;
}

// ============================================================
// JSON helpers
// ============================================================

FString UNwiroIKBridge::JsonToString(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

// ============================================================
// Response queue (polled by frontend JS)
// ============================================================

void UNwiroIKBridge::EnqueueResponse(const FString& Type, const FString& Data)
{
	PushEvent(Type, Data, ActiveChatId);
}

void UNwiroIKBridge::EnqueueResponse(const FString& Type, const FString& Data, const FString& ChatId)
{
	PushEvent(Type, Data, ChatId);
}

// adapter-reliability-w0: stage tracking. Records the stage on session +
// adapter, but only logs/notifies when the stage actually changes — multiple
// agent_message_chunk events all assert "streaming", and we don't want N log
// lines per turn for that. The Log-level emission feeds the UE Output Log so
// support can read the lifecycle without verbose-level filters.
void UNwiroIKBridge::SetAdapterStage(const FString& AdapterId, const FString& ChatId, const FString& Stage)
{
	bool bChanged = false;
	if (!ChatId.IsEmpty())
	{
		if (FChatSession* Session = ChatSessions.Find(ChatId))
		{
			if (Session->LastAdapterStage != Stage)
			{
				Session->LastAdapterStage = Stage;
				bChanged = true;
			}
		}
	}
	if (!AdapterId.IsEmpty())
	{
		if (FAdapterProcess* AP = AdapterProcesses.Find(AdapterId))
		{
			if (AP->LastStage != Stage)
			{
				AP->LastStage = Stage;
				bChanged = true;
			}
		}
	}
	if (!bChanged) return;

	UE_LOG(LogTemp, Log,
		TEXT("Nwiro IK: adapter stage adapter=%s chat=%s stage=%s"),
		AdapterId.IsEmpty() ? TEXT("<unset>") : *AdapterId,
		ChatId.IsEmpty() ? TEXT("<none>") : *ChatId,
		*Stage);
}

// adapter-reliability-w0: classified failure event. Records {stage, code,
// message} on the session so the JS-side watchdog (chat.ts) can read them
// if it later fires. Emits three frames for layered compatibility:
//   - adapter_error: rich payload, used by the new classified UI block.
//   - error: legacy text, used by older renderer paths.
//   - done: forces the chat out of "thinking..." regardless of which event
//     the renderer recognises — without it the spinner persists for 90s.
void UNwiroIKBridge::FailChatWithAdapterError(const FString& ChatId, const FString& AdapterId,
	const FString& Stage, const FString& Code, const FString& Message)
{
	UE_LOG(LogTemp, Warning,
		TEXT("Nwiro IK: adapter failure adapter=%s chat=%s stage=%s code=%s message=%s"),
		AdapterId.IsEmpty() ? TEXT("<unset>") : *AdapterId,
		ChatId.IsEmpty() ? TEXT("<none>") : *ChatId,
		*Stage, *Code, *Message);

	if (!ChatId.IsEmpty())
	{
		if (FChatSession* Session = ChatSessions.Find(ChatId))
		{
			Session->LastAdapterStage = Stage;
			Session->LastAdapterCode = Code;
			Session->LastAdapterError = Message;
			Session->bProcessing = false;
		}
	}

	if (ChatId.IsEmpty()) return;

	TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
	Payload->SetStringField(TEXT("adapter"), AdapterId);
	Payload->SetStringField(TEXT("stage"), Stage);
	Payload->SetStringField(TEXT("code"), Code);
	Payload->SetStringField(TEXT("message"), Message);
	EnqueueResponse(TEXT("adapter_error"), JsonToString(Payload), ChatId);
	EnqueueResponse(TEXT("error"), Message, ChatId);
	EnqueueResponse(TEXT("done"), TEXT(""), ChatId);
}

// adapter-reliability-w2 codex-review-fix: shared RPC-state cleanup for any
// path that fails a chat (timeouts, classified stdout, process exit). Failing
// without this leaves SessionRpcId/PromptRpcId non-zero, which (a) lets the
// timeout ticker fire a second classified error for a chat that already
// errored, and (b) makes DoCreateSession's re-entry guard silently no-op the
// next retry. Caller is responsible for clearing PendingMessage / setting
// bRetriedSessionWithoutMcp because those decisions differ between retry and
// terminal-failure paths.
void UNwiroIKBridge::ClearChatRpcState(FChatSession& Session)
{
	if (Session.SessionRpcId != 0) RpcToChatId.Remove(Session.SessionRpcId);
	if (Session.PromptRpcId != 0)  RpcToChatId.Remove(Session.PromptRpcId);
	Session.SessionRpcId = 0;
	Session.PromptRpcId = 0;
	Session.SessionRpcStartedAt = 0.0;
	Session.PromptRpcStartedAt = 0.0;
}

// adapter-reliability-w2: kill + reset an adapter process and trigger a
// fresh launch via EnsureProcess. Used by the session_new_timeout retry path
// to give MCP-isolation retry a clean adapter state. The OnCompleted of the
// outgoing process is guarded by a TWeakPtr identity check (Wave 1
// codex-review-fix) so the old completion can't remove the new entry.
//
// Caller is responsible for clearing per-session in-flight state (SessionRpcId,
// SessionRpcStartedAt) — this helper only manages adapter-level state.
void UNwiroIKBridge::DoRestartAdapter(const FString& AdapterId, const FString& Reason)
{
	UE_LOG(LogTemp, Warning,
		TEXT("Nwiro IK: restarting adapter adapter=%s reason=%s"),
		*AdapterId, *Reason);

	// codex-review-fix: previously bailed when AdapterProcesses had no entry
	// for AdapterId, which is exactly the state after `adapter_exited` (the
	// OnCompleted path Removes the entry). The user-action restart button
	// would then no-op silently while JS reported success. Now we reset
	// per-entry state when one exists, then unconditionally call
	// EnsureProcess to spawn a fresh process — both "stuck" and "missing"
	// adapters end up in the same recovered state.
	if (FAdapterProcess* AP = AdapterProcesses.Find(AdapterId))
	{
		if (AP->Process.IsValid()) AP->Process->Cancel(true);
		AP->Process.Reset();
		AP->bInitialized = false;
		AP->StdoutBuffer.Empty();
		// adapter-reliability-w5: reset BytesFromStdout alongside the buffer —
		// it lives on the per-process struct and would otherwise carry across
		// the restart, producing negative bytesThisTurn deltas on the first
		// post-restart prompt (snapshot taken AFTER restart but counter is
		// still at the pre-restart cumulative).
		AP->BytesFromStdout = 0;
		AP->InitRpcId = 0;
		AP->InitRpcStartedAt = 0.0;
		AP->LastStage = TEXT("");
		AP->LastError = TEXT("");
		AP->LastErrorCode = TEXT("");
		AP->RestartCount++;
		// Fresh process, fresh id space — drop the rejected set so we don't
		// stale-match a session id the new process happens to mint.
		AP->RejectedAcpSessionIds.Empty();
	}

	// Same temp-CurrentAdapter trick StartAdapter uses — EnsureProcess only
	// manages CurrentAdapter, but we may be restarting a non-active one.
	// EnsureProcess uses FindOrAdd, so it works whether the entry existed or
	// we're starting from scratch.
	const FString PrevAdapter = CurrentAdapter;
	CurrentAdapter = AdapterId;
	EnsureProcess();
	CurrentAdapter = PrevAdapter;
}

// adapter-reliability-w1: idempotent ticker registration. The ticker fires
// once per second on the game thread (where ChatSessions/AdapterProcesses are
// otherwise mutated, so no extra synchronization is needed). Captures
// TWeakObjectPtr — without this, an editor hot-reload that GC's the bridge
// would leave a raw `this` in the global ticker queue and crash on next tick.
void UNwiroIKBridge::EnsureTimeoutTickerStarted()
{
	if (TimeoutTickerHandle.IsValid()) return;

	TWeakObjectPtr<UNwiroIKBridge> WeakThis(this);
	TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis](float /*DeltaTime*/) -> bool
		{
			if (UNwiroIKBridge* Strong = WeakThis.Get())
			{
				Strong->CheckAdapterTimeouts();
				return true; // keep ticking
			}
			return false; // bridge GC'd — drop the ticker
		}),
		1.0f
	);
	UE_LOG(LogTemp, Log, TEXT("Nwiro IK: timeout ticker started"));
}

// adapter-reliability-w1: BeginDestroy override removes the ticker so the
// lambda's TWeakObjectPtr never even gets dereferenced post-destruction. We
// also clear the handle so a re-instantiation in the same session (rare but
// possible during dev) starts fresh.
void UNwiroIKBridge::BeginDestroy()
{
	if (TimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutTickerHandle);
		TimeoutTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

// adapter-reliability-w1: per-RPC timeout enforcement. Three independent
// thresholds, one ticker. Order matters: initialize timeouts first (because
// session/new can't start until init succeeds), session/new before prompt
// (because a stuck session/new would block prompt anyway). On timeout, every
// state field that gates retry is cleared — without that, even with a
// classified error the chat would still be wedged behind SessionRpcId != 0.
void UNwiroIKBridge::CheckAdapterTimeouts()
{
	const double Now = FPlatformTime::Seconds();
	// Env-tunable (NWIRO_*_TIMEOUT_SECONDS) — defaults match previous constexpr
	// values. See helper definitions near the top of this file for details.
	const double INITIALIZE_TIMEOUT  = GetInitializeTimeoutSeconds();
	const double SESSION_NEW_TIMEOUT = GetSessionNewTimeoutSeconds();
	const double FIRST_TOKEN_TIMEOUT = GetFirstTokenTimeoutSeconds();

	// initialize timeouts — adapter process up but never replied to initialize.
	for (auto& Pair : AdapterProcesses)
	{
		FAdapterProcess& AP = Pair.Value;
		if (AP.bInitialized || AP.InitRpcId == 0 || AP.InitRpcStartedAt <= 0.0) continue;
		if (Now - AP.InitRpcStartedAt < INITIALIZE_TIMEOUT) continue;

		const FString AdapterId = Pair.Key;
		const int32 TimedOutId = AP.InitRpcId;
		UE_LOG(LogTemp, Warning,
			TEXT("Nwiro IK: adapter timeout adapter=%s stage=initializing_acp rpc=%d elapsedMs=%d"),
			*AdapterId, TimedOutId, (int32)((Now - AP.InitRpcStartedAt) * 1000.0));

		// adapter-reliability-w1 codex-review-fix: terminate the stuck adapter
		// process. Without this, the next SendMessage hits EnsureProcess which
		// short-circuits on `Process.IsRunning()` and never re-sends initialize
		// — the chat gets a classified error but every retry queues a message
		// that can never reach an ACP session. Killing forces respawn on the
		// next send, which sends a fresh `initialize` with a new timestamp.
		if (AP.Process.IsValid())
		{
			AP.Process->Cancel(true);
		}
		AP.Process.Reset();
		AP.bInitialized = false;
		AP.StdoutBuffer.Empty();
		// adapter-reliability-w5: mirror the BytesFromStdout reset done in
		// DoRestartAdapter — same rationale (counter survives the process
		// teardown and would corrupt the next session's diagnostic delta).
		AP.BytesFromStdout = 0;
		AP.InitRpcId = 0;
		AP.InitRpcStartedAt = 0.0;
		RpcToChatId.Remove(TimedOutId);

		// Every chat that was waiting on this adapter's init is stranded.
		// Fail each individually — they may have different ChatIds the user
		// can retry from. Also clear PendingMessage so retry doesn't replay
		// a stale message into a fresh adapter.
		for (auto& SessPair : ChatSessions)
		{
			if (SessPair.Value.AdapterId == AdapterId && SessPair.Value.bProcessing)
			{
				SessPair.Value.PendingMessage.Empty();
				FailChatWithAdapterError(SessPair.Key, AdapterId, TEXT("initializing_acp"),
					TEXT("initialize_timeout"),
					TEXT("Adapter process started but never responded to initialize within 60s. The process has been terminated; the next send will respawn it."));
			}
		}
	}

	// adapter-reliability-w2 codex-review-fix: collect adapters that need a
	// restart instead of calling RestartAdapter inline. RestartAdapter calls
	// EnsureProcess synchronously, which sends an `initialize` RPC — and if
	// any path ever pumps the adapter's response synchronously (or a future
	// contributor adds one), HandleRpcResponse's init-success branch would
	// iterate ChatSessions and call DoCreateSession while we're still inside
	// the for-loop below. Even if today's UE event loop happens to keep that
	// safe, the assumption is fragile. Process pending restarts after the
	// loop ends. AddUnique because multiple chats sharing the same adapter
	// only need one restart.
	TArray<FString> AdaptersToRestart;

	// session/new + first-token timeouts. We iterate ChatSessions once and
	// branch — a session can't be in both states simultaneously (session/new
	// must succeed before session/prompt fires). `continue` after handling
	// session/new ensures we don't double-evaluate the same session this tick.
	for (auto& Pair : ChatSessions)
	{
		FChatSession& Session = Pair.Value;
		const FString ChatId = Pair.Key;
		const FString AdapterId = Session.AdapterId.IsEmpty() ? CurrentAdapter : Session.AdapterId;

		if (Session.SessionRpcId != 0 && Session.SessionId.IsEmpty()
			&& Session.SessionRpcStartedAt > 0.0
			&& Now - Session.SessionRpcStartedAt >= SESSION_NEW_TIMEOUT)
		{
			const int32 TimedOutId = Session.SessionRpcId;
			UE_LOG(LogTemp, Warning,
				TEXT("Nwiro IK: adapter timeout adapter=%s chat=%s stage=creating_session rpc=%d elapsedMs=%d retried=%s"),
				*AdapterId, *ChatId, TimedOutId,
				(int32)((Now - Session.SessionRpcStartedAt) * 1000.0),
				Session.bRetriedSessionWithoutMcp ? TEXT("true") : TEXT("false"));

			// Clear in-flight state in either branch so DoCreateSession's
			// SessionRpcId-non-zero guard doesn't silently no-op later.
			ClearChatRpcState(Session);

			// adapter-reliability-w2 §5+§6: first session_new_timeout doesn't
			// fail the chat — it kicks off a restart + MCP-isolation retry.
			// If the second attempt also times out we land in the else branch
			// and surface the classified error.
			//
			// Why combine restart and MCP-isolation in one retry: we don't
			// know which is broken (adapter wedged vs MCP unhealthy), and
			// the restart cost is small relative to the MCP-isolation
			// information value. If MCP-less retry succeeds, MCP is the
			// likely blocker (notice emitted from HandleRpcResponse). If it
			// fails too, we know it's the adapter/auth/trust path.
			if (!Session.bRetriedSessionWithoutMcp)
			{
				Session.bRetriedSessionWithoutMcp = true;
				// Keep PendingMessage + bProcessing so the post-init auto-fire
				// of DoCreateSession (with bAttachMcp=false) followed by
				// DoSendPrompt picks up the queued message naturally.
				EnqueueResponse(TEXT("system"),
					TEXT("Adapter session creation timed out — restarting and retrying without MCP attached..."),
					ChatId);
				// Defer the restart — see AdaptersToRestart comment above the loop.
				AdaptersToRestart.AddUnique(AdapterId);
				continue;
			}

			// Second timeout — both with and without MCP have failed. Reset
			// the retry flag so a future fresh send (after the user hits
			// retry) starts from default MCP-attached behaviour.
			Session.PendingMessage.Empty();
			Session.bProcessing = false;
			Session.bRetriedSessionWithoutMcp = false;

			FailChatWithAdapterError(ChatId, AdapterId, TEXT("creating_session"),
				TEXT("session_new_timeout"),
				TEXT("Adapter session creation timed out twice (with MCP and without). The adapter is likely wedged or Claude Code auth/trust is blocking it. Try Restart Adapter from Settings, or open this project in Claude Code once to grant trust."));
			continue;
		}

		if (Session.PromptRpcId != 0 && Session.bProcessing
			&& Session.PromptRpcStartedAt > 0.0
			&& Now - Session.PromptRpcStartedAt >= FIRST_TOKEN_TIMEOUT)
		{
			const int32 TimedOutId = Session.PromptRpcId;
			// adapter-reliability-w5: bytes-during-this-turn diagnostic.
			//   0   → adapter went silent (crashed / pipe stuck / never wrote)
			//   >0  → adapter is/was writing; partition further by whether parsed
			//         events arrived (look at the surrounding stream lines)
			// -1 if AP lookup fails — should not happen in practice; logged for
			// completeness so a missing value is distinguishable from zero bytes.
			FAdapterProcess* AP = AdapterProcesses.Find(AdapterId);
			const int64 BytesThisTurn = AP ? (AP->BytesFromStdout - Session.BytesAtPromptSendStart) : -1;
			UE_LOG(LogTemp, Warning,
				TEXT("Nwiro IK: adapter timeout adapter=%s chat=%s stage=waiting_for_first_token rpc=%d elapsedMs=%d bytesThisTurn=%lld"),
				*AdapterId, *ChatId, TimedOutId,
				(int32)((Now - Session.PromptRpcStartedAt) * 1000.0),
				BytesThisTurn);

			// adapter-reliability-w1 codex-review-fix: tell the adapter to stop
			// processing the cancelled turn. Best-effort — the adapter may
			// already be hung, but well-behaved adapters will cease emitting
			// further chunks for this prompt.
			if (!Session.SessionId.IsEmpty())
			{
				TSharedPtr<FJsonObject> CancelParams = MakeShareable(new FJsonObject);
				CancelParams->SetStringField(TEXT("sessionId"), Session.SessionId);
				SendRpcNotification(AdapterId, TEXT("session/cancel"), CancelParams);
			}

			ClearChatRpcState(Session);
			Session.bProcessing = false;

			// adapter-reliability-w1 codex-review-fix: nuke the ACP session
			// mapping AND remember the id so HandleSessionUpdate's
			// bProcessing-fallback can't re-attach late chunks to a retry.
			// SessionToChatId.Remove alone isn't enough — that handler has a
			// "find first waiting chat with empty SessionId" branch that
			// would happily latch the stale id onto a fresh DoCreateSession-
			// in-flight chat. The rejected set lives on FAdapterProcess
			// (codex-review-fix-2) so adapter removal clears it for free.
			if (!Session.SessionId.IsEmpty())
			{
				if (FAdapterProcess* AP = AdapterProcesses.Find(AdapterId))
				{
					AP->RejectedAcpSessionIds.Add(Session.SessionId);
				}
				SessionToChatId.Remove(Session.SessionId);
				Session.SessionId.Empty();
			}

			FailChatWithAdapterError(ChatId, AdapterId, TEXT("waiting_for_first_token"),
				TEXT("first_token_timeout"),
				FString::Printf(TEXT("Adapter accepted the prompt but never produced a first response within %ds. The session has been reset; the next send will create a fresh one."),
					(int32)FIRST_TOKEN_TIMEOUT));
		}
	}

	// adapter-reliability-w2 codex-review-fix: process deferred restarts AFTER
	// the ChatSessions loop has completed. RestartAdapter → EnsureProcess
	// can fire `initialize` and indirectly trigger HandleRpcResponse, which
	// would mutate ChatSessions if it ran reentrantly. Doing the work here
	// keeps the loop body purely state-mutating and reentrancy-free.
	for (const FString& AdapterToRestart : AdaptersToRestart)
	{
		DoRestartAdapter(AdapterToRestart, TEXT("session_new_timeout"));
	}
}

// adapter-reliability-w4b: public UFUNCTION wrapper for the user-action
// "Restart Adapter" button in the chat error bubble. Reason tag distinguishes
// from automatic restart-on-timeout in telemetry/logs.
//
// codex-review-fix: returns bool so JS can render an honest toast. False
// covers two cases the previous void-return version couldn't surface to the
// user:
//   1. Preflight (Wave 0 binary check) refused to spawn — e.g. CLI missing.
//      AdapterProcesses entry exists but Process stays null.
//   2. FInteractiveProcess->Launch() failed at the OS level — e.g. permissions.
// In both cases JS now shows a "could not restart, see Settings" toast
// instead of falsely claiming success.
bool UNwiroIKBridge::RestartAdapter(const FString& Adapter)
{
	UE_LOG(LogTemp, Log, TEXT("Nwiro IK: user-triggered RestartAdapter adapter=%s"), *Adapter);
	DoRestartAdapter(Adapter, TEXT("user_action"));

	const FAdapterProcess* AP = AdapterProcesses.Find(Adapter);
	const bool bRunning = AP && AP->Process.IsValid() && AP->Process->IsRunning();
	UE_LOG(LogTemp, Log, TEXT("Nwiro IK: RestartAdapter result adapter=%s running=%s"),
		*Adapter, bRunning ? TEXT("true") : TEXT("false"));
	return bRunning;
}

FString UNwiroIKBridge::GetChatIdForToolUse(const FString& ToolUseId) const
{
	const FString* Found = ToolUseToChatId.Find(ToolUseId);
	return Found ? *Found : ActiveChatId;
}

void UNwiroIKBridge::PushEvent(const FString& Type, const FString& Data, const FString& ChatId)
{
	// Build the JS string on the calling thread (often a background pipe reader)
	// so the game thread only has to hand the ready-made string to CEF.
	FString EscapedData = Data;
	EscapedData.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	EscapedData.ReplaceInline(TEXT("'"), TEXT("\\'"));
	EscapedData.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	EscapedData.ReplaceInline(TEXT("\r"), TEXT("\\r"));

	FString EscapedChatId = ChatId;
	EscapedChatId.ReplaceInline(TEXT("'"), TEXT("\\'"));

	FString JSCode = FString::Printf(
		TEXT("window.dispatchEvent(new CustomEvent('ue:response', { detail: { type: '%s', data: '%s', chatId: '%s' } }));"),
		*Type, *EscapedData, *EscapedChatId
	);

	// ExecuteJavascript must run on the game thread (CEF requirement), but
	// the escaping + string formatting above already happened off-thread.
	// If we're already on the game thread, call directly to avoid a frame
	// delay; otherwise dispatch.
	if (IsInGameThread())
	{
		if (FNwiroIKPanel::WebBrowserWidget.IsValid())
		{
			FNwiroIKPanel::WebBrowserWidget->ExecuteJavascript(JSCode);
		}
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [JSCode]()
		{
			if (FNwiroIKPanel::WebBrowserWidget.IsValid())
			{
				FNwiroIKPanel::WebBrowserWidget->ExecuteJavascript(JSCode);
			}
		});
	}

}

FString UNwiroIKBridge::PollResponse()
{
	FScopeLock Lock(&ResponseLock);
	if (ResponseQueue.Num() == 0) return TEXT("[]");
	FString Result = TEXT("[");
	for (int32 i = 0; i < ResponseQueue.Num(); ++i)
	{
		if (i > 0) Result += TEXT(",");
		Result += ResponseQueue[i];
	}
	Result += TEXT("]");
	ResponseQueue.Empty();
	return Result;
}

// ============================================================
// Image save (base64 → temp file)
// ============================================================

FString UNwiroIKBridge::SaveImageBase64(const FString& Base64Data, const FString& FileName)
{
	FString Pure = Base64Data;
	int32 CommaIdx;
	if (Pure.FindChar(',', CommaIdx)) Pure = Pure.Mid(CommaIdx + 1);
	TArray<uint8> Decoded;
	FBase64::Decode(Pure, Decoded);
	if (Decoded.Num() == 0) return TEXT("");
	FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NwiroImages"));
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*TempDir);
	FString FilePath = FPaths::Combine(TempDir, FileName);
	FFileHelper::SaveArrayToFile(Decoded, *FilePath);
	FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::MakePlatformFilename(FullPath);
	return FullPath;
}

void UNwiroIKBridge::OpenUrl(const FString& Url)
{
	if (!Url.IsEmpty()) FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
}

void UNwiroIKBridge::OpenImagePicker()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> OutFiles;
	const void* ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
		? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindow,
		TEXT("Select Images"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp"),
		EFileDialogFlags::Multiple,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		for (const FString& FilePath : OutFiles)
		{
			// Read file, convert to base64, send to frontend as image_added event
			TArray<uint8> FileData;
			if (FFileHelper::LoadFileToArray(FileData, *FilePath))
			{
				FString Base64 = FBase64::Encode(FileData);
				FString FileName = FPaths::GetCleanFilename(FilePath);
				FString Ext = FPaths::GetExtension(FilePath).ToLower();
				FString MimeType = Ext == TEXT("png") ? TEXT("image/png") :
					Ext == TEXT("jpg") || Ext == TEXT("jpeg") ? TEXT("image/jpeg") :
					TEXT("image/bmp");

				// Build data URL
				FString DataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Base64);

				// Send to frontend
				TSharedPtr<FJsonObject> ImgInfo = MakeShareable(new FJsonObject);
				ImgInfo->SetStringField(TEXT("name"), FileName);
				ImgInfo->SetStringField(TEXT("path"), FilePath);
				ImgInfo->SetStringField(TEXT("preview"), DataUrl);
				EnqueueResponse(TEXT("image_added"), JsonToString(ImgInfo));

				// Also add to pending images for the message
				AddImagePath(FilePath);
			}
		}
	}
}

FString UNwiroIKBridge::GetPluginInfo() const
{
	TSharedPtr<FJsonObject> Info = MakeShareable(new FJsonObject);
	FString PluginVersion = TEXT("unknown");
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NwiroIntegrationKit"));
	if (Plugin.IsValid()) PluginVersion = Plugin->GetDescriptor().VersionName;
	Info->SetStringField(TEXT("pluginVersion"), PluginVersion);
	Info->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Info->SetStringField(TEXT("mcpProtocol"), TEXT("2025-03-26"));
	Info->SetNumberField(TEXT("mcpPort"), FNwiroIKMCPServer::GetPort());
	Info->SetBoolField(TEXT("mcpRunning"), FNwiroIKMCPServer::IsRunning());
	Info->SetStringField(TEXT("adapter"), CurrentAdapter);
	Info->SetStringField(TEXT("acpProtocol"), TEXT("ACP v1"));
	return JsonToString(Info);
}

// ============================================================
// ACP JSON-RPC send
// ============================================================

void UNwiroIKBridge::SendRaw(const FString& AdapterId, const FString& Json)
{
	FAdapterProcess* AP = AdapterProcesses.Find(AdapterId);
	if (!AP || !AP->Process.IsValid() || !AP->Process->IsRunning()) return;
	// Off by default; enable in console with: Log LogTemp Verbose
	UE_LOG(LogTemp, Verbose, TEXT("ACP send [%s]: %s"), *AdapterId, *Json);
	AP->Process->SendWhenReady(Json + TEXT("\n"));
}

int32 UNwiroIKBridge::SendRpc(const FString& AdapterId, const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	int32 Id = NextRpcId++;
	TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Msg->SetNumberField(TEXT("id"), Id);
	Msg->SetStringField(TEXT("method"), Method);
	if (Params.IsValid())
		Msg->SetObjectField(TEXT("params"), Params);
	else
		Msg->SetObjectField(TEXT("params"), MakeShareable(new FJsonObject));
	SendRaw(AdapterId, JsonToString(Msg));
	return Id;
}

void UNwiroIKBridge::SendRpcNotification(const FString& AdapterId, const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Msg->SetStringField(TEXT("method"), Method);
	if (Params.IsValid())
		Msg->SetObjectField(TEXT("params"), Params);
	SendRaw(AdapterId, JsonToString(Msg));
}

void UNwiroIKBridge::SendRpcResult(const FString& AdapterId, int32 Id, const TSharedPtr<FJsonObject>& Result)
{
	TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Msg->SetNumberField(TEXT("id"), Id);
	Msg->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShareable(new FJsonObject));
	SendRaw(AdapterId, JsonToString(Msg));
}

// ─── codex-acp-string-id ─────────────────────────────────────────────────────
// Echo the original wire-type id back to the agent. Preserves Number | String |
// Null from the inbound `request` so JSON-RPC matching works on the peer side.
// ─────────────────────────────────────────────────────────────────────────────
void UNwiroIKBridge::SendRpcResult(const FString& AdapterId, const Acp::FRequestId& ReqId, const TSharedPtr<FJsonObject>& Result)
{
	TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (ReqId.Value.IsValid())
		Msg->SetField(TEXT("id"), ReqId.Value);
	else
		Msg->SetField(TEXT("id"), MakeShareable(new FJsonValueNull()));
	Msg->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShareable(new FJsonObject));
	SendRaw(AdapterId, JsonToString(Msg));
}

void UNwiroIKBridge::SendRpcError(const FString& AdapterId, const Acp::FRequestId& ReqId, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
	Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (ReqId.Value.IsValid())
		Msg->SetField(TEXT("id"), ReqId.Value);
	else
		Msg->SetField(TEXT("id"), MakeShareable(new FJsonValueNull()));
	TSharedPtr<FJsonObject> ErrObj = MakeShareable(new FJsonObject);
	ErrObj->SetNumberField(TEXT("code"), Code);
	ErrObj->SetStringField(TEXT("message"), Message);
	Msg->SetObjectField(TEXT("error"), ErrObj);
	SendRaw(AdapterId, JsonToString(Msg));
}

// Build the ACP `selected` outcome envelope and ship it back to the adapter.
// Payload shape preserved as `outcome.outcome = "selected"` — this is the live
// adapter contract (claude-code-acp parses response.outcome.outcome). Do not
// rename to `outcome.type` even if a future spec disagrees.
void UNwiroIKBridge::RespondToAcpPermission(const FString& AdapterId, const Acp::FRequestId& ReqId, const FString& OptionId)
{
	TSharedPtr<FJsonObject> Outcome = MakeShareable(new FJsonObject);
	Outcome->SetStringField(TEXT("outcome"), TEXT("selected"));
	Outcome->SetStringField(TEXT("optionId"), OptionId);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetObjectField(TEXT("outcome"), Outcome);

	SendRpcResult(AdapterId, ReqId, Result);
}

#if WITH_EDITOR
void UNwiroIKBridge::DebugInjectFrame(const FString& RawJsonLine)
{
	UE_LOG(LogTemp, Warning, TEXT("Nwiro: DebugInjectFrame: %s"), *RawJsonLine);
	ProcessLine(CurrentAdapter, RawJsonLine);
}
#endif

// ============================================================
// ACP protocol steps
// ============================================================

void UNwiroIKBridge::DoInitialize()
{

	TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
	Params->SetNumberField(TEXT("protocolVersion"), 1);

	// Client capabilities
	TSharedPtr<FJsonObject> Caps = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> FsCaps = MakeShareable(new FJsonObject);
	FsCaps->SetBoolField(TEXT("readTextFile"), true);
	FsCaps->SetBoolField(TEXT("writeTextFile"), false);
	Caps->SetObjectField(TEXT("fs"), FsCaps);
	// Stripping the terminal capability when shell is blocked removes shell
	// from the model's tool surface entirely — Codex can no longer choose
	// what isn't advertised. Prompt-only mitigation ("Do NOT run shell
	// commands") competes against the model's training prior; surface-level
	// removal is deterministic.
	//
	// Note: capability is fixed at ACP `initialize` time per spec — toggling
	// Block Shell mid-session requires adapter relaunch to take effect.
	// This is intentional; document in release notes.
	Caps->SetBoolField(TEXT("terminal"), false);
	Params->SetObjectField(TEXT("clientCapabilities"), Caps);

	// Client info
	TSharedPtr<FJsonObject> ClientInfo = MakeShareable(new FJsonObject);
	ClientInfo->SetStringField(TEXT("name"), TEXT("nwiro"));
	ClientInfo->SetStringField(TEXT("title"), TEXT("Nwiro UE5"));
	ClientInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Params->SetObjectField(TEXT("clientInfo"), ClientInfo);

	// adapter-reliability-w0: stage = initializing_acp.
	SetAdapterStage(CurrentAdapter, ActiveChatId, TEXT("initializing_acp"));
	int32 RpcId = SendRpc(CurrentAdapter, TEXT("initialize"), Params);
	FAdapterProcess* AP = AdapterProcesses.Find(CurrentAdapter);
	if (AP)
	{
		AP->InitRpcId = RpcId;
		// adapter-reliability-w1: arm initialize_timeout.
		AP->InitRpcStartedAt = FPlatformTime::Seconds();
	}
}

void UNwiroIKBridge::DoCreateSession(const FString& ChatId, bool bAttachMcp)
{
	FChatSession& Session = ChatSessions.FindOrAdd(ChatId);
	Session.ChatId = ChatId;
	if (Session.AdapterId.IsEmpty())
	{
		Session.AdapterId = CurrentAdapter;
	}

	// Re-entry guard. Without this, rapid setactivechat→sendmessage sequences
	// fire two session/new RPCs for the same chat: the first stores its RpcId
	// in Session.SessionRpcId, the second overwrites it, and the first
	// response can no longer be matched back to a chat — orphan ACP session.
	// SessionRpcId is cleared on the error path of HandleRpcResponse so a
	// failed session/new does not permanently lock the chat.
	if (!Session.SessionId.IsEmpty() || Session.SessionRpcId != 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("Nwiro IK: DoCreateSession skipped for chat %s — session already in flight or established (SessionRpcId=%d, SessionId=%s)"),
			*ChatId, Session.SessionRpcId,
			Session.SessionId.IsEmpty() ? TEXT("<empty>") : *Session.SessionId);
		return;
	}

	FString WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDir);

	TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
	Params->SetStringField(TEXT("cwd"), WorkingDir);

	// adapter-reliability-w2 §7: log MCP health right before session/new so
	// support can see in the UE log whether MCP was running + attached at the
	// moment session creation kicked off. Helpful when triaging session_new
	// failures — instantly distinguishes "MCP wasn't running" from "MCP was
	// running but hung the handshake."
	const bool bMcpRunning = FNwiroIKMCPServer::IsRunning();
	UE_LOG(LogTemp, Log,
		TEXT("Nwiro IK: MCP health before session/new running=%s port=%d attach=%s adapter=%s chat=%s"),
		bMcpRunning ? TEXT("true") : TEXT("false"),
		FNwiroIKMCPServer::GetPort(),
		bAttachMcp ? TEXT("true") : TEXT("false"),
		*Session.AdapterId, *ChatId);

	TArray<TSharedPtr<FJsonValue>> McpServers;
	// adapter-reliability-w2 §6: bAttachMcp gate. After a session_new_timeout,
	// the retry sets this false so we can prove whether MCP attachment was the
	// blocker — if the retry succeeds without it, MCP is the issue.
	if (bAttachMcp && bMcpRunning)
	{
		TSharedPtr<FJsonObject> McpServer = MakeShareable(new FJsonObject);
		McpServer->SetStringField(TEXT("type"), TEXT("http"));
		McpServer->SetStringField(TEXT("name"), TEXT("nwiro"));
		McpServer->SetStringField(TEXT("url"),
			FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), FNwiroIKMCPServer::GetPort()));

		// No headers — MCP server is loopback-only, no auth needed.
		McpServer->SetArrayField(TEXT("headers"), TArray<TSharedPtr<FJsonValue>>());
		McpServers.Add(MakeShareable(new FJsonValueObject(McpServer)));
	}
	Params->SetArrayField(TEXT("mcpServers"), McpServers);

	// Execute preamble — authored backend-side, pushed by the React app via
	// SetPromptPreambles. Falls back to a baked-in universal default when the
	// app hasn't pushed anything (e.g. backend was unreachable on first send).
	// Extensions snapshot + safety policy (below) are runtime state, not
	// authored prompt content — they stay inline.
	FString PromptText = CurrentExecutePreamble.IsEmpty()
		? FString(TEXT("You are Nwiro AI assistant embedded in Unreal Engine 5 Editor. ")
		          TEXT("Use Nwiro MCP tools for all UE work. Do not run shell commands. ")
		          TEXT("If a tool returns \"extension not enabled\", ask the user to enable it from the Extensions menu."))
		: CurrentExecutePreamble;

	// Extensions can be toggled by the user at any moment during the conversation,
	// so the snapshot below may be stale by the time you read it. Always ATTEMPT
	// the tool when the user requests its functionality — the runtime gate will
	// either let it through or return an explicit "extension not enabled" error.
	// Only after that error appears should you ask the user to enable it.
	PromptText += TEXT("\n\n## EXTENSION TOOLS");
	PromptText += TEXT("\nThese tools are gated behind per-chat extensions that the user can toggle on/off in real time:");
	PromptText += FString::Printf(TEXT("\n- File Editor (write_file, read_file, delete_file, rename_file) — currently: %s"),
		IsChatExtensionEnabled(TEXT("fileEditor")) ? TEXT("ENABLED") : TEXT("disabled (try anyway if user asks)"));
	PromptText += FString::Printf(TEXT("\n- Meshy 3D (generate_3d_model_meshy) — currently: %s"),
		IsChatExtensionEnabled(TEXT("meshy")) ? TEXT("ENABLED") : TEXT("disabled (try anyway if user asks)"));
	PromptText += FString::Printf(TEXT("\n- Tripo 3D (generate_3d_model_tripo) — currently: %s"),
		IsChatExtensionEnabled(TEXT("tripo")) ? TEXT("ENABLED") : TEXT("disabled (try anyway if user asks)"));
	PromptText += TEXT("\nRule: when the user asks for one of these features, ALWAYS call the tool. If the tool returns an \"extension not enabled\" error, only THEN tell the user to enable it from the Extensions menu. Do NOT refuse or invent workarounds based on the snapshot above — it is only a hint and may be out of date.");

	PromptText += TEXT("\n\n## SHELL EXECUTION");
	PromptText += TEXT("\nShell execution is disabled in Nwiro.");
	PromptText += TEXT("\nterminal/create requests will be rejected at the protocol level.");
	PromptText += TEXT("\nIf the user asks you to run shell commands or asks whether you can, say shell execution is disabled in Nwiro.");
	PromptText += TEXT("\nDo NOT claim you can run shell commands. Do NOT attempt shell commands.");

	const FString AdapterId = Session.AdapterId.IsEmpty() ? CurrentAdapter : Session.AdapterId;
	if (AdapterId == TEXT("claude"))
	{
		TSharedPtr<FJsonObject> Meta = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> SystemPrompt = MakeShareable(new FJsonObject);
		SystemPrompt->SetStringField(TEXT("append"), PromptText);
		Meta->SetObjectField(TEXT("systemPrompt"), SystemPrompt);
		Params->SetObjectField(TEXT("_meta"), Meta);
	}
	// Codex deliberately omitted: codex-acp destructures `session/new` to
	// {cwd, mcp_servers, ..} and drops every other field, including
	// `instructions`. The model gets PromptText via the per-turn prepend
	// in DoSendPrompt instead — that path always reaches Codex.

	// adapter-reliability-w0: stage = creating_session.
	SetAdapterStage(AdapterId, ChatId, TEXT("creating_session"));
	Session.SessionRpcId = SendRpc(AdapterId, TEXT("session/new"), Params);
	// adapter-reliability-w1: arm session_new_timeout.
	Session.SessionRpcStartedAt = FPlatformTime::Seconds();
	RpcToChatId.Add(Session.SessionRpcId, ChatId);
}

void UNwiroIKBridge::DoSendPrompt(const FString& ChatId, const FString& Message)
{
	FChatSession* Session = ChatSessions.Find(ChatId);
	if (!Session) return;
	const FString AdapterId = Session->AdapterId.IsEmpty() ? CurrentAdapter : Session->AdapterId;
	if (Session->SessionId.IsEmpty())
	{
		// adapter-reliability-w0: this used to silently return, leaving the
		// chat hanging at "thinking..." until the watchdog fired. Empty
		// SessionId here means session/new either never returned or returned
		// malformed — both are now caught upstream by the schema check, but
		// keep this guard as defence-in-depth and surface a classified error.
		FailChatWithAdapterError(ChatId, AdapterId, TEXT("sending_prompt"),
			TEXT("session_missing"),
			TEXT("Cannot send: session was never created (session/new returned no sessionId or failed earlier)."));
		return;
	}

	Session->bProcessing = true;
	// Ensure session mapping exists
	if (!SessionToChatId.Contains(Session->SessionId))
		SessionToChatId.Add(Session->SessionId, ChatId);

	// Set model via config option — skip "default" (adapter uses its own default)
	if (!CurrentModel.IsEmpty() && CurrentModel != TEXT("default") && CurrentModel != Session->LastModel)
	{
		Session->LastModel = CurrentModel;
		TSharedPtr<FJsonObject> ModelParams = MakeShareable(new FJsonObject);
		ModelParams->SetStringField(TEXT("sessionId"), Session->SessionId);
		ModelParams->SetStringField(TEXT("configId"), TEXT("model"));
		ModelParams->SetStringField(TEXT("value"), CurrentModel);
		SendRpc(AdapterId, TEXT("session/set_config_option"), ModelParams);
	}

	TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
	Params->SetStringField(TEXT("sessionId"), Session->SessionId);

	TArray<TSharedPtr<FJsonValue>> Prompt;

	for (const FString& ImgPath : PendingImages)
	{
		TArray<uint8> FileData;
		if (FFileHelper::LoadFileToArray(FileData, *ImgPath))
		{
			TSharedPtr<FJsonObject> ImgBlock = MakeShareable(new FJsonObject);
			ImgBlock->SetStringField(TEXT("type"), TEXT("image"));
			ImgBlock->SetStringField(TEXT("data"), FBase64::Encode(FileData));
			ImgBlock->SetStringField(TEXT("mimeType"), TEXT("image/png"));
			Prompt.Add(MakeShareable(new FJsonValueObject(ImgBlock)));
		}
	}
	PendingImages.Empty();

	// Build the user-text block. Two prepends, in order:
	//   1. ExecutePreamble (Codex only): codex-acp dropped session/new
	//      `instructions`, so this is the only path that reaches the model
	//      with our authored system prompt. Claude already got it via
	//      session/new._meta.systemPrompt.append in DoCreateSession; adding
	//      it here too would just double the tokens.
	//   2. PlanPreamble (both adapters, when CurrentMode == "plan"): per-turn
	//      reminder that tools are denied this turn so the model produces a
	//      plan instead of retry-looping when the host gate rejects calls.
	FString PromptText = Message;
	if (CurrentMode == TEXT("plan"))
	{
		const FString PlanText = CurrentPlanPreamble.IsEmpty()
			? FString(TEXT("[PLAN MODE] Do not call any tools — they will be denied. ")
			          TEXT("Describe a step-by-step plan from your UE5 knowledge. ")
			          TEXT("Ask the user to disable Plan Mode to apply."))
			: CurrentPlanPreamble;
		PromptText = PlanText + TEXT("\n\n---\nUser message:\n") + PromptText;
	}
	// Skip the execute prepend in Plan Mode: it tells the model to use Nwiro
	// MCP tools, which contradicts the plan-mode "do not call any tools" rule
	// and was confusing Codex into hedging instead of producing a real plan.
	if (AdapterId == TEXT("codex")
		&& CurrentMode != TEXT("plan")
		&& !CurrentExecutePreamble.IsEmpty())
	{
		PromptText = CurrentExecutePreamble + TEXT("\n\n---\n") + PromptText;
	}
	PromptText = TEXT("[SHELL DISABLED] Shell execution is disabled in Nwiro. ")
		TEXT("If the user asks whether you can run shell commands, say shell execution is disabled in Nwiro. ")
		TEXT("Do not claim you can run shell commands.")
		TEXT("\n\n---\n") + PromptText;

	TSharedPtr<FJsonObject> TextBlock = MakeShareable(new FJsonObject);
	TextBlock->SetStringField(TEXT("type"), TEXT("text"));
	TextBlock->SetStringField(TEXT("text"), PromptText);
	Prompt.Add(MakeShareable(new FJsonValueObject(TextBlock)));
	Params->SetArrayField(TEXT("prompt"), Prompt);

	// adapter-reliability-w0: stage = sending_prompt.
	SetAdapterStage(AdapterId, ChatId, TEXT("sending_prompt"));
	Session->PromptRpcId = SendRpc(AdapterId, TEXT("session/prompt"), Params);
	// adapter-reliability-w1: arm first_token_timeout. Cleared either when
	// the first agent_message_chunk arrives (HandleSessionUpdate) or when the
	// prompt response itself returns (HandleRpcResponse — for adapters that
	// never stream and reply in one shot).
	Session->PromptRpcStartedAt = FPlatformTime::Seconds();
	// adapter-reliability-w5: snapshot the adapter's lifetime byte counter at
	// the exact moment we arm the timeout, so the ticker can compute
	// bytes-during-this-turn = AP->BytesFromStdout - Session->BytesAtPromptSendStart.
	// AP lookup is cheap (TMap) and we're already past the SendRpc hot path.
	if (FAdapterProcess* AP = AdapterProcesses.Find(AdapterId))
	{
		Session->BytesAtPromptSendStart = AP->BytesFromStdout;
	}
	RpcToChatId.Add(Session->PromptRpcId, ChatId);
}

// ============================================================
// Process stdout — parse ACP messages
// ============================================================

void UNwiroIKBridge::ProcessLine(const FString& AdapterId, const FString& Line)
{
	if (Line.IsEmpty()) return;
	// Off by default; enable in console with: Log LogTemp Verbose
	UE_LOG(LogTemp, Verbose, TEXT("ACP recv: %s"), *Line);

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: ACP stdout line was not JSON: %s"), *Line);

		// adapter-reliability-w2 §8: try to classify the bare-text line. Many
		// adapter failure paths (auth, trust, MCP, launch) print a recognisable
		// phrase to stdout/stderr before any RPC could time out — catching them
		// here gives the user a specific code in 0s instead of 30s.
		const FStdoutKeywordPattern* Match = MatchStdoutKeyword(Line.ToLower());
		if (Match)
		{
			// Snapshot processing chats so we can iterate while
			// FailChatWithAdapterError mutates each session's bProcessing.
			//
			// codex-review-fix: filter to chats whose adapter actually
			// produced this line. Without the filter, a Claude "unauthorized"
			// stdout line would also fail any concurrent Codex chat — wrong
			// adapter id pinned to the wrong failure.
			TArray<FString> ProcessingChats;
			for (auto& Pair : ChatSessions)
			{
				if (!Pair.Value.bProcessing) continue;
				const FString SessionAdapterId = Pair.Value.AdapterId.IsEmpty()
					? CurrentAdapter : Pair.Value.AdapterId;
				if (SessionAdapterId != AdapterId) continue;
				ProcessingChats.Add(Pair.Key);
			}
			for (const FString& ChatId : ProcessingChats)
			{
				FChatSession* Session = ChatSessions.Find(ChatId);
				if (!Session) continue;
				// codex-review-fix: clear stuck RPC state before failing.
				// FailChatWithAdapterError only flips bProcessing → false; if
				// SessionRpcId/PromptRpcId/timestamps stay populated, the
				// timeout ticker can later fire a SECOND classified error for
				// the same chat, and DoCreateSession's re-entry guard will
				// silently no-op a retry.
				ClearChatRpcState(*Session);
				Session->PendingMessage.Empty();
				FailChatWithAdapterError(ChatId, AdapterId,
					Match->Stage, Match->Code, Match->Message);
			}
		}
		return;
	}

	// Check if frame carries an id (response, error, or agent-originated request)
	TSharedPtr<FJsonValue> IdVal = Acp::ExtractRpcId(Json);
	if (IdVal.IsValid())
	{
		const bool bIsResponse = Json->HasField(TEXT("result")) || Json->HasField(TEXT("error"));
		if (bIsResponse)
		{
			// Responses match against bridge-minted numeric ids in RpcToChatId.
			// If the peer ever sends a non-numeric response id we cannot match
			// it anyway; log and drop rather than silently coercing to 0.
			if (IdVal->Type != EJson::Number)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Nwiro: dropping ACP response with non-numeric id (type=%d) — bridge only mints numeric ids"),
					(int32)IdVal->Type);
				return;
			}
			int32 Id = (int32)IdVal->AsNumber();
			if (Json->HasField(TEXT("result")))
				HandleRpcResponse(Id, Json->GetObjectField(TEXT("result")), nullptr);
			else
				HandleRpcResponse(Id, nullptr, Json->GetObjectField(TEXT("error")));
			return;
		}

		// Agent-originated request (id + method). The id may be Number | String
		// | Null per JSON-RPC 2.0 — HandleMethod re-extracts via Acp::ExtractRpcId
		// to round-trip the original wire type on the response.
		if (Json->HasField(TEXT("method")))
		{
			FString Method = Json->GetStringField(TEXT("method"));
			HandleMethod(AdapterId, Method, Json);
			return;
		}
	}

	// Notification from agent (has "method" but no "id")
	if (Json->HasField(TEXT("method")))
	{
		FString Method = Json->GetStringField(TEXT("method"));
		HandleMethod(AdapterId, Method, Json);
	}
}

void UNwiroIKBridge::HandleRpcResponse(int32 Id, const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error)
{
	FString ChatId = FindChatIdByRpcId(Id);
	RpcToChatId.Remove(Id);

	if (Error.IsValid())
	{
		FString ErrMsg = Error->HasField(TEXT("message")) ? Error->GetStringField(TEXT("message")) : TEXT("Unknown error");
		UE_LOG(LogTemp, Error, TEXT("Nwiro: ACP error (id=%d, chat=%s): %s"), Id, *ChatId, *ErrMsg);

		// Only push error to UI for critical RPCs (session/new, prompt)
		// Non-critical RPCs (set_mode etc.) just log
		bool bIsCritical = false;
		FChatSession* Session = ChatId.IsEmpty() ? nullptr : ChatSessions.Find(ChatId);
		if (Session)
		{
			if (Session->SessionRpcId == Id || Session->PromptRpcId == Id)
				bIsCritical = true;
		}

		if (bIsCritical)
		{
			EnqueueResponse(TEXT("error"), ErrMsg, ChatId);
			if (Session && Session->PromptRpcId == Id)
			{
				Session->bProcessing = false;
				EnqueueResponse(TEXT("done"), TEXT(""), ChatId);
			}
			// Clear the in-flight marker so DoCreateSession's re-entry guard
			// allows a retry. Without this, a single failed session/new
			// (network blip, codex-acp crash, bad params) would leave
			// SessionRpcId non-zero forever and the chat could never recover.
			if (Session && Session->SessionRpcId == Id)
			{
				Session->SessionRpcId = 0;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Nwiro: Non-critical ACP error ignored (id=%d): %s"), Id, *ErrMsg);
		}
		return;
	}

	// Initialize response — check all adapter processes
	bool isInitResponse = false;
	FString InitializedAdapter;
	for (auto& APair : AdapterProcesses)
	{
		if (APair.Value.InitRpcId == Id)
		{
			APair.Value.bInitialized = true;
			// adapter-reliability-w1: disarm initialize_timeout — InitRpcId
			// stays non-zero on success (it's the matched response), but the
			// startedAt timestamp tells the ticker "already done."
			APair.Value.InitRpcStartedAt = 0.0;
			isInitResponse = true;
			InitializedAdapter = APair.Key;
			break;
		}
	}
	if (isInitResponse)
	{
		UE_LOG(LogTemp, Log, TEXT("Nwiro IK: ACP adapter initialized (adapter=%s, rpc=%d)"), *InitializedAdapter, Id);

		// Create sessions for any chats that were waiting (or just opened).
		// adapter-reliability-w2: respect bRetriedSessionWithoutMcp — after a
		// session_new_timeout we restart the adapter; once initialize comes
		// back here, the auto-fire below would otherwise re-attach MCP and
		// undo the isolation retry. Pass !flag so the retry actually exercises
		// the MCP-less path that the timeout handler wanted to test.
		for (auto& Pair : ChatSessions)
		{
			if (Pair.Value.AdapterId == InitializedAdapter && Pair.Value.SessionId.IsEmpty())
				DoCreateSession(Pair.Key, !Pair.Value.bRetriedSessionWithoutMcp);
		}
		return;
	}

	// Session/new response — find which chat this was for
	for (auto& Pair : ChatSessions)
	{
		if (Pair.Value.SessionRpcId == Id)
		{
			// adapter-reliability-w0: schema-validate before storing. Older
			// versions of claude-agent-acp return `sessionId` as a string at
			// the top level; a future schema change (rename, nest under
			// `session.id`, drop entirely) would have left us with an empty
			// string and a downstream silent abort in DoSendPrompt. Now we
			// fail loudly with `protocol_mismatch` and the raw response in
			// the log so the version drift is visible in support tickets.
			const TSharedPtr<FJsonValue>* SessionIdField = Result.IsValid()
				? Result->Values.Find(TEXT("sessionId")) : nullptr;
			const bool bHasValidSessionId =
				SessionIdField != nullptr && SessionIdField->IsValid()
				&& (*SessionIdField)->Type == EJson::String
				&& !(*SessionIdField)->AsString().IsEmpty();
			const FString FailedAdapterId = Pair.Value.AdapterId.IsEmpty() ? CurrentAdapter : Pair.Value.AdapterId;
			if (!bHasValidSessionId)
			{
				const FString RawJson = Result.IsValid() ? JsonToString(Result) : TEXT("<no result>");
				UE_LOG(LogTemp, Warning,
					TEXT("Nwiro IK: session/new returned no valid sessionId, raw=%s"),
					*RawJson);
				Pair.Value.SessionRpcId = 0;
				FailChatWithAdapterError(Pair.Key, FailedAdapterId, TEXT("creating_session"),
					TEXT("protocol_mismatch"),
					TEXT("Adapter session/new response is missing a valid sessionId — likely a protocol version mismatch. Update the plugin or the adapter binary."));
				return;
			}
			const FString NewSessionId = (*SessionIdField)->AsString();
			Pair.Value.SessionId = NewSessionId;
			// adapter-reliability-w1: disarm session_new_timeout.
			Pair.Value.SessionRpcStartedAt = 0.0;
			SessionToChatId.Add(NewSessionId, Pair.Key);
			UE_LOG(LogTemp, Log, TEXT("Nwiro IK: ACP session created for chat %s (sessionId=%s)"),
				*Pair.Key, *NewSessionId);
			const FString AdapterId = FailedAdapterId;

			// adapter-reliability-w2 §6: if this session was created via the
			// MCP-isolation retry, the original attempt-with-MCP must have been
			// the blocker. Tell the user that succinctly so they can act on it
			// (restart MCP, check port, etc.) rather than chalking it up to a
			// transient hiccup. Reset the flag so subsequent prompts in this
			// chat go back to the default MCP-attached path.
			if (Pair.Value.bRetriedSessionWithoutMcp)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Nwiro IK: session created only after MCP isolation retry — Nwiro MCP likely unhealthy adapter=%s chat=%s"),
					*AdapterId, *Pair.Key);
				EnqueueResponse(TEXT("system"),
					TEXT("Adapter session created only after disabling MCP attachment. The Nwiro MCP connection may be blocked or unhealthy — try Restart MCP from Settings."),
					Pair.Key);
				Pair.Value.bRetriedSessionWithoutMcp = false;
			}

			// Set mode to auto/full (some adapters start in read-only).
			// Claude's ACP implementation rejects session/set_mode with
			// `Internal error`, so we skip the call entirely for it
			// instead of spamming the log with non-critical errors.
			if (AdapterId != TEXT("claude"))
			{
				TSharedPtr<FJsonObject> ModeParams = MakeShareable(new FJsonObject);
				ModeParams->SetStringField(TEXT("sessionId"), NewSessionId);
				ModeParams->SetStringField(TEXT("modeId"), TEXT("auto"));
				SendRpc(AdapterId, TEXT("session/set_mode"), ModeParams);
			}


			if (!Pair.Value.PendingMessage.IsEmpty())
			{
				FString Msg = Pair.Value.PendingMessage;
				Pair.Value.PendingMessage.Empty();
				DoSendPrompt(Pair.Key, Msg);
			}
			return;
		}
	}

	// Prompt response (done) — find which chat
	for (auto& Pair : ChatSessions)
	{
		if (Pair.Value.PromptRpcId == Id)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Nwiro IK: ACP prompt completed for chat %s (rpc=%d)"), *Pair.Key, Id);
			Pair.Value.bProcessing = false;
			// adapter-reliability-w5: log bytes-before-response for the one-shot
			// (non-streaming) success path. Guarded by `> 0.0` so the streaming
			// path doesn't double-log — when HandleSessionUpdate already cleared
			// this on first chunk, the guard skips us. Verbose level matches the
			// streaming-path log for grep consistency.
			if (Pair.Value.PromptRpcStartedAt > 0.0)
			{
				const FAdapterProcess* APLog = AdapterProcesses.Find(Pair.Value.AdapterId);
				const int64 BytesBeforeFirstChunk = APLog
					? (APLog->BytesFromStdout - Pair.Value.BytesAtPromptSendStart)
					: -1;
				UE_LOG(LogTemp, Verbose,
					TEXT("Nwiro IK: first chunk received (one-shot) adapter=%s chat=%s elapsedMs=%d bytesBeforeFirstChunk=%lld"),
					*Pair.Value.AdapterId, *Pair.Key,
					(int32)((FPlatformTime::Seconds() - Pair.Value.PromptRpcStartedAt) * 1000.0),
					BytesBeforeFirstChunk);
			}
			// adapter-reliability-w1: disarm first_token_timeout. Belt-and-
			// braces — HandleSessionUpdate already cleared this on first
			// stream chunk, but adapters that reply in one shot (no streaming)
			// jump straight here, so clear again for that case.
			Pair.Value.PromptRpcStartedAt = 0.0;
			EnqueueResponse(TEXT("done"), TEXT(""), Pair.Key);
			return;
		}
	}
}

void UNwiroIKBridge::HandleMethod(const FString& AdapterId, const FString& Method, const TSharedPtr<FJsonObject>& FullMsg)
{
	TSharedPtr<FJsonObject> Params = FullMsg->HasField(TEXT("params"))
		? FullMsg->GetObjectField(TEXT("params")) : nullptr;
	// Union-aware id read (codex-acp-string-id fix). Replaces the prior
	// `(int32)GetNumberField("id")` which logged a `String used as a Number`
	// LogJson error and produced 0 whenever codex emitted a string id.
	//
	// JSON-RPC 2.0: `"id": null` is a request that REQUIRES a response with id
	// echoed as null — distinct from a missing `id` field (notification, no
	// response). HasId reflects field-present, not field-non-null; the new
	// SendRpcResult overload round-trips the null wire value verbatim.
	Acp::FRequestId ReqId(Acp::ExtractRpcId(FullMsg));
	const bool HasId = ReqId.IsValid();

	// session/update — streaming content (includes sessionId for routing)
	if (Method == TEXT("session/update") && Params.IsValid())
	{
		FString AcpSessionId = Params->HasField(TEXT("sessionId"))
			? Params->GetStringField(TEXT("sessionId")) : TEXT("");
		TSharedPtr<FJsonObject> Update = Params->HasField(TEXT("update"))
			? Params->GetObjectField(TEXT("update")) : nullptr;
		if (Update.IsValid()) HandleSessionUpdate(AdapterId, AcpSessionId, Update);
		return;
	}

	// session/request_permission — permission popup
	if (Method == TEXT("session/request_permission") && Params.IsValid())
	{
		// Extract options from ACP
		TArray<TSharedPtr<FJsonValue>> OptArr;
		if (Params->HasField(TEXT("options")))
			OptArr = Params->GetArrayField(TEXT("options"));

		// ── Mode-based auto-response ──

		// Helper: find first option matching a kind
		auto FindOption = [&](const FString& KindMatch) -> FString {
			for (const auto& OptVal : OptArr) {
				TSharedPtr<FJsonObject> Opt = OptVal->AsObject();
				if (!Opt.IsValid()) continue;
				FString Kind = Opt->HasField(TEXT("kind")) ? Opt->GetStringField(TEXT("kind")) : TEXT("");
				if (Kind.Contains(KindMatch, ESearchCase::IgnoreCase)) return Opt->GetStringField(TEXT("optionId"));
			}
			return TEXT("");
		};

		// Bypass Mode → auto-approve all permission prompts
		// Shell safety outranks mode-level auto-approval.
		TSharedPtr<FJsonObject> PermissionToolCall = GetOptionalObjectField(Params, TEXT("toolCall"));
		FString PermissionToolCallId = GetOptionalStringField(PermissionToolCall, TEXT("toolCallId"));
		FString PermissionSessionId = GetOptionalStringField(Params, TEXT("sessionId"));
		FString PermissionToolCallKey = MakeToolCallKey(PermissionSessionId, PermissionToolCallId);
		const bool bShellPermission =
			IsShellToolCall(PermissionToolCall)
			|| (!PermissionToolCallKey.IsEmpty() && BlockedShellToolCallIds.Contains(PermissionToolCallKey));

		if (bShellPermission)
		{
			FString Opt = FindOption(TEXT("reject"));
			if (Opt.IsEmpty()) Opt = FindOption(TEXT("deny"));
			if (Opt.IsEmpty()) Opt = TEXT("deny");

			FString Title = GetOptionalStringField(PermissionToolCall, TEXT("title"));
			if (Title.IsEmpty()) Title = PermissionToolCallId.IsEmpty() ? TEXT("(unknown)") : PermissionToolCallId;
			UE_LOG(LogTemp, Warning,
				TEXT("Nwiro IK: BLOCKED shell permission request '%s' (shell disabled)"), *Title);
			RespondToAcpPermission(AdapterId, ReqId, Opt);

			FString PermissionChatId = !PermissionSessionId.IsEmpty()
				? SessionToChatId.FindRef(PermissionSessionId) : TEXT("");
			if (PermissionChatId.IsEmpty() && !PermissionToolCallId.IsEmpty())
				PermissionChatId = ToolUseToChatId.FindRef(PermissionToolCallId);
			if (PermissionChatId.IsEmpty())
				PermissionChatId = ActiveChatId;

			if (!PermissionChatId.IsEmpty())
			{
				if (FChatSession* PermissionSession = ChatSessions.Find(PermissionChatId); PermissionSession && PermissionSession->bProcessing)
				{
					if (!PermissionSessionId.IsEmpty())
					{
						TSharedPtr<FJsonObject> CancelParams = MakeShareable(new FJsonObject);
						CancelParams->SetStringField(TEXT("sessionId"), PermissionSessionId);
						SendRpcNotification(AdapterId, TEXT("session/cancel"), CancelParams);
					}
					PermissionSession->bProcessing = false;
					EnqueueResponse(TEXT("done"), TEXT(""), PermissionChatId);
				}
			}
			return;
		}

		// Bypass Mode: auto-approve non-shell permission prompts.
		if (CurrentMode == TEXT("bypassPermissions"))
		{
			FString Opt = FindOption(TEXT("allow"));
			if (Opt.IsEmpty()) Opt = FindOption(TEXT("approve"));
			if (Opt.IsEmpty() && OptArr.Num() > 0) Opt = OptArr[0]->AsObject()->GetStringField(TEXT("optionId"));
			RespondToAcpPermission(AdapterId, ReqId, Opt);
			return;
		}

		// Accept Edits → auto-approve all (edits are the main use case)
		if (CurrentMode == TEXT("acceptEdits"))
		{
			FString Opt = FindOption(TEXT("allow"));
			if (Opt.IsEmpty()) Opt = FindOption(TEXT("approve"));
			if (Opt.IsEmpty() && OptArr.Num() > 0) Opt = OptArr[0]->AsObject()->GetStringField(TEXT("optionId"));
			RespondToAcpPermission(AdapterId, ReqId, Opt);
			return;
		}

		// Plan Mode → auto-deny all tool calls
		if (CurrentMode == TEXT("plan"))
		{
			FString Opt = FindOption(TEXT("reject"));
			if (Opt.IsEmpty()) Opt = FindOption(TEXT("deny"));
			if (Opt.IsEmpty()) Opt = TEXT("deny");
			RespondToAcpPermission(AdapterId, ReqId, Opt);

			// Notify user
			FString ChatId = ActiveChatId;
			if (!ChatId.IsEmpty())
				EnqueueResponse(TEXT("system"),
					TEXT("Plan mode: tool execution blocked — disable Plan Mode to act on the project."),
					ChatId);

			return;
		}

		// Don't Ask → auto-deny (no prompts, if not pre-allowed then deny)
		if (CurrentMode == TEXT("dontAsk"))
		{
			FString Opt = FindOption(TEXT("reject"));
			if (Opt.IsEmpty()) Opt = FindOption(TEXT("deny"));
			if (Opt.IsEmpty()) Opt = TEXT("deny");
			RespondToAcpPermission(AdapterId, ReqId, Opt);
			return;
		}

		// Default → show permission card to user.
		// Mint a local int32 id for the React contract and remember the original
		// ACP id (which may be string/null on the wire) so we can echo it back
		// verbatim when the user clicks Allow/Deny. Negative range keeps ACP
		// ids permanently disjoint from MCP's >= 1000 range.
		const int32 LocalPermId = NextLocalPermId--;
		LocalPermIdToAcpReqId.Add(LocalPermId, ReqId);
		LocalPermIdToAdapterId.Add(LocalPermId, AdapterId);

		TSharedPtr<FJsonObject> PermJson = MakeShareable(new FJsonObject);
		PermJson->SetNumberField(TEXT("id"), LocalPermId);

		if (Params->HasField(TEXT("toolCall")))
		{
			TSharedPtr<FJsonObject> ToolCall = Params->GetObjectField(TEXT("toolCall"));
			PermJson->SetStringField(TEXT("toolName"),
				ToolCall->HasField(TEXT("title")) ? ToolCall->GetStringField(TEXT("title")) : TEXT("Unknown"));
		}

		TArray<TSharedPtr<FJsonValue>> OutOpts;
		for (const auto& OptVal : OptArr)
		{
			TSharedPtr<FJsonObject> Opt = OptVal->AsObject();
			if (!Opt.IsValid()) continue;
			TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
			O->SetStringField(TEXT("optionId"), Opt->GetStringField(TEXT("optionId")));
			O->SetStringField(TEXT("name"), Opt->GetStringField(TEXT("name")));
			O->SetStringField(TEXT("kind"), Opt->HasField(TEXT("kind")) ? Opt->GetStringField(TEXT("kind")) : TEXT(""));
			OutOpts.Add(MakeShareable(new FJsonValueObject(O)));
		}
		PermJson->SetArrayField(TEXT("options"), OutOpts);

		EnqueueResponse(TEXT("permission_request"), JsonToString(PermJson));
		return;
	}

	// fs/read_text_file — agent wants to read a file
	if (Method == TEXT("fs/read_text_file") && HasId && Params.IsValid())
	{
		FString FilePath = Params->HasField(TEXT("path")) ? Params->GetStringField(TEXT("path")) : TEXT("");
		auto ReadIntParam = [&](const TCHAR* Key) -> int32
		{
			const TSharedPtr<FJsonValue> Value = Params->TryGetField(Key);
			if (!Value.IsValid()) return 0;
			if (Value->Type == EJson::Number) return static_cast<int32>(Value->AsNumber());
			if (Value->Type == EJson::String) return FCString::Atoi(*Value->AsString());
			return 0;
		};
		int32 Line = ReadIntParam(TEXT("line"));
		int32 Limit = ReadIntParam(TEXT("limit"));

		FString Content;
		if (FFileHelper::LoadFileToString(Content, *FilePath))
		{
			// Apply line/limit if specified
			if (Line > 0 || Limit > 0)
			{
				TArray<FString> Lines;
				Content.ParseIntoArrayLines(Lines);
				int32 Start = FMath::Max(0, Line - 1);
				int32 End = Limit > 0 ? FMath::Min(Start + Limit, Lines.Num()) : Lines.Num();
				Content.Empty();
				for (int32 i = Start; i < End; ++i)
				{
					Content += Lines[i];
					if (i < End - 1) Content += TEXT("\n");
				}
			}

			TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
			Res->SetStringField(TEXT("content"), Content);
			SendRpcResult(AdapterId, ReqId, Res);
		}
		else
		{
			SendRpcError(AdapterId, ReqId, -32002, FString::Printf(TEXT("File not found: %s"), *FilePath));
		}
		return;
	}

	// terminal/create — agent wants to run a command
	if (Method == TEXT("terminal/create") && HasId && Params.IsValid())
	{
		FString Command = Params->HasField(TEXT("command"))
			? Params->GetStringField(TEXT("command")) : TEXT("(unknown)");
		UE_LOG(LogTemp, Warning,
			TEXT("Nwiro IK: BLOCKED terminal/create — command='%s' (shell disabled)"), *Command);

		SendRpcError(AdapterId, ReqId, -32002,
			TEXT("Shell execution is disabled in Nwiro. Use nwiro MCP tools instead."));

		if (!ActiveChatId.IsEmpty())
		{
			EnqueueResponse(TEXT("system"),
				FString::Printf(TEXT("Blocked shell command: %s"), *Command),
				ActiveChatId);
		}
		return;
	}

	// terminal/output
	if (Method == TEXT("terminal/output") && HasId)
	{
		if (bBlockCommandExecution) { SendRpcResult(AdapterId, ReqId, MakeShareable(new FJsonObject)); return; }
		const FString TermId = Params.IsValid() && Params->HasField(TEXT("terminalId"))
			? Params->GetStringField(TEXT("terminalId")) : TEXT("");
		const FTerminalResult* TerminalResult = TerminalResults.Find(TermId);
		TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
		Res->SetStringField(TEXT("output"), TerminalResult ? TerminalResult->Output : TEXT(""));
		Res->SetBoolField(TEXT("truncated"), false);
		TSharedPtr<FJsonObject> ExitStatus = MakeShareable(new FJsonObject);
		ExitStatus->SetNumberField(TEXT("exitCode"), TerminalResult ? TerminalResult->ExitCode : 0);
		Res->SetObjectField(TEXT("exitStatus"), ExitStatus);
		SendRpcResult(AdapterId, ReqId, Res);
		return;
	}

	// terminal/wait_for_exit, terminal/kill, terminal/release
	if ((Method == TEXT("terminal/wait_for_exit") || Method == TEXT("terminal/kill") || Method == TEXT("terminal/release")) && HasId)
	{
		if (bBlockCommandExecution) { SendRpcResult(AdapterId, ReqId, MakeShareable(new FJsonObject)); return; }
		const FString TermId = Params.IsValid() && Params->HasField(TEXT("terminalId"))
			? Params->GetStringField(TEXT("terminalId")) : TEXT("");
		TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
		if (Method == TEXT("terminal/wait_for_exit"))
		{
			const FTerminalResult* TerminalResult = TerminalResults.Find(TermId);
			Res->SetNumberField(TEXT("exitCode"), TerminalResult ? TerminalResult->ExitCode : 0);
		}
		else if (Method == TEXT("terminal/release"))
		{
			TerminalResults.Remove(TermId);
		}
		SendRpcResult(AdapterId, ReqId, Res);
		return;
	}

	// mcp/connect — agent wants to connect to our in-process MCP server
	if (Method == TEXT("mcp/connect") && HasId && Params.IsValid())
	{
		FString AcpId = Params->HasField(TEXT("acpId")) ? Params->GetStringField(TEXT("acpId")) : TEXT("");

		TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
		Res->SetStringField(TEXT("connectionId"), TEXT("nwiro-mcp-conn"));
		SendRpcResult(AdapterId, ReqId, Res);
		return;
	}

	// mcp/message — agent sends MCP JSON-RPC messages to our in-process server
	if (Method == TEXT("mcp/message") && HasId && Params.IsValid())
	{
		TSharedPtr<FJsonObject> McpMsg = Params->HasField(TEXT("message"))
			? Params->GetObjectField(TEXT("message")) : nullptr;

		if (McpMsg.IsValid())
		{
			// Process the MCP message through our existing MCP server dispatch
			TSharedPtr<FJsonObject> McpResult = FNwiroIKMCPServer::ProcessJsonRpc(McpMsg);

			TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
			Res->SetObjectField(TEXT("message"), McpResult);
			SendRpcResult(AdapterId, ReqId, Res);

		}
		else
		{
			TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
			SendRpcResult(AdapterId, ReqId, Res);
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Nwiro: Unhandled ACP method: %s"), *Method);
}

void UNwiroIKBridge::HandleSessionUpdate(const FString& AdapterId, const FString& AcpSessionId, const TSharedPtr<FJsonObject>& Update)
{
	// adapter-reliability-w1 codex-review-fix: drop updates for sessions we
	// explicitly cancelled (first_token_timeout). Without this, the
	// bProcessing-fallback below would happily route a late chunk from the
	// timed-out session into a fresh retry chat and stream stale text into
	// the new prompt's bubble. Per-adapter so adapter restart/removal clears
	// the set for free (codex-review-fix-2).
	if (!AcpSessionId.IsEmpty())
	{
		if (FAdapterProcess* AP = AdapterProcesses.Find(AdapterId);
			AP && AP->RejectedAcpSessionIds.Contains(AcpSessionId))
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("Nwiro IK: dropping session/update for rejected adapter=%s sessionId=%s"),
				*AdapterId, *AcpSessionId);
			return;
		}
	}

	// Resolve chatId from ACP sessionId
	FString ChatId;
	FString* FoundChatId = SessionToChatId.Find(AcpSessionId);
	if (FoundChatId)
	{
		ChatId = *FoundChatId;
	}
	else
	{
		// Session not mapped yet — find by checking which chat is waiting for this session
		for (auto& Pair : ChatSessions)
		{
			if (Pair.Value.bProcessing && Pair.Value.SessionId.IsEmpty())
			{
				ChatId = Pair.Key;
				// Also set the mapping now so future events are routed correctly
				if (!AcpSessionId.IsEmpty())
				{
					Pair.Value.SessionId = AcpSessionId;
					SessionToChatId.Add(AcpSessionId, Pair.Key);
				}
				break;
			}
		}
		if (ChatId.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("Nwiro: Could not resolve chatId for session %s, falling back to active"), *AcpSessionId);
			ChatId = ActiveChatId;
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("Nwiro: SessionUpdate routed to chat %s (session %s)"), *ChatId, *AcpSessionId);

	FChatSession* Session = ChatSessions.Find(ChatId);

	FString UpdateType = Update->HasField(TEXT("sessionUpdate"))
		? Update->GetStringField(TEXT("sessionUpdate")) : TEXT("");

	if (UpdateType == TEXT("user_message_chunk")) return;

	if (UpdateType == TEXT("agent_message_chunk"))
	{
		if (Update->HasField(TEXT("content")))
		{
			TSharedPtr<FJsonObject> Content = Update->GetObjectField(TEXT("content"));
			FString ContentType = Content->HasField(TEXT("type")) ? Content->GetStringField(TEXT("type")) : TEXT("");
			if (ContentType == TEXT("text") && Content->HasField(TEXT("text")))
			{
				// adapter-reliability-w0: stage = streaming. Idempotent helper
				// dedupes per-chunk repetition so the log stays low-volume.
				const FString StreamAdapterId = (Session && !Session->AdapterId.IsEmpty())
					? Session->AdapterId : CurrentAdapter;
				SetAdapterStage(StreamAdapterId, ChatId, TEXT("streaming"));
				// adapter-reliability-w5: happy-path bytes-before-first-chunk log,
				// mirror of the timeout-path bytesThisTurn line. Same field shape so
				// grep "bytesBeforeFirstChunk|bytesThisTurn" gives a unified
				// latency-vs-bytes distribution across success + failure. Verbose
				// because this fires on every turn — flip on with
				// `Log LogTemp Verbose` when collecting distributions.
				if (Session && Session->PromptRpcStartedAt > 0.0)
				{
					const FAdapterProcess* APLog = AdapterProcesses.Find(StreamAdapterId);
					const int64 BytesBeforeFirstChunk = APLog
						? (APLog->BytesFromStdout - Session->BytesAtPromptSendStart)
						: -1;
					UE_LOG(LogTemp, Verbose,
						TEXT("Nwiro IK: first chunk received adapter=%s chat=%s elapsedMs=%d bytesBeforeFirstChunk=%lld"),
						*StreamAdapterId, *ChatId,
						(int32)((FPlatformTime::Seconds() - Session->PromptRpcStartedAt) * 1000.0),
						BytesBeforeFirstChunk);
				}
				// adapter-reliability-w1: first token has arrived — disarm
				// first_token_timeout. Long-running streams (multi-minute
				// generations) are now legitimate; we no longer time them out.
				if (Session) Session->PromptRpcStartedAt = 0.0;
				EnqueueResponse(TEXT("stream"), Content->GetStringField(TEXT("text")), ChatId);
			}
		}
		return;
	}

	if (UpdateType == TEXT("agent_thought_chunk"))
	{
		EnqueueResponse(TEXT("thinking"), TEXT(""), ChatId);
		return;
	}

	if (UpdateType == TEXT("tool_call"))
	{

		// Map toolCallId → chatId for MCP permission routing
		FString ToolCallIdForMap = Update->HasField(TEXT("toolCallId")) ? Update->GetStringField(TEXT("toolCallId")) : TEXT("");
		if (!ToolCallIdForMap.IsEmpty()) ToolUseToChatId.Add(ToolCallIdForMap, ChatId);

		FString ToolCallId = Update->HasField(TEXT("toolCallId")) ? Update->GetStringField(TEXT("toolCallId")) : TEXT("");
		FString ToolStatus = Update->HasField(TEXT("status")) ? Update->GetStringField(TEXT("status")) : TEXT("");

		if (IsShellToolCall(Update))
		{
			if (!ToolCallId.IsEmpty()) BlockedShellToolCallIds.Add(MakeToolCallKey(AcpSessionId, ToolCallId));
			FString Title = GetOptionalStringField(Update, TEXT("title"));
			if (Title.IsEmpty()) Title = ToolCallId.IsEmpty() ? TEXT("(unknown)") : ToolCallId;
			UE_LOG(LogTemp, Warning,
				TEXT("Nwiro IK: BLOCKED shell tool_call '%s' before UI display"), *Title);
			if (!ChatId.IsEmpty())
			{
				EnqueueResponse(TEXT("system"),
					FString::Printf(TEXT("Blocked shell command: %s"), *Title),
					ChatId);
			}
			if (Session && Session->bProcessing)
			{
				if (!AcpSessionId.IsEmpty())
				{
					const FString ChatAdapterId = Session->AdapterId.IsEmpty() ? CurrentAdapter : Session->AdapterId;
					TSharedPtr<FJsonObject> CancelParams = MakeShareable(new FJsonObject);
					CancelParams->SetStringField(TEXT("sessionId"), AcpSessionId);
					SendRpcNotification(ChatAdapterId, TEXT("session/cancel"), CancelParams);
				}
				Session->bProcessing = false;
				EnqueueResponse(TEXT("done"), TEXT(""), ChatId);
			}
			return;
		}

		// Skip if already seen OR if it's a status update (not initial "pending")
		if (Session)
		{
			if (!ToolCallId.IsEmpty() && Session->SeenToolCallIds.Contains(ToolCallId)) return;
			if (!ToolCallId.IsEmpty()) Session->SeenToolCallIds.Add(ToolCallId);
		}
		// Skip completed/failed tool_call events (they're final status updates)
		if (ToolStatus == TEXT("completed") || ToolStatus == TEXT("failed")) return;

		FString Title = Update->HasField(TEXT("title")) ? Update->GetStringField(TEXT("title")) : TEXT("");
		// Generic: extract tool name after last "/" or "__" separator
		int32 SlashIdx = INDEX_NONE;
		Title.FindLastChar('/', SlashIdx);
		if (SlashIdx != INDEX_NONE)
			Title = Title.Mid(SlashIdx + 1);
		// Also handle "__" separator (e.g. "mcp__nwiro__create_blueprint")
		int32 DunderIdx = Title.Find(TEXT("__"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (DunderIdx != INDEX_NONE)
			Title = Title.Mid(DunderIdx + 2);
		Title.ReplaceInline(TEXT("_"), TEXT(" "));

		TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
		Data->SetStringField(TEXT("name"), Title);
		Data->SetStringField(TEXT("id"), ToolCallId);
		EnqueueResponse(TEXT("tool_start"), JsonToString(Data), ChatId);

		// Extract rawInput if present
		if (Update->HasField(TEXT("rawInput")))
		{
			TSharedPtr<FJsonObject> RawInput = Update->GetObjectField(TEXT("rawInput"));
			// Use "arguments" sub-object if present, otherwise whole rawInput
			FString InputStr;
			if (RawInput.IsValid() && RawInput->HasField(TEXT("arguments")))
				InputStr = JsonToString(RawInput->GetObjectField(TEXT("arguments")));
			else if (RawInput.IsValid())
				InputStr = JsonToString(RawInput);
			if (!InputStr.IsEmpty())
			{
				TSharedPtr<FJsonObject> UpdData = MakeShareable(new FJsonObject);
				UpdData->SetStringField(TEXT("id"), ToolCallId);
				UpdData->SetStringField(TEXT("input"), InputStr);
				EnqueueResponse(TEXT("tool_update"), JsonToString(UpdData), ChatId);
			}
		}
		return;
	}

	if (UpdateType == TEXT("tool_call_update"))
	{
		FString Status = Update->HasField(TEXT("status")) ? Update->GetStringField(TEXT("status")) : TEXT("");
		FString ToolCallId = Update->HasField(TEXT("toolCallId")) ? Update->GetStringField(TEXT("toolCallId")) : TEXT("");

		FString ToolCallKey = MakeToolCallKey(AcpSessionId, ToolCallId);
		if (!ToolCallKey.IsEmpty() && BlockedShellToolCallIds.Contains(ToolCallKey))
		{
			if (Status == TEXT("completed") || Status == TEXT("failed"))
			{
				BlockedShellToolCallIds.Remove(ToolCallKey);
			}
			return;
		}

		if (IsShellToolCall(Update))
		{
			if (!ToolCallKey.IsEmpty()) BlockedShellToolCallIds.Add(ToolCallKey);
			FString Title = GetOptionalStringField(Update, TEXT("title"));
			if (Title.IsEmpty()) Title = ToolCallId.IsEmpty() ? TEXT("(unknown)") : ToolCallId;
			UE_LOG(LogTemp, Warning,
				TEXT("Nwiro IK: BLOCKED shell tool_call_update '%s' before UI display"), *Title);
			if (!ChatId.IsEmpty())
			{
				EnqueueResponse(TEXT("system"),
					FString::Printf(TEXT("Blocked shell command: %s"), *Title),
					ChatId);
			}
			if (Session && Session->bProcessing)
			{
				if (!AcpSessionId.IsEmpty())
				{
					const FString ChatAdapterId = Session->AdapterId.IsEmpty() ? CurrentAdapter : Session->AdapterId;
					TSharedPtr<FJsonObject> CancelParams = MakeShareable(new FJsonObject);
					CancelParams->SetStringField(TEXT("sessionId"), AcpSessionId);
					SendRpcNotification(ChatAdapterId, TEXT("session/cancel"), CancelParams);
				}
				Session->bProcessing = false;
				EnqueueResponse(TEXT("done"), TEXT(""), ChatId);
			}
			return;
		}

		// Extract rawInput (comes as object, serialize to string)
		if (Update->HasField(TEXT("rawInput")))
		{
			TSharedPtr<FJsonObject> RawInput = Update->GetObjectField(TEXT("rawInput"));
			if (RawInput.IsValid() && RawInput->Values.Num() > 0)
			{
				FString InputStr = JsonToString(RawInput);
				TSharedPtr<FJsonObject> UpdData = MakeShareable(new FJsonObject);
				UpdData->SetStringField(TEXT("id"), ToolCallId);
				UpdData->SetStringField(TEXT("input"), InputStr);
				EnqueueResponse(TEXT("tool_update"), JsonToString(UpdData), ChatId);

			}
		}

		if (Status == TEXT("completed") || Status == TEXT("failed"))
		{
			TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
			Data->SetStringField(TEXT("id"), ToolCallId);
			Data->SetStringField(TEXT("status"), Status);

			// rawOutput: Codex 0.13.0 wraps as {"content":[{type,text}]}, older as bare [{type,text}],
			// some adapters may send a plain string.
			FString Combined;
			const TSharedPtr<FJsonValue> Raw = Update->TryGetField(TEXT("rawOutput"));

			auto ExtractRawOutputItems = [&]() -> const TArray<TSharedPtr<FJsonValue>>*
			{
				if (!Raw.IsValid()) return nullptr;
				if (Raw->Type == EJson::Object)
				{
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					Raw->AsObject()->TryGetArrayField(TEXT("content"), Arr);
					return Arr;
				}
				if (Raw->Type == EJson::Array)
				{
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					Update->TryGetArrayField(TEXT("rawOutput"), Arr);
					return Arr;
				}
				return nullptr;
			};

			if (const TArray<TSharedPtr<FJsonValue>>* RawArr = ExtractRawOutputItems())
			{
				for (const auto& Item : *RawArr)
				{
					FString Text;
					if (!Item.IsValid() || Item->Type != EJson::Object) continue;
					TSharedPtr<FJsonObject> O = Item->AsObject();
					if (O.IsValid() && O->TryGetStringField(TEXT("text"), Text))
					{
						if (!Combined.IsEmpty()) Combined += TEXT("\n");
						Combined += Text;
					}
				}
			}
			else if (Raw.IsValid() && Raw->Type == EJson::String)
			{
				Combined = Raw->AsString();
			}

			if (!Combined.IsEmpty())
			{
				Data->SetStringField(TEXT("output"), Combined);
			}

			EnqueueResponse(TEXT("tool_end"), JsonToString(Data), ChatId);
		}
		return;
	}

	// Session info update
	if (UpdateType == TEXT("session_info_update"))
	{
		return; // Could update title
	}

	// Current mode update
	if (UpdateType == TEXT("current_mode_update"))
	{
		FString ModeId = Update->HasField(TEXT("currentModeId")) ? Update->GetStringField(TEXT("currentModeId")) : TEXT("");
		CurrentMode = ModeId;
		return;
	}
}

// ============================================================
// Permission response
// ============================================================

void UNwiroIKBridge::RespondToPermission(int32 RequestId, const FString& OptionId)
{
	// ─── codex-acp-string-id ─────────────────────────────────────────────────
	// ACP permission card: bridge minted RequestId as a LocalPermId, the real
	// ACP id (which may be string/null) lives in LocalPermIdToAcpReqId. Look up
	// and echo with the original wire type so the agent can match it.
	// ─────────────────────────────────────────────────────────────────────────
	if (Acp::FRequestId* AcpReqId = LocalPermIdToAcpReqId.Find(RequestId))
	{
		const FString AdapterId = LocalPermIdToAdapterId.FindRef(RequestId);
		RespondToAcpPermission(AdapterId.IsEmpty() ? CurrentAdapter : AdapterId, *AcpReqId, OptionId);
		LocalPermIdToAcpReqId.Remove(RequestId);
		LocalPermIdToAdapterId.Remove(RequestId);
		return;
	}

	// MCP tool permission (in-process server). FNwiroIKMCPServer::NextPermissionId
	// starts at 1000, so its id range is naturally disjoint from LocalPermId.
	if (RequestId >= 1000)
	{
		bool bAllowed = OptionId.Contains(TEXT("allow")) || OptionId.Contains(TEXT("approved"));
		if (OptionId == TEXT("allow_session"))
		{
			FNwiroIKMCPServer::bSessionAllowed = true;
			bAllowed = true;
		}
		FNwiroIKMCPServer::RespondToToolPermission(RequestId, bAllowed);
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("Nwiro: RespondToPermission ignored unknown id %d (no ACP map entry, below MCP range)"),
		RequestId);
}

// ============================================================
// Adapter binary discovery
// ============================================================

FString UNwiroIKBridge::FindAdapterBinary() const
{
	const FAdapterInfo* Info = FindAdapter(CurrentAdapter);
	if (!Info) return TEXT("");

#if PLATFORM_WINDOWS
	FString ExeName = Info->BinaryName + TEXT(".exe");
#else
	FString ExeName = Info->BinaryName;
#endif

	// Search paths in priority order
	TArray<FString> SearchDirs = {
		FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NwiroIntegrationKit"))->GetBaseDir(), TEXT("Binaries")),
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries")),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NwiroIntegrationKit")),
	};

	for (const FString& Dir : SearchDirs)
	{
		FString FullPath = FPaths::Combine(Dir, ExeName);
		if (NwiroPathExists(FullPath)) return FullPath;
	}

	return ExeName; // Fallback to PATH
}

bool UNwiroIKBridge::IsAdapterAvailable(const FString& Adapter)
{
	FString OldAdapter = CurrentAdapter;
	CurrentAdapter = Adapter;
	FString Path = FindAdapterBinary();
	CurrentAdapter = OldAdapter;
	return NwiroPathExists(Path);
}

bool UNwiroIKBridge::IsCliAvailable(const FString& Adapter)
{
	FString CliName = Adapter; // claude, codex etc.

	// First check known paths
	const FAdapterInfo* Info = FindAdapter(Adapter);
	if (Info)
	{
		for (const FString& Candidate : Info->ExeCandidates)
		{
			if (NwiroPathExists(Candidate)) return true;
		}
	}

	// Fallback: use 'where' (Windows) to search PATH
#if PLATFORM_WINDOWS
	int32 RetCode = 0;
	FString StdOut, StdErr;
	FPlatformProcess::ExecProcess(TEXT("where"), *CliName, &RetCode, &StdOut, &StdErr);
	return RetCode == 0 && !StdOut.TrimStartAndEnd().IsEmpty();
#else
	int32 RetCode = 0;
	FString StdOut, StdErr;
	FPlatformProcess::ExecProcess(TEXT("/usr/bin/which"), *CliName, &RetCode, &StdOut, &StdErr);
	return RetCode == 0;
#endif
}

void UNwiroIKBridge::DeleteAdapter(const FString& Adapter)
{
	const FAdapterInfo* Info = FindAdapter(Adapter);
	if (!Info) return;

#if PLATFORM_WINDOWS
	FString ExeName = Info->BinaryName + TEXT(".exe");
#else
	FString ExeName = Info->BinaryName;
#endif

	FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NwiroIntegrationKit"));
	FString ExePath = FPaths::Combine(SaveDir, ExeName);

	// Kill adapter process on game thread (fast)
	KillProcess();

	// Background: force-kill + wait + delete
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ExeName, ExePath, Adapter]()
	{
#if PLATFORM_WINDOWS
		FString KillCmd = FString::Printf(TEXT("/C taskkill /F /IM \"%s\" >nul 2>&1"), *ExeName);
		FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *KillCmd, nullptr, nullptr, nullptr);
#endif
		FPlatformProcess::Sleep(1.0f);

		bool bDeleted = false;
		if (FPaths::FileExists(ExePath))
			bDeleted = IFileManager::Get().Delete(*ExePath, false, true);

		AsyncTask(ENamedThreads::GameThread, [this, bDeleted, Adapter]()
		{
			if (bDeleted)
				PushEvent(TEXT("download_progress"), FString::Printf(TEXT("{\"adapter\":\"%s\",\"status\":\"deleted\"}"), *Adapter));
			else
				PushEvent(TEXT("download_progress"), FString::Printf(TEXT("{\"adapter\":\"%s\",\"status\":\"delete_failed\"}"), *Adapter));
		});
	});
}

void UNwiroIKBridge::DownloadAdapter(const FString& Adapter, const FString& Url)
{
	const FAdapterInfo* Info = FindAdapter(Adapter);
	if (!Info)
	{
		EnqueueResponse(TEXT("error"), FString::Printf(TEXT("Unknown adapter: %s"), *Adapter));
		return;
	}

#if PLATFORM_WINDOWS
	FString BinaryName = Info->BinaryName + TEXT(".exe");
#else
	FString BinaryName = Info->BinaryName;
#endif

	// Some adapters ship .tar.gz on Unix (codex), others ship .zip (claude on
	// every platform, codex on Windows). Pick the suffix from the URL so we
	// save it with the right extension and pick the right extractor below.
	const bool bIsTarGz = Url.EndsWith(TEXT(".tar.gz")) || Url.EndsWith(TEXT(".tgz"));
	const FString ArchiveExt = bIsTarGz ? TEXT("-download.tar.gz") : TEXT("-download.zip");
	FString ZipName = Info->BinaryName + ArchiveExt;

	FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NwiroIntegrationKit"));
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*SaveDir);

	FString ZipPath = FPaths::Combine(SaveDir, ZipName);
	FString ExePath = FPaths::Combine(SaveDir, BinaryName);
	// URL must be supplied by the JS layer — no hardcoded fallback so a stale
	// release URL can never sneak in via a recompiled plugin.
	FString DownloadUrl = Url.IsEmpty() ? Info->DownloadUrl : Url;
	if (DownloadUrl.IsEmpty())
	{
		EnqueueResponse(TEXT("error"), FString::Printf(
			TEXT("No download URL provided for %s. The frontend should pass it via bridge.downloadadapter(adapter, url)."),
			*Adapter));
		return;
	}

	// Already downloaded?
	if (FPaths::FileExists(ExePath))
	{
		EnqueueResponse(TEXT("system"), FString::Printf(TEXT("%s is already available."), *Adapter));
		return;
	}

	PushEvent(TEXT("download_progress"), FString::Printf(TEXT("{\"adapter\":\"%s\",\"progress\":0,\"total\":0,\"status\":\"starting\"}"), *Adapter));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(DownloadUrl);
	HttpRequest->SetVerb(TEXT("GET"));

	HttpRequest->OnRequestProgress64().BindLambda([this, Adapter](FHttpRequestPtr, uint64 BytesSent, uint64 BytesReceived)
	{
		float ProgressMB = static_cast<float>(BytesReceived) / (1024.0f * 1024.0f);
		PushEvent(TEXT("download_progress"), FString::Printf(TEXT("{\"adapter\":\"%s\",\"progress\":%.1f,\"status\":\"downloading\"}"), *Adapter, ProgressMB));
	});

	HttpRequest->OnProcessRequestComplete().BindLambda([this, ZipPath, ExePath, SaveDir, Adapter, bIsTarGz](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString Detail = FString::Printf(
				TEXT("Download HTTP failed (success=%s, code=%d) — check network, proxy, or GitHub availability"),
				bSuccess ? TEXT("true") : TEXT("false"), Code);
			UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: adapter '%s' %s"), *Adapter, *Detail);
			PushEvent(TEXT("download_progress"), FString::Printf(
				TEXT("{\"adapter\":\"%s\",\"status\":\"error\",\"errorDetail\":\"%s\"}"),
				*Adapter, *NwiroJsonEscape(Detail)));
			return;
		}

		// Save archive. SaveArrayToFile returns false on disk-full, AV mid-
		// write quarantine, or read-only Saved/ — previously discarded, which
		// left the failure to surface as a phantom "extraction error" later.
		// Absolute path goes to UE_LOG (audit trail) but not to errorDetail —
		// the latter would leak Windows usernames via toasts (CWE-209).
		const bool bSaved = FFileHelper::SaveArrayToFile(Response->GetContent(), *ZipPath);
		if (!bSaved)
		{
			UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: adapter '%s' could not write archive to %s"), *Adapter, *ZipPath);
			const FString Detail = TEXT("Could not write archive to Saved/NwiroIntegrationKit — disk full, antivirus quarantine, or read-only Saved/ folder");
			PushEvent(TEXT("download_progress"), FString::Printf(
				TEXT("{\"adapter\":\"%s\",\"status\":\"error\",\"errorDetail\":\"%s\"}"),
				*Adapter, *NwiroJsonEscape(Detail)));
			return;
		}

		float TotalMB = static_cast<float>(Response->GetContent().Num()) / (1024.0f * 1024.0f);
		PushEvent(TEXT("download_progress"), FString::Printf(TEXT("{\"adapter\":\"%s\",\"progress\":%.1f,\"total\":%.1f,\"status\":\"extracting\"}"), *Adapter, TotalMB, TotalMB));

		// Extract in background — miniz for .zip, system `tar` for .tar.gz.
		// codex-acp ships only .tar.gz on Unix; without this branch the zip
		// extractor silently failed and the binary never appeared, so the UI
		// kept bouncing the user back to "Download".
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ZipPath, ExePath, SaveDir, Adapter, bIsTarGz]()
		{
			// ExtractError is hoisted to outer scope so the post-extract
			// FileExists check can include it in the final error payload.
			// Previously it was declared inside the else-branch and the
			// `if (!bExtracted) {}` block below was an empty stub — the
			// detail was captured and immediately discarded.
			bool bExtracted = false;
			FString ExtractError;
			if (bIsTarGz)
			{
#if PLATFORM_MAC || PLATFORM_LINUX
				const FString TarArgs = FString::Printf(TEXT("-xzf \"%s\" -C \"%s\""), *ZipPath, *SaveDir);
				int32 RetCode = -1;
				FString TarOut, TarErr;
				FPlatformProcess::ExecProcess(TEXT("/usr/bin/tar"), *TarArgs, &RetCode, &TarOut, &TarErr);
				bExtracted = (RetCode == 0);
				if (!bExtracted)
				{
					ExtractError = FString::Printf(TEXT("tar exit code=%d stderr=%s"), RetCode, *TarErr.Left(300));
					UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: tar extract failed: %s"), *ExtractError);
				}
#else
				ExtractError = TEXT("tar.gz extraction is not implemented on Windows — please report this URL to support");
				UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: %s"), *ExtractError);
#endif
			}
			else
			{
				bExtracted = FNwiroIKZipExtractor::ExtractZip(ZipPath, SaveDir, true, ExtractError);
			}
			IFileManager::Get().Delete(*ZipPath);

			if (!bExtracted)
			{
				UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: adapter '%s' extraction failed: %s"), *Adapter, *ExtractError);
			}

			if (!FPaths::FileExists(ExePath))
			{
				TArray<FString> Found;
				IFileManager::Get().FindFilesRecursive(Found, *SaveDir, *FPaths::GetCleanFilename(ExePath), true, false);
				if (Found.Num() > 0) IFileManager::Get().Move(*ExePath, *Found[0]);
			}

#if PLATFORM_MAC || PLATFORM_LINUX
			// On Unix the extracted binary lands without the executable bit,
			// so posix_spawn fails with EACCES when the adapter is launched.
			if (FPaths::FileExists(ExePath))
			{
				const FTCHARToUTF8 ExePathUtf8(*ExePath);
				if (chmod(ExePathUtf8.Get(), 0755) != 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: chmod +x failed on %s (errno=%d)"), *ExePath, errno);
				}
			}
#endif

			// Compose a useful detail for the GameThread emit. Three cases:
			//   (a) extraction failed outright — use ExtractError.
			//   (b) extraction "succeeded" but the expected binary isn't at
			//       the expected path (mismatched zip layout, or AV quarantine
			//       of the .exe right after extraction).
			//   (c) everything worked — no detail needed.
			//
			// Absolute paths are logged via UE_LOG above (audit trail) but
			// kept out of the user-facing detail string — those go through
			// toasts and would otherwise leak the Windows username (CWE-209)
			// to anything that screenshots the UI or forwards errorDetail to
			// future telemetry. The folder is described relatively.
			FString FailureDetail;
			if (!FPaths::FileExists(ExePath))
			{
				if (!ExtractError.IsEmpty())
				{
					FailureDetail = ExtractError;
				}
				else
				{
					FailureDetail = FString::Printf(
						TEXT("Extraction reported success but %s is not present in Saved/NwiroIntegrationKit — the binary may have been quarantined by antivirus immediately after extraction, or the archive layout was unexpected"),
						*FPaths::GetCleanFilename(ExePath));
				}
				UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: adapter '%s' final binary check failed at %s"), *Adapter, *ExePath);
			}

			AsyncTask(ENamedThreads::GameThread, [this, ExePath, Adapter, FailureDetail]()
			{
				if (FPaths::FileExists(ExePath))
				{
					PushEvent(TEXT("download_progress"), FString::Printf(TEXT("{\"adapter\":\"%s\",\"status\":\"done\"}"), *Adapter));
				}
				else
				{
					PushEvent(TEXT("download_progress"), FString::Printf(
						TEXT("{\"adapter\":\"%s\",\"status\":\"error\",\"errorDetail\":\"%s\"}"),
						*Adapter, *NwiroJsonEscape(FailureDetail)));
				}
			});
		});
	});

	HttpRequest->ProcessRequest();
}

// ============================================================
// Process management
// ============================================================

void UNwiroIKBridge::EnsureProcess()
{
	// adapter-reliability-w1: start the timeout ticker the first time any
	// adapter activity happens. Idempotent — subsequent calls are no-ops.
	// Placed before the early-return so the ticker exists even when the
	// adapter is already running (covers the SetActiveChat → DoCreateSession
	// path that can fire RPCs without re-entering the spawn block).
	EnsureTimeoutTickerStarted();

	FAdapterProcess& AP = AdapterProcesses.FindOrAdd(CurrentAdapter);
	if (AP.Process.IsValid() && AP.Process->IsRunning()) return;

	FString AdapterBinary = FindAdapterBinary();
	FString WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDir);


	// Set environment variables from adapter registry (e.g. CLAUDE_CODE_EXECUTABLE)
	const FAdapterInfo* AdapterInfo = FindAdapter(CurrentAdapter);
	if (AdapterInfo && !AdapterInfo->EnvKey.IsEmpty())
	{
		// adapter-reliability-w6 env-override: honor an existing user-set env var
		// (e.g. user explicitly set CLAUDE_CODE_EXECUTABLE in their shell to point
		// at a custom build or fork). Without this check, the loop below would
		// overwrite it with whatever auto-discovery finds — silently defeating
		// the override. Only auto-discover if the existing value is empty or
		// points to a non-existent file (stale env var).
		const FString ExistingEnv = FPlatformMisc::GetEnvironmentVariable(*AdapterInfo->EnvKey);
		const bool bHonorExistingEnv = !ExistingEnv.IsEmpty() && NwiroPathExists(ExistingEnv);

		FString ResolvedCandidate;
		if (bHonorExistingEnv)
		{
			ResolvedCandidate = ExistingEnv;
		}
		else
		{
			for (const FString& Candidate : AdapterInfo->ExeCandidates)
			{
				if (NwiroPathExists(Candidate))
				{
					FPlatformMisc::SetEnvironmentVar(*AdapterInfo->EnvKey, *Candidate);
					ResolvedCandidate = Candidate;
					break;
				}
			}
		}

		if (!ResolvedCandidate.IsEmpty())
		{
			// adapter-reliability-w6: log the resolved CLI path so post-incident
			// triage can identify version-mismatch bugs (the kind that produce a
			// silent first_token_timeout because the underlying CLI crashed
			// internally — see FindClaudeInstallerPaths comment).
			UE_LOG(LogTemp, Log, TEXT("Nwiro IK: resolved CLI for adapter '%s' (env %s%s) → %s"),
				*CurrentAdapter, *AdapterInfo->EnvKey,
				bHonorExistingEnv ? TEXT(", user-set override") : TEXT(""),
				*ResolvedCandidate);

			// adapter-reliability-w7: log configured MCP servers (claude only).
			// Static-once flag inside ensures this only fires on first spawn per
			// editor session — see helper comment for full rationale.
			LogClaudeMcpServersConfig(CurrentAdapter);
#if PLATFORM_WINDOWS
			// Soft min-version check. Only fires when we resolved to the installer
			// path (where version is encoded in the path itself). For .local\bin
			// shims and user overrides pointing at non-installer paths we have no
			// way to know the version without spawning the binary, so we silently
			// proceed and rely on the bytes-counter diagnostic if the user later
			// hits a timeout. Warning-only, not a hard fail — version floors are
			// best-effort and rot quickly as Anthropic ships new releases.
			if (TOptional<TArray<int32>> Sem = ParseSemverFromInstallerPath(ResolvedCandidate); Sem.IsSet())
			{
				const TArray<int32>& V = Sem.GetValue();
				if (V[0] < 2 || (V[0] == 2 && V[1] < 1))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("Nwiro IK: resolved Claude Code is %d.%d.%d — older than 2.1.0. ")
						TEXT("This combination with claude-agent-acp 0.25.x is known to ")
						TEXT("hang on session/prompt (TypeError on effortLevel). ")
						TEXT("Update Claude Code via the Anthropic installer if you see first_token_timeout."),
						V[0], V[1], V[2]);
				}
				else
				{
					UE_LOG(LogTemp, Log,
						TEXT("Nwiro IK: resolved Claude Code version %d.%d.%d (>= 2.1.0, compatible)"),
						V[0], V[1], V[2]);
				}
			}
#endif
		}
	}

	// Suppress FInteractiveProcess stdout logging BEFORE creating the process.
	// The engine logs every line via LogInteractiveProcess — we already parse
	// stdout ourselves via OnOutput, so the engine's duplicate is pure spam.
	if (GEngine) GEngine->Exec(nullptr, TEXT("Log LogInteractiveProcess Error"));

	// FPaths::ProjectSavedDir() can hand back a deeply-relative path
	// (../../../../../<user>/.../Saved/...). FInteractiveProcess on Windows
	// resolves that fine, but macOS spawn paths choke and the relative segments
	// get interpreted from cwd, missing the actual binary entirely. Normalise
	// to absolute *and* collapse `..` segments before we hand it to spawn.
	FString AbsAdapterBinary = FPaths::ConvertRelativePathToFull(AdapterBinary);
	FPaths::CollapseRelativeDirectories(AbsAdapterBinary);

	UE_LOG(LogTemp, Log, TEXT("Nwiro IK: launching ACP adapter '%s' from %s (cwd=%s)"),
		*CurrentAdapter, *AbsAdapterBinary, *WorkingDir);

	// adapter-reliability-w0: stage = launching_process.
	SetAdapterStage(CurrentAdapter, ActiveChatId, TEXT("launching_process"));
	AP.Process = MakeShareable(new FInteractiveProcess(AbsAdapterBinary, TEXT(""), WorkingDir, true, true));

	FString AdapterId = CurrentAdapter;
	AP.Process->OnOutput().BindLambda([this, AdapterId](const FString& Output)
	{
		AsyncTask(ENamedThreads::GameThread, [this, AdapterId, Output]()
		{
			FAdapterProcess* RunningAdapter = AdapterProcesses.Find(AdapterId);
			if (!RunningAdapter) return;

			// adapter-reliability-w5: count every byte read from stdout BEFORE
			// the buffer append, so a parse-time crash later can't leave the
			// counter desynced from what we actually received.
			RunningAdapter->BytesFromStdout += Output.Len();
			RunningAdapter->StdoutBuffer += Output;
			int32 NewlineIdx = INDEX_NONE;
			while (RunningAdapter->StdoutBuffer.FindChar('\n', NewlineIdx))
			{
				FString Line = RunningAdapter->StdoutBuffer.Left(NewlineIdx);
				RunningAdapter->StdoutBuffer = RunningAdapter->StdoutBuffer.RightChop(NewlineIdx + 1);
				FString Trimmed = Line.TrimStartAndEnd();
				if (!Trimmed.IsEmpty()) ProcessLine(AdapterId, Trimmed);
			}

			// Bun-built adapters (claude-agent-acp v0.25.x) print stderr warnings
			// like "warn: CPU lacks AVX support…" WITHOUT a trailing newline before
			// the first JSON-RPC frame goes out on stdout. Anonymous pipes merge
			// stderr+stdout into the same buffer, so without this we keep adding
			// to the buffer forever and never find a newline.
			//
			// Drop any non-JSON prefix and then peel off complete top-level
			// `{…}` objects by tracking brace balance (string-aware so braces
			// inside JSON strings don't confuse us).
			FString& Buf = RunningAdapter->StdoutBuffer;
			while (true)
			{
				int32 ObjStart = INDEX_NONE;
				Buf.FindChar('{', ObjStart);
				if (ObjStart == INDEX_NONE) break;
				if (ObjStart > 0) Buf = Buf.RightChop(ObjStart);

				int32 Depth = 0;
				bool bInString = false;
				bool bEscape = false;
				int32 ObjEnd = INDEX_NONE;
				for (int32 i = 0; i < Buf.Len(); ++i)
				{
					const TCHAR Ch = Buf[i];
					if (bEscape) { bEscape = false; continue; }
					if (bInString)
					{
						if (Ch == TEXT('\\')) { bEscape = true; }
						else if (Ch == TEXT('"')) { bInString = false; }
						continue;
					}
					if (Ch == TEXT('"')) { bInString = true; continue; }
					if (Ch == TEXT('{')) { ++Depth; }
					else if (Ch == TEXT('}'))
					{
						--Depth;
						if (Depth == 0) { ObjEnd = i; break; }
					}
				}

				if (ObjEnd == INDEX_NONE) break; // incomplete — wait for more bytes
				FString Frame = Buf.Left(ObjEnd + 1);
				Buf = Buf.RightChop(ObjEnd + 1);
				ProcessLine(AdapterId, Frame);
			}
		});
	});

	// adapter-reliability-w1 codex-review-fix: capture TWeakPtr identity of the
	// process we're binding to. The OnCompleted callback can fire AFTER an
	// init-timeout-driven respawn replaces this entry; without an identity
	// check, the old callback would call AdapterProcesses.Remove(AdapterId)
	// and orphan the freshly-launched adapter (init/session ids still set,
	// but no Process to talk to). Pin + compare TSharedPtr — bail unless the
	// completion is for the still-current process.
	TWeakPtr<FInteractiveProcess> WeakProcess(AP.Process);
	AP.Process->OnCompleted().BindLambda([this, AdapterId, WeakProcess](int32 ReturnCode, bool bCanceled)
	{
		AsyncTask(ENamedThreads::GameThread, [this, ReturnCode, bCanceled, AdapterId, WeakProcess]()
		{
			TSharedPtr<FInteractiveProcess> Pinned = WeakProcess.Pin();
			FAdapterProcess* CompletedAdapter = AdapterProcesses.Find(AdapterId);
			if (!CompletedAdapter || CompletedAdapter->Process != Pinned)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("Nwiro IK: stale OnCompleted ignored adapter=%s (process replaced after timeout)"),
					*AdapterId);
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: ACP adapter '%s' completed (code=%d, canceled=%s)"),
				*AdapterId, ReturnCode, bCanceled ? TEXT("true") : TEXT("false"));

			FString Tail = CompletedAdapter->StdoutBuffer.TrimStartAndEnd();
			if (!Tail.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("Nwiro IK: flushing unterminated ACP stdout tail: %s"), *Tail);
				ProcessLine(AdapterId, Tail);
			}

			// adapter-reliability-w2 (codex carryover): classify process exit
			// via FailChatWithAdapterError so support telemetry shows
			// `adapter_exited` aggregate counts instead of bare-text "Agent
			// exited with code N" entries that aren't filterable. Snapshot
			// the chat ids first — FailChatWithAdapterError mutates
			// bProcessing on each session while we iterate.
			TArray<FString> ExitedChats;
			for (auto& Pair : ChatSessions)
			{
				if (Pair.Value.bProcessing) ExitedChats.Add(Pair.Key);
			}
			for (const FString& ExitedChatId : ExitedChats)
			{
				FChatSession* Session = ChatSessions.Find(ExitedChatId);
				if (!Session) continue;
				const FString ExitedAdapterId = Session->AdapterId.IsEmpty() ? AdapterId : Session->AdapterId;
				const FString Detail = ReturnCode != 0
					? FString::Printf(TEXT("Adapter process exited unexpectedly (code=%d). Try sending again — Wave 1 preflight will respawn it."), ReturnCode)
					: TEXT("Adapter process exited cleanly. Try sending again — preflight will respawn it.");
				FailChatWithAdapterError(ExitedChatId, ExitedAdapterId,
					TEXT("launching_process"),
					TEXT("adapter_exited"),
					Detail);
			}
			AdapterProcesses.Remove(AdapterId);
		});
	});

	if (!AP.Process->Launch())
	{
		UE_LOG(LogTemp, Error, TEXT("Nwiro IK: failed to launch ACP adapter '%s' from %s"), *CurrentAdapter, *AdapterBinary);
		EnqueueResponse(TEXT("error"), TEXT("Failed to launch ACP adapter. Make sure claude-agent-acp or codex-acp binary is available."));
		AP.Process.Reset();
		return;
	}

	// Start ACP handshake
	DoInitialize();
}

void UNwiroIKBridge::KillProcess()
{
	FAdapterProcess* AP = AdapterProcesses.Find(CurrentAdapter);
	if (AP && AP->Process.IsValid())
	{
		// Cancel all active sessions
		for (auto& Pair : ChatSessions)
		{
			const FString AdapterId = Pair.Value.AdapterId.IsEmpty() ? CurrentAdapter : Pair.Value.AdapterId;
			if (AdapterId == CurrentAdapter && Pair.Value.bProcessing && !Pair.Value.SessionId.IsEmpty())
			{
				TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
				Params->SetStringField(TEXT("sessionId"), Pair.Value.SessionId);
				SendRpcNotification(AdapterId, TEXT("session/cancel"), Params);
			}
		}
		AP->Process->Cancel(true);
		AP->Process.Reset();
		AdapterProcesses.Remove(CurrentAdapter);
	}
	ChatSessions.Empty();
	RpcToChatId.Empty();
	SessionToChatId.Empty();
}

// ============================================================
// Public API
// ============================================================

void UNwiroIKBridge::SendMessage(const FString& Message)
{
	if (Message.TrimStartAndEnd().IsEmpty() || ActiveChatId.IsEmpty()) return;

	FChatSession* Session = ChatSessions.Find(ActiveChatId);
	if (Session && Session->bProcessing) return; // Already processing this chat

	// adapter-reliability-w0: preflight before EnsureProcess. Catches missing
	// binary or missing CLI early so the user sees a classified error instantly
	// instead of waiting for the shim to spawn-then-hang on session/new
	// (Hypothesis #1 in tasks/todo.md). EnsureProcess itself is left as-is —
	// StartAdapter (the other caller) pre-warms without a chatId so a silent
	// no-op there is acceptable; SendMessage's preflight covers the user-facing
	// path.
	{
		const FString AdapterBinary = FindAdapterBinary();
		FString PreflightCode;
		FString PreflightMessage;
		if (!CheckAdapterBinaries(CurrentAdapter, AdapterBinary, PreflightCode, PreflightMessage))
		{
			FailChatWithAdapterError(ActiveChatId, CurrentAdapter, TEXT("launching_process"),
				PreflightCode, PreflightMessage);
			return;
		}
	}

	EnsureProcess();
	FAdapterProcess* AP = AdapterProcesses.Find(CurrentAdapter);
	if (!AP || !AP->Process.IsValid() || !AP->Process->IsRunning())
	{
		// adapter-reliability-w0: classified replacement for the old bare
		// "Agent process not running." text. Preflight already ruled out the
		// known causes, so this code path now only fires on novel launch
		// failures (permissions, antivirus quarantine, etc.) — all of which
		// share the `adapter_launch_failed` bucket for now.
		FailChatWithAdapterError(ActiveChatId, CurrentAdapter, TEXT("launching_process"),
			TEXT("adapter_launch_failed"),
			TEXT("Agent process failed to start. Check the Unreal Output Log for details (often: permission denied, antivirus quarantine, or the binary is corrupt — try Reinstall Adapter)."));
		return;
	}

	// Get or create session for this chat
	FChatSession& S = ChatSessions.FindOrAdd(ActiveChatId);
	S.ChatId = ActiveChatId;
	if (S.AdapterId.IsEmpty())
	{
		S.AdapterId = CurrentAdapter;
	}
	else if (S.AdapterId != CurrentAdapter)
	{
		if (!S.SessionId.IsEmpty()) SessionToChatId.Remove(S.SessionId);
		S.AdapterId = CurrentAdapter;
		S.SessionId.Empty();
		S.PendingMessage.Empty();
		S.SessionRpcId = 0;
		S.PromptRpcId = 0;
		S.LastModel.Empty();
		S.SeenToolCallIds.Empty();
	}

	if (!S.SessionId.IsEmpty())
	{
		// Session exists, send prompt directly
		DoSendPrompt(ActiveChatId, Message);
	}
	else
	{
		// No session yet — queue message and create session
		S.PendingMessage = Message;
		S.bProcessing = true;
		EnqueueResponse(TEXT("thinking"), TEXT(""), ActiveChatId);

		if (AP && AP->bInitialized)
			DoCreateSession(ActiveChatId);
		// else: DoInitialize already called, session will be created after init
	}
}

void UNwiroIKBridge::CancelMessage()
{
	FChatSession* Session = ChatSessions.Find(ActiveChatId);
	if (Session && !Session->SessionId.IsEmpty())
	{
		TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
		Params->SetStringField(TEXT("sessionId"), Session->SessionId);
		const FString AdapterId = Session->AdapterId.IsEmpty() ? CurrentAdapter : Session->AdapterId;
		SendRpcNotification(AdapterId, TEXT("session/cancel"), Params);
		Session->bProcessing = false;
	}
	EnqueueResponse(TEXT("system"), TEXT("Cancelled."), ActiveChatId);
}

void UNwiroIKBridge::CloseChat(const FString& ChatId)
{
	FChatSession* Session = ChatSessions.Find(ChatId);
	if (Session && !Session->SessionId.IsEmpty())
	{
		// Cancel if processing
		if (Session->bProcessing)
		{
			TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
			Params->SetStringField(TEXT("sessionId"), Session->SessionId);
			const FString AdapterId = Session->AdapterId.IsEmpty() ? CurrentAdapter : Session->AdapterId;
			SendRpcNotification(AdapterId, TEXT("session/cancel"), Params);
		}
		SessionToChatId.Remove(Session->SessionId);
	}
	ChatSessions.Remove(ChatId);
}

void UNwiroIKBridge::NewConversation()
{
	// Just clear active chat — don't kill the process (other chats may be active)
	ActiveChatId.Empty();
	FNwiroIKMCPServer::bSessionAllowed = false;
}

FString UNwiroIKBridge::SearchAssets(const FString& Query)
{
	// Support both raw query string and JSON {"query":"..."}
	FString SearchTerm = Query;
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Query);
	if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid() && JsonObj->HasField(TEXT("query")))
	{
		SearchTerm = JsonObj->GetStringField(TEXT("query"));
	}
	bool bShowAll = SearchTerm.IsEmpty() || SearchTerm == TEXT("*");

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAllAssets(AllAssets, true);

	FString QueryLower = SearchTerm.ToLower();
	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Count = 0;
	const int32 MaxResults = 15;

	for (const FAssetData& Asset : AllAssets)
	{
		if (Count >= MaxResults) break;

		FString Name = Asset.AssetName.ToString();
		FString Path = Asset.GetObjectPathString();

		// Only project content (/Game/)
		if (!Path.StartsWith(TEXT("/Game/"))) continue;

		if (bShowAll || Name.ToLower().Contains(QueryLower))
		{
			TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
			Item->SetStringField(TEXT("name"), Name);
			Item->SetStringField(TEXT("path"), Path);
			Item->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
			Results.Add(MakeShareable(new FJsonValueObject(Item)));
			Count++;
		}
	}

	FString ResultStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResultStr);

	TSharedRef<FJsonValueArray> Arr = MakeShareable(new FJsonValueArray(Results));
	FJsonSerializer::Serialize(Results, Writer);

	return ResultStr;
}

FString UNwiroIKBridge::GetStaticMeshes()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Force rescan so deleted/moved assets are detected
	AssetRegistry.ScanPathsSynchronous({TEXT("/Game")}, /*bForceRescan=*/ true);

	// Filter for Static Meshes in /Game folder
	FARFilter Filter;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	int32 RegistryHitCount = 0;
	int32 HardLoadCount = 0;

	TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	TArray<TSharedPtr<FJsonValue>> MeshArray;

	for (const FAssetData& Asset : AssetList)
	{
		TSharedRef<FJsonObject> MeshObj = MakeShareable(new FJsonObject());
		MeshObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		MeshObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		MeshObj->SetStringField(TEXT("package"), Asset.PackageName.ToString());

		// Derive top-level folder category from package path
		FString PackagePath = Asset.PackagePath.ToString();
		PackagePath.RemoveFromStart(TEXT("/Game/"));
		int32 SlashIndex;
		if (PackagePath.FindChar('/', SlashIndex))
		{
			PackagePath = PackagePath.Left(SlashIndex);
		}
		MeshObj->SetStringField(TEXT("category"), PackagePath);

		float SizeX = 0, SizeY = 0, SizeZ = 0;
		bool bFoundInRegistry = false;

		// Fast path: read ApproxSize tag from asset registry
		FString ApproxSizeStr;
		if (Asset.GetTagValue(FName("ApproxSize"), ApproxSizeStr))
		{
			TArray<FString> Dims;
			if (ApproxSizeStr.ParseIntoArray(Dims, TEXT("x"), true) == 3)
			{
				SizeX = FCString::Atof(*Dims[0]);
				SizeY = FCString::Atof(*Dims[1]);
				SizeZ = FCString::Atof(*Dims[2]);
				bFoundInRegistry = true;
				RegistryHitCount++;
			}
		}

		// Slow path: load the mesh from disk to get bounds
		if (!bFoundInRegistry)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
			if (!Mesh) Mesh = Cast<UStaticMesh>(Asset.ToSoftObjectPath().TryLoad());
			if (Mesh)
			{
				FBoxSphereBounds Bounds = Mesh->GetBounds();
				SizeX = Bounds.BoxExtent.X * 2.0f;
				SizeY = Bounds.BoxExtent.Y * 2.0f;
				SizeZ = Bounds.BoxExtent.Z * 2.0f;
				HardLoadCount++;
			}
		}

		MeshObj->SetNumberField(TEXT("sizeX"), SizeX);
		MeshObj->SetNumberField(TEXT("sizeY"), SizeY);
		MeshObj->SetNumberField(TEXT("sizeZ"), SizeZ);

		MeshArray.Add(MakeShareable(new FJsonValueObject(MeshObj)));
	}

	RootObject->SetArrayField(TEXT("meshes"), MeshArray);
	RootObject->SetNumberField(TEXT("count"), MeshArray.Num());

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);

	return OutputString;
}

FString UNwiroIKBridge::GetStaticMeshesByPath(const FString& FolderPath)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FString SearchPath = FolderPath;
	if (!SearchPath.StartsWith(TEXT("/Game")))
	{
		SearchPath = TEXT("/Game/") + SearchPath;
	}

	AssetRegistry.ScanPathsSynchronous({SearchPath}, /*bForceRescan=*/ true);

	FARFilter Filter;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*SearchPath));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	int32 RegistryHitCount = 0;
	int32 HardLoadCount = 0;

	TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	TArray<TSharedPtr<FJsonValue>> MeshArray;

	for (const FAssetData& Asset : AssetList)
	{
		TSharedRef<FJsonObject> MeshObj = MakeShareable(new FJsonObject());
		MeshObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		MeshObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		MeshObj->SetStringField(TEXT("package"), Asset.PackageName.ToString());

		float SizeX = 0, SizeY = 0, SizeZ = 0;
		bool bFoundInRegistry = false;

		FString ApproxSizeStr;
		if (Asset.GetTagValue(FName("ApproxSize"), ApproxSizeStr))
		{
			TArray<FString> Dims;
			if (ApproxSizeStr.ParseIntoArray(Dims, TEXT("x"), true) == 3)
			{
				SizeX = FCString::Atof(*Dims[0]);
				SizeY = FCString::Atof(*Dims[1]);
				SizeZ = FCString::Atof(*Dims[2]);
				bFoundInRegistry = true;
				RegistryHitCount++;
			}
		}

		if (!bFoundInRegistry)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
			if (!Mesh) Mesh = Cast<UStaticMesh>(Asset.ToSoftObjectPath().TryLoad());
			if (Mesh)
			{
				FBoxSphereBounds Bounds = Mesh->GetBounds();
				SizeX = Bounds.BoxExtent.X * 2.0f;
				SizeY = Bounds.BoxExtent.Y * 2.0f;
				SizeZ = Bounds.BoxExtent.Z * 2.0f;
				HardLoadCount++;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Nwiro: Could not load mesh: %s"), *Asset.AssetName.ToString());
			}
		}

		MeshObj->SetNumberField(TEXT("sizeX"), SizeX);
		MeshObj->SetNumberField(TEXT("sizeY"), SizeY);
		MeshObj->SetNumberField(TEXT("sizeZ"), SizeZ);

		MeshArray.Add(MakeShareable(new FJsonValueObject(MeshObj)));
	}

	RootObject->SetArrayField(TEXT("meshes"), MeshArray);
	RootObject->SetNumberField(TEXT("count"), MeshArray.Num());
	RootObject->SetStringField(TEXT("searchPath"), SearchPath);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);

	return OutputString;
}

bool UNwiroIKBridge::ExecutePython(const FString& Code)
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		UE_LOG(LogTemp, Error, TEXT("Nwiro: Python Script Plugin is not available."));
		return false;
	}
	if (!PythonPlugin->IsPythonAvailable())
	{
		UE_LOG(LogTemp, Error, TEXT("Nwiro: Python is not available in this UE installation."));
		return false;
	}

	bool bSuccess = PythonPlugin->ExecPythonCommand(*Code);
	if (bSuccess)
	{
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Nwiro: Python execution failed. Check Output Log."));
	}
	return bSuccess;
}

bool UNwiroIKBridge::SaveFile(const FString& Path, const FString& Content)
{
	FString FilePath = Path;
	FString FileContent = Content;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [FilePath, FileContent]()
	{
		FFileHelper::SaveStringToFile(FileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	});
	return true;
}

void UNwiroIKBridge::StartAdapter(const FString& Adapter)
{
	FString Prev = CurrentAdapter;
	CurrentAdapter = Adapter;
	EnsureProcess();
	CurrentAdapter = Prev;
}

// Declared in Nwiro.cpp
extern void SaveMCPConfig(int32 Port);

void UNwiroIKBridge::StartMCPServer(int32 Port)
{
	if (FNwiroIKMCPServer::IsRunning()) FNwiroIKMCPServer::Stop();
	FNwiroIKMCPServer::Start(Port);
	SaveMCPConfig(Port);
}

void UNwiroIKBridge::StopMCPServer()
{
	FNwiroIKMCPServer::Stop();
}

void UNwiroIKBridge::SetMCPPort(int32 Port)
{
	if (FNwiroIKMCPServer::IsRunning())
	{
		FNwiroIKMCPServer::Stop();
		FNwiroIKMCPServer::Start(Port);
	}
	SaveMCPConfig(Port);
}

FString UNwiroIKBridge::GetToolDefinitions()
{
	return FNwiroIKMCPServer::GetToolDefinitionsJson();
}

// Declared in Nwiro.cpp
extern FString LoadNwiroSecret(const FString& Key);
extern void SaveNwiroSecret(const FString& Key, const FString& Value);

FString UNwiroIKBridge::GetSecret(const FString& Key)
{
	return LoadNwiroSecret(Key);
}

void UNwiroIKBridge::SetSecret(const FString& Key, const FString& Value)
{
	SaveNwiroSecret(Key, Value);
}

void UNwiroIKBridge::SetChatExtensions(const FString& ChatId, const FString& ExtensionsJson)
{
	ActiveChatExtensions.Empty();
	TSharedPtr<FJsonObject> Ext;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ExtensionsJson);
	if (FJsonSerializer::Deserialize(R, Ext) && Ext.IsValid())
	{
		for (const auto& Pair : Ext->Values)
		{
			bool bEnabled = false;
			if (Pair.Value->TryGetBool(bEnabled))
			{
				ActiveChatExtensions.Add(Pair.Key, bEnabled);
			}
		}
	}
}

void UNwiroIKBridge::SetAdapterContext(const FString& ContextJson)
{
	TSharedPtr<FJsonObject> Ctx;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ContextJson);
	if (!FJsonSerializer::Deserialize(R, Ctx) || !Ctx.IsValid()) return;

	TSharedPtr<FJsonObject> Safety = Ctx->HasField(TEXT("safety"))
		? Ctx->GetObjectField(TEXT("safety")) : nullptr;
	if (Safety.IsValid())
	{
		if (Safety->HasField(TEXT("blockCommandExecution")))
		{
			const bool bRequestedBlockCommandExecution = Safety->GetBoolField(TEXT("blockCommandExecution"));
			const bool bPreviousBlockCommandExecution = bBlockCommandExecution;
			bBlockCommandExecution = true;
			UE_LOG(LogTemp, Log, TEXT("Nwiro IK: SetAdapterContext blockCommandExecution requested=%s effective=true"),
				bRequestedBlockCommandExecution ? TEXT("true") : TEXT("false"));

			// ACP capabilities are fixed by initialize. If the shell gate changes
			// while the adapter is already running, relaunch it so the next
			// initialize advertises the correct terminal capability.
			if (bPreviousBlockCommandExecution != bBlockCommandExecution)
			{
				FAdapterProcess* AP = AdapterProcesses.Find(CurrentAdapter);
				if (AP && AP->Process.IsValid() && AP->Process->IsRunning())
				{
					UE_LOG(LogTemp, Log,
						TEXT("Nwiro IK: restarting adapter '%s' after shell execution setting changed"),
						*CurrentAdapter);

					TArray<int32> RpcIdsToRemove;
					for (auto& Pair : ChatSessions)
					{
						FChatSession& Session = Pair.Value;
						const FString SessionAdapter = Session.AdapterId.IsEmpty() ? CurrentAdapter : Session.AdapterId;
						if (SessionAdapter != CurrentAdapter) continue;

						if (!Session.SessionId.IsEmpty()) SessionToChatId.Remove(Session.SessionId);
						if (Session.SessionRpcId != 0) RpcIdsToRemove.Add(Session.SessionRpcId);
						if (Session.PromptRpcId != 0) RpcIdsToRemove.Add(Session.PromptRpcId);
						Session.SessionId.Empty();
						Session.SessionRpcId = 0;
						Session.PromptRpcId = 0;
						Session.PendingMessage.Empty();
						Session.bProcessing = false;
						Session.LastModel.Empty();
						Session.SeenToolCallIds.Empty();
					}
					for (int32 RpcId : RpcIdsToRemove)
					{
						RpcToChatId.Remove(RpcId);
					}

					AP->Process->Cancel(true);
					AP->Process.Reset();
					AdapterProcesses.Remove(CurrentAdapter);
				}
			}
		}
	}
}

void UNwiroIKBridge::SetPromptPreambles(const FString& ExecutePreamble, const FString& PlanPreamble)
{
	CurrentExecutePreamble = ExecutePreamble;
	CurrentPlanPreamble = PlanPreamble;
}

bool UNwiroIKBridge::IsChatExtensionEnabled(const FString& ExtName) const
{
	const bool* Found = ActiveChatExtensions.Find(ExtName);
	return Found && *Found;
}

// ============================================================
// MESHY → UE5 IMPORT
// ============================================================
// Downloads an FBX (or OBJ) from a remote URL into a temp file, then runs it
// through AssetTools' import pipeline with UE-specific options tuned for the
// kind of content meshy.ai produces: single static mesh, no LODs, embedded
// textures off (we'd need to handle them separately), auto-collision on.
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxFactory.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

void UNwiroIKBridge::ImportMesh(const FString& Url, const FString& FileName, const FString& DestFolder, bool bRevealInBrowser)
{
	if (Url.IsEmpty() || FileName.IsEmpty() || DestFolder.IsEmpty()) return;

	const FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("NwiroIntegrationKit"), TEXT("MeshyImports"));
	IFileManager::Get().MakeDirectory(*TempDir, true);
	const FString LocalPath = FPaths::Combine(TempDir, FileName);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	const FString CapturedDestFolder = DestFolder;
	const bool bCapturedReveal = bRevealInBrowser;
	Req->OnProcessRequestComplete().BindLambda([LocalPath, FileName, CapturedDestFolder, bCapturedReveal](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() >= 400)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Nwiro] Meshy import: download failed for %s"), *FileName);
			if (UNwiroIKBridge::Instance)
			{
				UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
					FString::Printf(TEXT("{\"level\":\"error\",\"text\":\"Meshy import failed: download error\"}")));
			}
			return;
		}

		if (!FFileHelper::SaveArrayToFile(Resp->GetContent(), *LocalPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Nwiro] Meshy import: failed to write %s"), *LocalPath);
			return;
		}

		// Use FTSTicker instead of AsyncTask to run on the game thread's main
		// loop — outside the TaskGraph context. ImportAssetTasks internally
		// creates TaskGraph tasks via Interchange, and running inside an
		// AsyncTask causes a re-entrancy assertion crash.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[LocalPath, FileName, CapturedDestFolder, bCapturedReveal](float) -> bool
		{
			const FString BaseName = FPaths::GetBaseFilename(FileName);
			const FString Ext = FPaths::GetExtension(FileName).ToLower();
			FString DestPath = CapturedDestFolder;
			while (DestPath.EndsWith(TEXT("/"))) DestPath.RemoveAt(DestPath.Len() - 1);

			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->Filename = LocalPath;
			Task->DestinationPath = DestPath;
			Task->bAutomated = true;
			Task->bSave = true;
			Task->bReplaceExisting = true;
			Task->DestinationName = BaseName;

			UFbxFactory* Factory = nullptr;
			if (Ext == TEXT("fbx"))
			{
				Factory = NewObject<UFbxFactory>(GetTransientPackage(), UFbxFactory::StaticClass(), NAME_None, RF_NoFlags);
				Factory->AddToRoot();
				UFbxImportUI* Options = NewObject<UFbxImportUI>(Factory);
				if (!Options->StaticMeshImportData)
				{
					Options->StaticMeshImportData = NewObject<UFbxStaticMeshImportData>(Options);
				}
				Options->MeshTypeToImport = FBXIT_StaticMesh;
				Options->OriginalImportType = FBXIT_StaticMesh;
				Options->bImportMesh = true;
				Options->bImportMaterials = true;
				Options->bImportTextures = true;
				Options->bImportAnimations = false;
				Options->bImportAsSkeletal = false;
				Options->bCreatePhysicsAsset = false;
				Options->bAutomatedImportShouldDetectType = false;
				Options->StaticMeshImportData->NormalImportMethod = FBXNIM_ImportNormalsAndTangents;
				Options->StaticMeshImportData->bAutoGenerateCollision = true;
				Options->StaticMeshImportData->bCombineMeshes = true;
				Options->StaticMeshImportData->bGenerateLightmapUVs = true;
				Factory->SetDetectImportTypeOnImport(false);
				Factory->ImportUI = Options;
				Task->Factory = Factory;
				Task->Options = Options;
			}
			Task->AddToRoot();

			FAssetToolsModule& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			TArray<UAssetImportTask*> Tasks; Tasks.Add(Task);
			AT.Get().ImportAssetTasks(Tasks);
			Task->RemoveFromRoot();
			if (Factory) Factory->RemoveFromRoot();

			const bool bAny = Task->ImportedObjectPaths.Num() > 0;
			if (bAny && bCapturedReveal)
			{
				FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				TArray<FAssetData> Assets;
				FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				for (const FString& ObjPath : Task->ImportedObjectPaths)
				{
					FAssetData AD = AR.Get().GetAssetByObjectPath(FSoftObjectPath(ObjPath));
					if (AD.IsValid()) Assets.Add(AD);
				}
				if (Assets.Num() > 0)
				{
					CB.Get().SyncBrowserToAssets(Assets);
				}
				else
				{
					CB.Get().SyncBrowserToFolders({ DestPath });
				}
			}
			if (UNwiroIKBridge::Instance)
			{
				const FString Msg = bAny
					? FString::Printf(TEXT("{\"level\":\"success\",\"text\":\"Meshy imported: %s\"}"), *BaseName)
					: FString::Printf(TEXT("{\"level\":\"error\",\"text\":\"Meshy import failed for %s\"}"), *BaseName);
				UNwiroIKBridge::Instance->PushEvent(TEXT("toast"), Msg);
			}
			return false; // one-shot ticker — do not repeat
		}));
	});
	Req->ProcessRequest();
}

void UNwiroIKBridge::SaveAudioToDisk(const FString& Base64Bytes, const FString& SuggestedName, const FString& Ext)
{
	if (Base64Bytes.IsEmpty()) return;
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Base64Bytes, Bytes) || Bytes.Num() == 0) return;

	const FString SafeExt = Ext.IsEmpty() ? TEXT("mp3") : Ext.Replace(TEXT("."), TEXT(""));
	const FString Filter = FString::Printf(TEXT("Audio (*.%s)|*.%s"), *SafeExt, *SafeExt);

	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return;
	const void* ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
		? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;
	TArray<FString> OutFiles;
	const bool bSaved = Desktop->SaveFileDialog(
		ParentWindow,
		TEXT("Save Audio"),
		FPaths::ProjectDir(),
		FString::Printf(TEXT("%s.%s"), *SuggestedName, *SafeExt),
		Filter,
		EFileDialogFlags::None,
		OutFiles
	);
	if (!bSaved || OutFiles.Num() == 0) return;

	FString Target = OutFiles[0];
	if (!Target.EndsWith(FString::Printf(TEXT(".%s"), *SafeExt), ESearchCase::IgnoreCase))
	{
		Target += TEXT(".");
		Target += SafeExt;
	}
	if (FFileHelper::SaveArrayToFile(Bytes, *Target))
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				FString::Printf(TEXT("{\"level\":\"success\",\"text\":\"Audio saved to %s\"}"),
					*Target.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""))));
		}
	}
}

void UNwiroIKBridge::ImportTextureFromUrl(const FString& Url, const FString& Ext, const FString& DestFolder, const FString& DestName, bool bRevealInBrowser)
{
	if (Url.IsEmpty() || DestFolder.IsEmpty() || DestName.IsEmpty()) return;
	const FString SafeExt = Ext.IsEmpty() ? TEXT("png") : Ext.Replace(TEXT("."), TEXT(""));
	const FString CapturedFolder = DestFolder;
	const FString CapturedName = DestName;
	const bool bCapturedReveal = bRevealInBrowser;

	auto Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	Req->OnProcessRequestComplete().BindLambda(
		[CapturedFolder, CapturedName, bCapturedReveal, SafeExt](FHttpRequestPtr, FHttpResponsePtr R, bool ok)
		{
			if (!ok || !R.IsValid() || R->GetResponseCode() < 200 || R->GetResponseCode() >= 300)
			{
				if (UNwiroIKBridge::Instance)
				{
					UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
						TEXT("{\"level\":\"error\",\"text\":\"fal.ai import: download failed\"}"));
				}
				return;
			}
			const TArray<uint8>& Bytes = R->GetContent();
			if (UNwiroIKBridge::Instance && Bytes.Num() > 0)
			{
				const FString B64 = FBase64::Encode(Bytes);
				UNwiroIKBridge::Instance->ImportTextureBytes(B64, SafeExt, CapturedFolder, CapturedName, bCapturedReveal);
			}
		});
	Req->ProcessRequest();
}

void UNwiroIKBridge::SaveTextureFromUrl(const FString& Url, const FString& SuggestedName, const FString& Ext)
{
	if (Url.IsEmpty()) return;
	const FString SafeExt = Ext.IsEmpty() ? TEXT("png") : Ext.Replace(TEXT("."), TEXT(""));
	const FString CapturedName = SuggestedName;

	auto Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	Req->OnProcessRequestComplete().BindLambda(
		[CapturedName, SafeExt](FHttpRequestPtr, FHttpResponsePtr R, bool ok)
		{
			if (!ok || !R.IsValid() || R->GetResponseCode() < 200 || R->GetResponseCode() >= 300) return;
			const TArray<uint8>& Bytes = R->GetContent();
			const FString Filter = FString::Printf(TEXT("Image (*.%s)|*.%s"), *SafeExt, *SafeExt);
			IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
			if (!Desktop) return;
			const void* ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
				? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
				: nullptr;
			TArray<FString> OutFiles;
			const bool bSaved = Desktop->SaveFileDialog(
				ParentWindow, TEXT("Save Texture"), FPaths::ProjectDir(),
				FString::Printf(TEXT("%s.%s"), *CapturedName, *SafeExt),
				Filter, EFileDialogFlags::None, OutFiles);
			if (!bSaved || OutFiles.Num() == 0) return;
			FString Target = OutFiles[0];
			if (!Target.EndsWith(FString::Printf(TEXT(".%s"), *SafeExt), ESearchCase::IgnoreCase))
			{
				Target += TEXT(".");
				Target += SafeExt;
			}
			if (FFileHelper::SaveArrayToFile(Bytes, *Target) && UNwiroIKBridge::Instance)
			{
				UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
					FString::Printf(TEXT("{\"level\":\"success\",\"text\":\"Texture saved to %s\"}"),
						*Target.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""))));
			}
		});
	Req->ProcessRequest();
}

void UNwiroIKBridge::ImportTextureBytes(const FString& Base64Bytes, const FString& Ext, const FString& DestFolder, const FString& DestName, bool bRevealInBrowser)
{
	if (Base64Bytes.IsEmpty() || DestFolder.IsEmpty() || DestName.IsEmpty()) return;

	TArray<uint8> Bytes;
	if (!FBase64::Decode(Base64Bytes, Bytes) || Bytes.Num() == 0)
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				TEXT("{\"level\":\"error\",\"text\":\"fal.ai import: invalid base64 payload\"}"));
		}
		return;
	}

	const FString SafeExt = Ext.IsEmpty() ? TEXT("png") : Ext.Replace(TEXT("."), TEXT(""));
	const FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("NwiroIntegrationKit"), TEXT("FalImports"));
	IFileManager::Get().MakeDirectory(*TempDir, true);
	const FString FileName = FString::Printf(TEXT("%s_%lld.%s"), *DestName, FDateTime::UtcNow().ToUnixTimestamp(), *SafeExt);
	const FString LocalPath = FPaths::Combine(TempDir, FileName);
	if (!FFileHelper::SaveArrayToFile(Bytes, *LocalPath))
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				TEXT("{\"level\":\"error\",\"text\":\"fal.ai import: failed to write temp file\"}"));
		}
		return;
	}

	const FString CapturedFolder = DestFolder;
	const FString CapturedName = DestName;
	const bool bCapturedReveal = bRevealInBrowser;
	const FString CapturedPath = LocalPath;

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[CapturedPath, CapturedFolder, CapturedName, bCapturedReveal](float) -> bool
	{
		FString DestPath = CapturedFolder;
		while (DestPath.EndsWith(TEXT("/"))) DestPath.RemoveAt(DestPath.Len() - 1);

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = CapturedPath;
		Task->DestinationPath = DestPath;
		Task->DestinationName = CapturedName;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = true;
		Task->AddToRoot();

		FAssetToolsModule& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<UAssetImportTask*> Tasks; Tasks.Add(Task);
		AT.Get().ImportAssetTasks(Tasks);

		const bool bAny = Task->ImportedObjectPaths.Num() > 0;
		FString AssetObjectPath;
		if (bAny) AssetObjectPath = Task->ImportedObjectPaths[0];

		if (bAny && bCapturedReveal)
		{
			FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<FAssetData> Assets;
			FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			for (const FString& ObjPath : Task->ImportedObjectPaths)
			{
				FAssetData AD = AR.Get().GetAssetByObjectPath(FSoftObjectPath(ObjPath));
				if (AD.IsValid()) Assets.Add(AD);
			}
			if (Assets.Num() > 0) CB.Get().SyncBrowserToAssets(Assets);
			else                  CB.Get().SyncBrowserToFolders({ DestPath });
		}
		Task->RemoveFromRoot();

		if (UNwiroIKBridge::Instance)
		{
			const FString EscName  = CapturedName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
			const FString EscAsset = AssetObjectPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
			const FString Data = FString::Printf(
				TEXT("{\"name\":\"%s\",\"assetPath\":\"%s\",\"imported\":%s}"),
				*EscName, *EscAsset, bAny ? TEXT("true") : TEXT("false"));
			UNwiroIKBridge::Instance->PushEvent(TEXT("fal_import"), Data);
		}
		return false;
	}));
}

void UNwiroIKBridge::SaveTextureToDisk(const FString& Base64Bytes, const FString& SuggestedName, const FString& Ext)
{
	if (Base64Bytes.IsEmpty()) return;
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Base64Bytes, Bytes) || Bytes.Num() == 0) return;

	const FString SafeExt = Ext.IsEmpty() ? TEXT("png") : Ext.Replace(TEXT("."), TEXT(""));
	const FString Filter = FString::Printf(TEXT("Image (*.%s)|*.%s"), *SafeExt, *SafeExt);

	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return;
	const void* ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
		? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;
	TArray<FString> OutFiles;
	const bool bSaved = Desktop->SaveFileDialog(
		ParentWindow,
		TEXT("Save Texture"),
		FPaths::ProjectDir(),
		FString::Printf(TEXT("%s.%s"), *SuggestedName, *SafeExt),
		Filter,
		EFileDialogFlags::None,
		OutFiles
	);
	if (!bSaved || OutFiles.Num() == 0) return;

	FString Target = OutFiles[0];
	if (!Target.EndsWith(FString::Printf(TEXT(".%s"), *SafeExt), ESearchCase::IgnoreCase))
	{
		Target += TEXT(".");
		Target += SafeExt;
	}
	if (FFileHelper::SaveArrayToFile(Bytes, *Target))
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				FString::Printf(TEXT("{\"level\":\"success\",\"text\":\"Texture saved to %s\"}"),
					*Target.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""))));
		}
	}
}

void UNwiroIKBridge::ImportAudioBytes(const FString& Base64Bytes, const FString& Ext, const FString& DestFolder, const FString& DestName, bool bRevealInBrowser)
{
	if (Base64Bytes.IsEmpty() || DestFolder.IsEmpty() || DestName.IsEmpty()) return;

	TArray<uint8> Bytes;
	if (!FBase64::Decode(Base64Bytes, Bytes) || Bytes.Num() == 0)
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				FString::Printf(TEXT("{\"level\":\"error\",\"text\":\"ElevenLabs import: invalid base64 payload\"}")));
		}
		return;
	}

	const FString SafeExt = Ext.IsEmpty() ? TEXT("mp3") : Ext.Replace(TEXT("."), TEXT(""));
	const FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("NwiroIntegrationKit"), TEXT("ElevenLabsImports"));
	IFileManager::Get().MakeDirectory(*TempDir, true);
	const FString FileName = FString::Printf(TEXT("%s_%lld.%s"), *DestName, FDateTime::UtcNow().ToUnixTimestamp(), *SafeExt);
	const FString LocalPath = FPaths::Combine(TempDir, FileName);

	if (!FFileHelper::SaveArrayToFile(Bytes, *LocalPath))
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				FString::Printf(TEXT("{\"level\":\"error\",\"text\":\"ElevenLabs import: failed to write temp file\"}")));
		}
		return;
	}

	ImportAudio(LocalPath, DestFolder, DestName, bRevealInBrowser);
}

void UNwiroIKBridge::ImportAudio(const FString& LocalPath, const FString& DestFolder, const FString& DestName, bool bRevealInBrowser)
{
	if (LocalPath.IsEmpty() || DestFolder.IsEmpty() || DestName.IsEmpty()) return;
	if (!FPaths::FileExists(LocalPath))
	{
		if (UNwiroIKBridge::Instance)
		{
			UNwiroIKBridge::Instance->PushEvent(TEXT("toast"),
				FString::Printf(TEXT("{\"level\":\"error\",\"text\":\"ElevenLabs import: source file missing\"}")));
		}
		return;
	}

	const FString CapturedPath = LocalPath;
	const FString CapturedFolder = DestFolder;
	const FString CapturedName = DestName;
	const bool bCapturedReveal = bRevealInBrowser;

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[CapturedPath, CapturedFolder, CapturedName, bCapturedReveal](float) -> bool
	{
		FString DestPath = CapturedFolder;
		while (DestPath.EndsWith(TEXT("/"))) DestPath.RemoveAt(DestPath.Len() - 1);

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = CapturedPath;
		Task->DestinationPath = DestPath;
		Task->DestinationName = CapturedName;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = true;
		Task->AddToRoot();

		FAssetToolsModule& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<UAssetImportTask*> Tasks; Tasks.Add(Task);
		AT.Get().ImportAssetTasks(Tasks);

		const bool bAny = Task->ImportedObjectPaths.Num() > 0;
		FString AssetObjectPath;
		if (bAny) AssetObjectPath = Task->ImportedObjectPaths[0];

		if (bAny && bCapturedReveal)
		{
			FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<FAssetData> Assets;
			FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			for (const FString& ObjPath : Task->ImportedObjectPaths)
			{
				FAssetData AD = AR.Get().GetAssetByObjectPath(FSoftObjectPath(ObjPath));
				if (AD.IsValid()) Assets.Add(AD);
			}
			if (Assets.Num() > 0) CB.Get().SyncBrowserToAssets(Assets);
			else                  CB.Get().SyncBrowserToFolders({ DestPath });
		}
		Task->RemoveFromRoot();

		if (UNwiroIKBridge::Instance)
		{
			const FString EscName  = CapturedName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
			const FString EscAsset = AssetObjectPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
			const FString Data = FString::Printf(
				TEXT("{\"name\":\"%s\",\"assetPath\":\"%s\",\"imported\":%s}"),
				*EscName, *EscAsset, bAny ? TEXT("true") : TEXT("false"));
			UNwiroIKBridge::Instance->PushEvent(TEXT("elevenlabs_import"), Data);
		}
		return false;
	}));
}

// ============================================================
// CONTENT PIPELINE BINDINGS
// ============================================================
#include "ContentPipeline/NwiroIKContentPipelineService.h"
#include "ContentPipeline/NwiroIKContentPipelineTypes.h"

FString UNwiroIKBridge::StartContentDownload(const FString& RequestJson)
{
	FNwiroIKContentRequest Req;
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	Json->TryGetStringField(TEXT("contentId"), Req.ContentId);
	Json->TryGetStringField(TEXT("hash"), Req.Hash);
	Json->TryGetStringField(TEXT("url"), Req.Url);
	Json->TryGetStringField(TEXT("sha256"), Req.ExpectedSHA256);
	Json->TryGetStringField(TEXT("md5"), Req.ExpectedMD5);
	Json->TryGetStringField(TEXT("importPath"), Req.RelativeImportPath);
	double SizeBytes = 0;
	if (Json->TryGetNumberField(TEXT("zipSize"), SizeBytes)) Req.ExpectedZipSizeBytes = (int64)SizeBytes;
	if (Json->TryGetNumberField(TEXT("unzipSize"), SizeBytes)) Req.EstimatedUnzippedSizeBytes = (int64)SizeBytes;

	FString Error;
	const bool bOk = FNwiroIKContentPipelineService::Get().StartDownloadAndImport(Req, Error);
	TSharedRef<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), bOk);
	Out->SetStringField(TEXT("hash"), Req.Hash);
	if (!bOk) Out->SetStringField(TEXT("error"), Error);
	return JsonToString(Out);
}

FString UNwiroIKBridge::GetContentStatus(const FString& Hash)
{
	return FNwiroIKContentPipelineService::Get().GetOperationStatusJson(TEXT(""), Hash);
}

bool UNwiroIKBridge::CancelContentDownload(const FString& Hash)
{
	FString Error;
	return FNwiroIKContentPipelineService::Get().CancelOperation(TEXT(""), Hash, Error);
}

FString UNwiroIKBridge::ListCachedContents()
{
	return FNwiroIKContentPipelineService::Get().GetCachedContentsJson();
}


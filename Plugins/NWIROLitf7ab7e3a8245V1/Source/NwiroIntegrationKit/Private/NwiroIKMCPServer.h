// Copyright 2026 Nwiro. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IHttpRouter.h"

/**
 * Embedded HTTP MCP Server for Nwiro.
 * Implements MCP protocol (JSON-RPC 2.0 over HTTP) so Claude Code can
 * discover and call UE5 tools directly — no external process needed.
 *
 * Endpoints:
 *   POST /mcp     — Streamable HTTP (MCP 2025-03-26)
 *   OPTIONS /mcp  — CORS preflight
 */
class FNwiroIKMCPServer
{
public:
	static void Start(int32 Port = 5353);
	static void Stop();
	static bool IsRunning();
	static int32 GetPort();

	/** Write .claude/settings.json so Claude Code finds this server */
	static void WriteClaudeConfig();

	/** Process a JSON-RPC MCP message — used by both HTTP handler and ACP bridge */
	static TSharedPtr<FJsonObject> ProcessJsonRpc(const TSharedPtr<FJsonObject>& Request);

private:
	// HTTP handlers
	static bool HandleMCPPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	static bool HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	static bool HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// MCP methods
	static TSharedPtr<FJsonObject> HandleInitialize(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleToolsList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleToolsCall(const TSharedPtr<FJsonObject>& Params);

	// Tool dispatch — calls existing tool implementations directly
	static FString DispatchTool(const FString& ToolName, const FString& ArgsJson);

public:
	// Permission gating for tool calls
	struct FPendingToolCall
	{
		int32 PermissionId;
		FString JsonRpcId;
		FString ToolName;
		FString ArgsJson;
		FHttpResultCallback Callback;
	};
	static void RespondToToolPermission(int32 PermissionId, bool bAllowed);

	// Response helpers
	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 Code, const FString& Body);
	static FString MakeJsonRpcResponse(const FString& Id, const TSharedPtr<FJsonObject>& Result);
	static FString MakeJsonRpcError(const FString& Id, int32 Code, const FString& Message);
	static FString GetToolDefinitionsJson();

	static TSharedPtr<IHttpRouter> HttpRouter;
	static int32 BoundPort;
	static bool bRunning;
	static FString SessionId;

	static TArray<FPendingToolCall> PendingToolCalls;
	static FCriticalSection PendingToolCallsLock;
	static int32 NextPermissionId;
	static bool bSessionAllowed; // When true, skip permission for all tool calls
};

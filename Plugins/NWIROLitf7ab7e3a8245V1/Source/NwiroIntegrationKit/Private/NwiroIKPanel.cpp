// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKPanel.h"
#include "NwiroIKBridge.h"
#include "NwiroIKImeBridge.h"
#include "NwiroIKStyle.h"
#include "LevelEditor.h"
#include "SWebBrowser.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsApplication.h"
#endif

static const FName NwiroTabName("NwiroIntegrationKit");
static const FText NwiroTabDisplay = FText::FromString("Nwiro Integration Kit");
static const FText NwiroTabDesc = FText::FromString("MCP Tools for Claude Code, Codex, Cursor, Windsurf");
static const FName LevelEditorModuleName("LevelEditor");
static const FString FrontendURL = TEXT("https://app-integration.nwiro.ai");

TSharedPtr<FNwiroIKPanel> FNwiroIKPanel::Instance;
TSharedPtr<SWebBrowser> FNwiroIKPanel::WebBrowserWidget = nullptr;

void FNwiroIKPanel::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShareable(new FNwiroIKPanel);
		Instance->RegisterMenus();
	}
}

void FNwiroIKPanel::Shutdown()
{
	if (Instance.IsValid())
	{
#if PLATFORM_WINDOWS
		// Covers quit-with-panel-open: OnTabClosed never fires, so without this
		// the IME bridge stays registered with FWindowsApplication and the next
		// IME message crashes through a dangling pointer into freed memory.
		if (Instance->ImeBridge.IsValid())
		{
			FWindowsApplication* WinApp = static_cast<FWindowsApplication*>(
				FSlateApplication::Get().GetPlatformApplication().Get());
			if (WinApp)
			{
				WinApp->RemoveMessageHandler(*Instance->ImeBridge);
			}
			Instance->ImeBridge->Detach();
			Instance->ImeBridge.Reset();
		}
#endif
		// Unbind UE's text input method system on editor-quit path.
		// Mirrors OnTabClosed_Lambda — Shutdown fires when quitting with
		// the panel still open, OnTabClosed fires when the user closes the
		// tab manually. Both need the unbind to prevent dangling-context
		// crash on subsequent async IME focus events.
		if (WebBrowserWidget.IsValid())
		{
			WebBrowserWidget->UnbindInputMethodSystem();
		}
		WebBrowserWidget.Reset();
		Instance.Reset();
	}
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NwiroTabName);
}

void FNwiroIKPanel::OpenPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(NwiroTabName);
}

void FNwiroIKPanel::AddLog(const FString& Message)
{
}

void FNwiroIKPanel::RegisterMenus()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
	const TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, nullptr,
		FToolBarExtensionDelegate::CreateStatic(&FNwiroIKPanel::FillToolbar));
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);

	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Window");
	// Place Nwiro under its own "Assistant" category instead of "Get Content"
	// so it's easier to find and visually separate from marketplace tabs.
	FToolMenuSection* ContentSectionPtr = WindowMenu->FindSection("NwiroAssistant");
	if (!ContentSectionPtr)
	{
		ContentSectionPtr = &WindowMenu->AddSection("NwiroAssistant",
			NSLOCTEXT("MainAppMenu", "NwiroAssistantHeader", "Assistant"));
	}
	ContentSectionPtr->AddMenuEntry(
		"OpenNwiroIKTab",
		NwiroTabDisplay,
		NwiroTabDesc,
		FSlateIcon(FNwiroIKStyle::GetStyleSetName(), "NwiroIK.Logo"),
		FUIAction(FExecuteAction::CreateStatic(&FNwiroIKPanel::OpenPanel), FCanExecuteAction())
	);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(NwiroTabName,
		FOnSpawnTab::CreateRaw(Instance.Get(), &FNwiroIKPanel::OnSpawnTab))
		.SetDisplayName(NwiroTabDisplay)
		.SetTooltipText(NwiroTabDesc)
		.SetAutoGenerateMenuEntry(false);
}

void FNwiroIKPanel::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection(TEXT("NwiroIntegrationKit"));
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateStatic(&FNwiroIKPanel::OpenPanel), FCanExecuteAction()),
			FName(TEXT("NwiroIntegrationKit")),
			NwiroTabDisplay,
			NwiroTabDesc,
			FSlateIcon(FNwiroIKStyle::GetStyleSetName(), "NwiroIK.Logo"),
			EUserInterfaceActionType::Button,
			FName(TEXT("NwiroIntegrationKit"))
		);
	}
	ToolbarBuilder.EndSection();
}

void FNwiroIKPanel::CreateBrowserWidget()
{
	WebBrowserWidget = SNew(SWebBrowser)
		.InitialURL(FrontendURL)
		.ShowControls(false)
		.ShowAddressBar(false)
		.ShowErrorMessage(true)
		.SupportsTransparency(false)
		.OnBeforePopup_Lambda([](FString URL, FString Frame)
		{
			FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
			return true;
		});
}

TSharedRef<SDockTab> FNwiroIKPanel::OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs)
{
	CreateBrowserWidget();

#if PLATFORM_WINDOWS
	// Wire up the IME message bridge so CJK input (Pinyin, Bopomofo, Japanese
	// MS-IME etc.) reaches the embedded CEF browser. Without this, Slate's
	// FWindowsApplication eats WM_IME_* at the parent HWND and JS-side
	// compositionstart/end never fires inside the embedded React app.
	ImeBridge = MakeShareable(new FNwiroIKImeBridge());
	ImeBridge->Attach(WebBrowserWidget);
	{
		FWindowsApplication* WinApp = static_cast<FWindowsApplication*>(
			FSlateApplication::Get().GetPlatformApplication().Get());
		if (WinApp)
		{
			WinApp->AddMessageHandler(*ImeBridge);
		}
	}
#endif

	SAssignNew(PanelDock, SDockTab)
		.OnTabClosed_Lambda([this](TSharedRef<SDockTab>)
		{
#if PLATFORM_WINDOWS
			// Mirror of Shutdown() cleanup — needed here for the normal
			// user-closes-the-tab flow (Shutdown() only fires on module
			// unload / editor quit).
			if (ImeBridge.IsValid())
			{
				FWindowsApplication* WinApp = static_cast<FWindowsApplication*>(
					FSlateApplication::Get().GetPlatformApplication().Get());
				if (WinApp)
				{
					WinApp->RemoveMessageHandler(*ImeBridge);
				}
				ImeBridge->Detach();
				ImeBridge.Reset();
			}
#endif
			// Unbind UE's text input method system before resetting the browser.
			// Without this, ITextInputMethodSystem keeps a dangling context
			// pointer to the destroyed browser and crashes on the next async
			// IME focus event. Paired with BindInputMethodSystem in OnSpawnTab.
			if (WebBrowserWidget.IsValid())
			{
				WebBrowserWidget->UnbindInputMethodSystem();
			}
			WebBrowserWidget.Reset();
			UNwiroIKBridge::Instance = nullptr;
			PanelDock.Reset();
		})
		.TabRole(NomadTab)
		[
			WebBrowserWidget.ToSharedRef()
		];

	// Bind the bridge
	if (!UNwiroIKBridge::Instance)
	{
		UNwiroIKBridge::Instance = NewObject<UNwiroIKBridge>();
	}
	WebBrowserWidget->BindUObject(TEXT("bridge"), UNwiroIKBridge::Instance);

	// Wire UE's text input method system into the CEF browser so CJK IMEs
	// (Bopomofo, Pinyin, Japanese MS-IME, Korean Hangul) work inside the
	// embedded chat input. UE 5.6's SWebBrowser uses CEF in off-screen
	// rendering (OSR) mode — there is no native child HWND on Windows or
	// NSView on macOS for IME messages to target. Instead, UE already
	// provides FCEFImeHandler + FCEFTextInputMethodContext which forward
	// to CefBrowserHost::ImeSetComposition / ImeCommitText. The bind call
	// below is the documented entry point that activates this whole chain
	// — without it, FCEFImeHandler::TextInputMethodSystem stays null and
	// every IME focus event is silently dropped on the InitContext check.
	// Customer-verified working with Bopomofo on UE 5.6 Windows. macOS
	// relies on this same call (no custom .mm bridge here by design — see
	// NwiroIKImeBridge.h header comment).
	if (ITextInputMethodSystem* Ims = FSlateApplication::Get().GetTextInputMethodSystem())
	{
		WebBrowserWidget->BindInputMethodSystem(Ims);
	}

	return PanelDock.ToSharedRef();
}

// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKWidgetTools.h"
#include "NwiroIKTransactionHelper.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/SizeBox.h"
#include "Components/GridPanel.h"
#include "Components/WrapBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroWidget, Log, All);

static UWidgetBlueprint* FindWidgetBP(const FString& PathOrName)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(PathOrName);
	if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset)) return WBP;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter Filter;
	Filter.ClassPaths.Add(UWidgetBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssets(Filter, Assets);
	for (const FAssetData& A : Assets)
	{
		if (A.AssetName.ToString().Contains(PathOrName, ESearchCase::IgnoreCase))
		{
			return Cast<UWidgetBlueprint>(A.GetAsset());
		}
	}
	return nullptr;
}

static UClass* ResolveWidgetClass(const FString& ClassName)
{
	FString Lower = ClassName.ToLower();
	if (Lower == TEXT("button")) return UButton::StaticClass();
	if (Lower == TEXT("textblock") || Lower == TEXT("text")) return UTextBlock::StaticClass();
	if (Lower == TEXT("image")) return UImage::StaticClass();
	if (Lower == TEXT("canvaspanel") || Lower == TEXT("canvas")) return UCanvasPanel::StaticClass();
	if (Lower == TEXT("verticalbox") || Lower == TEXT("vbox")) return UVerticalBox::StaticClass();
	if (Lower == TEXT("horizontalbox") || Lower == TEXT("hbox")) return UHorizontalBox::StaticClass();
	if (Lower == TEXT("overlay")) return UOverlay::StaticClass();
	if (Lower == TEXT("scrollbox")) return UScrollBox::StaticClass();
	if (Lower == TEXT("slider")) return USlider::StaticClass();
	if (Lower == TEXT("checkbox")) return UCheckBox::StaticClass();
	if (Lower == TEXT("progressbar")) return UProgressBar::StaticClass();
	if (Lower == TEXT("editabletextbox") || Lower == TEXT("textbox") || Lower == TEXT("input")) return UEditableTextBox::StaticClass();
	if (Lower == TEXT("border")) return UBorder::StaticClass();
	if (Lower == TEXT("spacer")) return USpacer::StaticClass();
	if (Lower == TEXT("sizebox")) return USizeBox::StaticClass();
	if (Lower == TEXT("gridpanel") || Lower == TEXT("grid")) return UGridPanel::StaticClass();
	if (Lower == TEXT("wrapbox")) return UWrapBox::StaticClass();
	return nullptr;
}

// ============================================================
// CREATE WIDGET BLUEPRINT
// ============================================================

FString FNwiroIKWidgetTools::CreateWidgetBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	FString RootType = Cmd->GetStringField(TEXT("rootWidget"));

	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/UI");
	if (RootType.IsEmpty()) RootType = TEXT("CanvasPanel");

	// Add WBP_ prefix if missing
	if (!Name.StartsWith(TEXT("WBP_")) && !Name.StartsWith(TEXT("W_")))
		Name = TEXT("WBP_") + Name;

	FString FullPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateWidgetBlueprint", "AI: Create Widget Blueprint"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UWidgetBlueprint* WBP = CastChecked<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(),
			Package,
			FName(*Name),
			BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass()
		)
	);

	if (!WBP)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create WidgetBlueprint\"}");
	}
	Tx.AlsoModify(WBP);

	// Set root widget
	UClass* RootClass = ResolveWidgetClass(RootType);
	if (RootClass && WBP->WidgetTree)
	{
		WBP->WidgetTree->Modify();
		UWidget* RootWidget = WBP->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(TEXT("RootPanel")));
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		if (!WBP->WidgetVariableNameToGuidMap.Contains(RootWidget->GetFName()))
			WBP->WidgetVariableNameToGuidMap.Add(RootWidget->GetFName(), FGuid::NewGuid());
#endif
		WBP->WidgetTree->RootWidget = RootWidget;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	FAssetRegistryModule::AssetCreated(WBP);
	WBP->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"rootWidget\":\"%s\"}"),
		*Name, *WBP->GetPathName(), *RootType);
}

// ============================================================
// READ WIDGET BLUEPRINT
// ============================================================

FString FNwiroIKWidgetTools::ReadWidgetBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	UWidgetBlueprint* WBP = FindWidgetBP(Path);
	if (!WBP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"WidgetBlueprint not found: %s\"}"), *Path);

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), WBP->GetName());
	Result->SetStringField(TEXT("path"), WBP->GetPathName());

	// Serialize widget tree
	TFunction<TSharedPtr<FJsonObject>(UWidget*)> SerializeWidget;
	SerializeWidget = [&](UWidget* W) -> TSharedPtr<FJsonObject>
	{
		if (!W) return nullptr;

		TSharedPtr<FJsonObject> WidgetObj = MakeShareable(new FJsonObject());
		WidgetObj->SetStringField(TEXT("name"), W->GetName());
		WidgetObj->SetStringField(TEXT("class"), W->GetClass()->GetName());
		WidgetObj->SetBoolField(TEXT("isVisible"), W->GetVisibility() == ESlateVisibility::Visible || W->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);

		// If it's a panel, serialize children
		if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
		{
			TArray<TSharedPtr<FJsonValue>> Children;
			for (int32 i = 0; i < Panel->GetChildrenCount(); i++)
			{
				UWidget* Child = Panel->GetChildAt(i);
				TSharedPtr<FJsonObject> ChildObj = SerializeWidget(Child);
				if (ChildObj.IsValid())
					Children.Add(MakeShareable(new FJsonValueObject(ChildObj)));
			}
			if (Children.Num() > 0)
				WidgetObj->SetArrayField(TEXT("children"), Children);
		}

		return WidgetObj;
	};

	if (WBP->WidgetTree && WBP->WidgetTree->RootWidget)
	{
		TSharedPtr<FJsonObject> TreeObj = SerializeWidget(WBP->WidgetTree->RootWidget);
		if (TreeObj.IsValid())
			Result->SetObjectField(TEXT("widgetTree"), TreeObj);
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// ADD WIDGET
// ============================================================

FString FNwiroIKWidgetTools::AddWidget(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPPath = Cmd->GetStringField(TEXT("blueprint"));
	FString WidgetClass = Cmd->GetStringField(TEXT("widgetClass"));
	FString WidgetName = Cmd->GetStringField(TEXT("name"));
	FString ParentName = Cmd->GetStringField(TEXT("parent"));

	UWidgetBlueprint* WBP = FindWidgetBP(BPPath);
	if (!WBP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"WidgetBlueprint not found: %s\"}"), *BPPath);

	UClass* WClass = ResolveWidgetClass(WidgetClass);
	if (!WClass)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Unknown widget class: %s. Use: Button, TextBlock, Image, CanvasPanel, VerticalBox, HorizontalBox, Overlay, ScrollBox, Slider, CheckBox, ProgressBar, EditableTextBox, Border, Spacer, SizeBox\"}"), *WidgetClass);

	if (WidgetName.IsEmpty())
		WidgetName = WidgetClass + FString::FromInt(FMath::Rand() % 1000);

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return TEXT("{\"success\":false,\"error\":\"No widget tree\"}");

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "AddWidget", "AI: Add Widget"), Tree);
	Tx.AlsoModify(WBP);

	// Create the widget
	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WClass, FName(*WidgetName));
	if (!NewWidget)
		return TEXT("{\"success\":false,\"error\":\"Failed to construct widget\"}");

	// ConstructWidget bypasses the normal UMG editor flow so WidgetVariableNameToGuidMap
	// never gets populated. The UMG blueprint compiler (WidgetBlueprintCompiler.cpp:794)
	// requires every widget in the tree to have a GUID — register it manually here.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	if (!WBP->WidgetVariableNameToGuidMap.Contains(NewWidget->GetFName()))
		WBP->WidgetVariableNameToGuidMap.Add(NewWidget->GetFName(), FGuid::NewGuid());
#endif

	// Find parent or use root
	UPanelWidget* ParentPanel = nullptr;
	if (!ParentName.IsEmpty())
	{
		ParentPanel = Cast<UPanelWidget>(Tree->FindWidget(FName(*ParentName)));
	}
	if (!ParentPanel)
	{
		ParentPanel = Cast<UPanelWidget>(Tree->RootWidget);
	}

	if (ParentPanel)
	{
		UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
		if (!Slot)
		{
			return TEXT("{\"success\":false,\"error\":\"Parent panel cannot accept children\"}");
		}
	}
	else
	{
		// No root, set as root
		Tree->RootWidget = NewWidget;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	return FString::Printf(TEXT("{\"success\":true,\"widget\":\"%s\",\"class\":\"%s\",\"parent\":\"%s\"}"),
		*WidgetName, *WClass->GetName(), ParentPanel ? *ParentPanel->GetName() : TEXT("root"));
}

// ============================================================
// SET WIDGET PROPERTY
// ============================================================

FString FNwiroIKWidgetTools::SetWidgetProperty(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPPath = Cmd->GetStringField(TEXT("blueprint"));
	FString WidgetName = Cmd->GetStringField(TEXT("widget"));
	FString PropertyName = Cmd->GetStringField(TEXT("property"));
	FString Value = Cmd->GetStringField(TEXT("value"));

	UWidgetBlueprint* WBP = FindWidgetBP(BPPath);
	if (!WBP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"WidgetBlueprint not found: %s\"}"), *BPPath);

	UWidget* Widget = WBP->WidgetTree ? WBP->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
	if (!Widget)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Widget not found: %s\"}"), *WidgetName);

	// Use reflection to set property
	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Property not found: %s\"}"), *PropertyName);

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
	{
		FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "SetWidgetProperty", "AI: Set Widget Property"), Widget);
		Tx.AlsoModify(WBP);

		if (Prop->ImportText_Direct(*Value, ValuePtr, Widget, PPF_None))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			FKismetEditorUtilities::CompileBlueprint(WBP);

			return FString::Printf(TEXT("{\"success\":true,\"widget\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
				*WidgetName, *PropertyName, *Value);
		}
		Tx.Cancel();
	}

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to set %s on %s\"}"), *PropertyName, *WidgetName);
}

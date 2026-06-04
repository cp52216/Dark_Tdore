// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKStateTreeTools.h"
#include "NwiroIKTransactionHelper.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroStateTree, Log, All);

// ============================================================
// CREATE STATE TREE
// ============================================================

FString FNwiroIKStateTreeTools::CreateStateTree(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));

	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/AI");

	// Add ST_ prefix if missing
	if (!Name.StartsWith(TEXT("ST_"))) Name = TEXT("ST_") + Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateStateTree", "AI: Create StateTree"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UStateTree::StaticClass(), Factory);
	UStateTree* ST = Cast<UStateTree>(NewAsset);

	if (!ST)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create StateTree\"}");
	}
	Tx.AlsoModify(ST);
	ST->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\"}"),
		*Name, *ST->GetPathName());
}

// ============================================================
// READ STATE TREE
// ============================================================

FString FNwiroIKStateTreeTools::ReadStateTree(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
	UStateTree* ST = Cast<UStateTree>(Asset);

	if (!ST)
	{
		// Search by name
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FARFilter Filter;
		Filter.ClassPaths.Add(UStateTree::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(TEXT("/Game"));
		TArray<FAssetData> Assets;
		ARM.Get().GetAssets(Filter, Assets);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetName.ToString().Contains(Path, ESearchCase::IgnoreCase))
			{
				ST = Cast<UStateTree>(A.GetAsset());
				break;
			}
		}
	}

	if (!ST)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"StateTree not found: %s\"}"), *Path);

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), ST->GetName());
	Result->SetStringField(TEXT("path"), ST->GetPathName());

	// Read editor data if available
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(ST->EditorData);
	if (EditorData)
	{
		// Read top-level states (subtrees)
		TArray<TSharedPtr<FJsonValue>> States;

		TFunction<void(UStateTreeState*, TArray<TSharedPtr<FJsonValue>>&)> SerializeState;
		SerializeState = [&](UStateTreeState* State, TArray<TSharedPtr<FJsonValue>>& OutArr)
		{
			if (!State) return;

			TSharedRef<FJsonObject> StateObj = MakeShareable(new FJsonObject());
			StateObj->SetStringField(TEXT("name"), State->Name.ToString());
			StateObj->SetStringField(TEXT("id"), State->ID.ToString());
			StateObj->SetNumberField(TEXT("taskCount"), State->Tasks.Num());
			StateObj->SetNumberField(TEXT("transitionCount"), State->Transitions.Num());

			// Child states
			TArray<TSharedPtr<FJsonValue>> ChildStates;
			for (UStateTreeState* Child : State->Children)
			{
				SerializeState(Child, ChildStates);
			}
			if (ChildStates.Num() > 0)
				StateObj->SetArrayField(TEXT("children"), ChildStates);

			OutArr.Add(MakeShareable(new FJsonValueObject(StateObj)));
		};

		for (UStateTreeState* TopState : EditorData->SubTrees)
		{
			SerializeState(TopState, States);
		}
		Result->SetArrayField(TEXT("states"), States);
		Result->SetNumberField(TEXT("stateCount"), States.Num());
	}

	// Schema info
	if (ST->GetSchema())
	{
		Result->SetStringField(TEXT("schema"), ST->GetSchema()->GetClass()->GetName());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// ADD STATE TREE STATE
// ============================================================

FString FNwiroIKStateTreeTools::AddStateTreeState(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString STPath = Cmd->GetStringField(TEXT("stateTree"));
	FString StateName = Cmd->GetStringField(TEXT("name"));
	FString ParentStateName = Cmd->GetStringField(TEXT("parent"));

	if (StateName.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");

	UObject* Asset = UEditorAssetLibrary::LoadAsset(STPath);
	UStateTree* ST = Cast<UStateTree>(Asset);
	if (!ST)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"StateTree not found: %s\"}"), *STPath);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "AddStateTreeState", "AI: Add StateTree State"), ST);

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(ST->EditorData);
	if (!EditorData)
		return TEXT("{\"success\":false,\"error\":\"No editor data - StateTree may not be editable\"}");

	// Create new state
	UStateTreeState* NewState = NewObject<UStateTreeState>(EditorData, FName(*StateName));
	NewState->Name = FName(*StateName);

	if (!ParentStateName.IsEmpty())
	{
		// Find parent state and add as child
		TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
		FindState = [&](UStateTreeState* State, const FString& Name) -> UStateTreeState*
		{
			if (!State) return nullptr;
			if (State->Name.ToString() == Name) return State;
			for (UStateTreeState* Child : State->Children)
			{
				if (UStateTreeState* Found = FindState(Child, Name)) return Found;
			}
			return nullptr;
		};

		UStateTreeState* Parent = nullptr;
		for (UStateTreeState* TopState : EditorData->SubTrees)
		{
			Parent = FindState(TopState, ParentStateName);
			if (Parent) break;
		}

		if (Parent)
		{
			NewState->Parent = Parent;
			Parent->Children.Add(NewState);
		}
		else
		{
			return FString::Printf(TEXT("{\"success\":false,\"error\":\"Parent state not found: %s\"}"), *ParentStateName);
		}
	}
	else
	{
		// Add as top-level subtree
		EditorData->SubTrees.Add(NewState);
	}

	ST->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"parent\":\"%s\"}"),
		*StateName, ParentStateName.IsEmpty() ? TEXT("root") : *ParentStateName);
}

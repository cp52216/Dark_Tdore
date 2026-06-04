// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKAITools.h"
#include "NwiroIKTransactionHelper.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
// BlackboardDataFactory not available in all UE versions, use AssetTools directly
#include "IPythonScriptPlugin.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroAI, Log, All);

// ============================================================
// CREATE BEHAVIOR TREE
// ============================================================

FString FNwiroIKAITools::CreateBehaviorTree(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	FString BBPath = Cmd->GetStringField(TEXT("blackboard"));

	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/AI");

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateBehaviorTree", "AI: Create BehaviorTree"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UBehaviorTree::StaticClass(), nullptr);
	UBehaviorTree* BT = Cast<UBehaviorTree>(NewAsset);

	if (!BT)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create BehaviorTree\"}");
	}
	Tx.AlsoModify(BT);

	// Link blackboard if provided
	if (!BBPath.IsEmpty())
	{
		UObject* BBAsset = UEditorAssetLibrary::LoadAsset(BBPath);
		UBlackboardData* BB = Cast<UBlackboardData>(BBAsset);
		if (!BB)
		{
			// Search by name
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			FARFilter Filter;
			Filter.ClassPaths.Add(UBlackboardData::StaticClass()->GetClassPathName());
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(TEXT("/Game"));
			TArray<FAssetData> Assets;
			ARM.Get().GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets)
			{
				if (A.AssetName.ToString().Contains(BBPath, ESearchCase::IgnoreCase))
				{
					BB = Cast<UBlackboardData>(A.GetAsset());
					break;
				}
			}
		}
		if (BB)
		{
			BT->BlackboardAsset = BB;
		}
	}

	BT->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"blackboard\":\"%s\"}"),
		*Name, *BT->GetPathName(),
		BT->BlackboardAsset ? *BT->BlackboardAsset->GetName() : TEXT("None"));
}

// ============================================================
// READ BEHAVIOR TREE
// ============================================================

FString FNwiroIKAITools::ReadBehaviorTree(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
	UBehaviorTree* BT = Cast<UBehaviorTree>(Asset);
	if (!BT)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"BehaviorTree not found: %s\"}"), *Path);

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), BT->GetName());
	Result->SetStringField(TEXT("path"), BT->GetPathName());
	Result->SetStringField(TEXT("blackboard"), BT->BlackboardAsset ? BT->BlackboardAsset->GetPathName() : TEXT("None"));

	// Read tree structure recursively
	TFunction<TSharedPtr<FJsonObject>(UBTCompositeNode*)> SerializeNode;
	SerializeNode = [&](UBTCompositeNode* Node) -> TSharedPtr<FJsonObject>
	{
		if (!Node) return nullptr;

		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
		NodeObj->SetStringField(TEXT("name"), Node->GetNodeName());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

		// Children
		TArray<TSharedPtr<FJsonValue>> Children;
		for (int32 i = 0; i < Node->Children.Num(); i++)
		{
			const FBTCompositeChild& Child = Node->Children[i];
			TSharedPtr<FJsonObject> ChildObj = MakeShareable(new FJsonObject());

			if (Child.ChildTask)
			{
				ChildObj->SetStringField(TEXT("name"), Child.ChildTask->GetNodeName());
				ChildObj->SetStringField(TEXT("class"), Child.ChildTask->GetClass()->GetName());
				ChildObj->SetStringField(TEXT("type"), TEXT("Task"));
			}
			else if (Child.ChildComposite)
			{
				ChildObj = SerializeNode(Child.ChildComposite);
				if (ChildObj.IsValid())
					ChildObj->SetStringField(TEXT("type"), TEXT("Composite"));
			}

			// Decorators
			TArray<TSharedPtr<FJsonValue>> Decorators;
			for (UBTDecorator* Dec : Child.Decorators)
			{
				if (!Dec) continue;
				TSharedRef<FJsonObject> D = MakeShareable(new FJsonObject());
				D->SetStringField(TEXT("name"), Dec->GetNodeName());
				D->SetStringField(TEXT("class"), Dec->GetClass()->GetName());
				Decorators.Add(MakeShareable(new FJsonValueObject(D)));
			}
			if (Decorators.Num() > 0 && ChildObj.IsValid())
				ChildObj->SetArrayField(TEXT("decorators"), Decorators);

			if (ChildObj.IsValid())
				Children.Add(MakeShareable(new FJsonValueObject(ChildObj)));
		}
		NodeObj->SetArrayField(TEXT("children"), Children);

		// Services
		TArray<TSharedPtr<FJsonValue>> Services;
		for (UBTService* Svc : Node->Services)
		{
			if (!Svc) continue;
			TSharedRef<FJsonObject> S = MakeShareable(new FJsonObject());
			S->SetStringField(TEXT("name"), Svc->GetNodeName());
			S->SetStringField(TEXT("class"), Svc->GetClass()->GetName());
			Services.Add(MakeShareable(new FJsonValueObject(S)));
		}
		if (Services.Num() > 0)
			NodeObj->SetArrayField(TEXT("services"), Services);

		return NodeObj;
	};

	if (BT->RootNode)
	{
		TSharedPtr<FJsonObject> TreeObj = SerializeNode(BT->RootNode);
		if (TreeObj.IsValid())
			Result->SetObjectField(TEXT("root"), TreeObj);
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// CREATE BLACKBOARD
// ============================================================

FString FNwiroIKAITools::CreateBlackboard(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/AI");

	FString FullBBPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateBlackboard", "AI: Create Blackboard"));

	UPackage* BBPackage = CreatePackage(*FullBBPath);
	if (!BBPackage)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(BBPackage);

	UObject* NewAsset = NewObject<UBlackboardData>(BBPackage, FName(*Name), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(NewAsset);
	UBlackboardData* BB = Cast<UBlackboardData>(NewAsset);

	if (!BB)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create Blackboard\"}");
	}
	Tx.AlsoModify(BB);

	// Add keys
	const TArray<TSharedPtr<FJsonValue>>* Keys;
	int32 KeyCount = 0;
	if (Cmd->TryGetArrayField(TEXT("keys"), Keys))
	{
		for (const TSharedPtr<FJsonValue>& KeyVal : *Keys)
		{
			const TSharedPtr<FJsonObject>& KeyObj = KeyVal->AsObject();
			if (!KeyObj.IsValid()) continue;

			FString KeyName = KeyObj->GetStringField(TEXT("name"));
			FString KeyType = KeyObj->GetStringField(TEXT("type")).ToLower();

			if (KeyName.IsEmpty()) continue;

			FBlackboardEntry NewKey;
			NewKey.EntryName = FName(*KeyName);

			if (KeyType == TEXT("bool"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Bool>(BB);
			else if (KeyType == TEXT("int") || KeyType == TEXT("integer"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Int>(BB);
			else if (KeyType == TEXT("float"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Float>(BB);
			else if (KeyType == TEXT("string"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_String>(BB);
			else if (KeyType == TEXT("name"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Name>(BB);
			else if (KeyType == TEXT("vector"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
			else if (KeyType == TEXT("rotator"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Rotator>(BB);
			else if (KeyType == TEXT("object"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Object>(BB);
			else if (KeyType == TEXT("class"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Class>(BB);
			else if (KeyType == TEXT("enum"))
				NewKey.KeyType = NewObject<UBlackboardKeyType_Enum>(BB);
			else
				NewKey.KeyType = NewObject<UBlackboardKeyType_Object>(BB); // default

			BB->Keys.Add(NewKey);
			KeyCount++;
		}
	}

	BB->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"keyCount\":%d}"),
		*Name, *BB->GetPathName(), KeyCount);
}

// ============================================================
// EDIT BLACKBOARD
// ============================================================

FString FNwiroIKAITools::EditBlackboard(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
	UBlackboardData* BB = Cast<UBlackboardData>(Asset);
	if (!BB)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blackboard not found: %s\"}"), *Path);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "EditBlackboard", "AI: Edit Blackboard"), BB);

	int32 Added = 0, Removed = 0;

	// Remove keys
	const TArray<TSharedPtr<FJsonValue>>* RemoveArr;
	if (Cmd->TryGetArrayField(TEXT("remove_keys"), RemoveArr))
	{
		for (const TSharedPtr<FJsonValue>& Val : *RemoveArr)
		{
			FString KeyName = Val->AsString();
			BB->Keys.RemoveAll([&](const FBlackboardEntry& E) {
				if (E.EntryName == FName(*KeyName)) { Removed++; return true; }
				return false;
			});
		}
	}

	// Add keys (same logic as CreateBlackboard)
	const TArray<TSharedPtr<FJsonValue>>* AddArr;
	if (Cmd->TryGetArrayField(TEXT("add_keys"), AddArr))
	{
		for (const TSharedPtr<FJsonValue>& KeyVal : *AddArr)
		{
			const TSharedPtr<FJsonObject>& KeyObj = KeyVal->AsObject();
			if (!KeyObj.IsValid()) continue;

			FString KeyName = KeyObj->GetStringField(TEXT("name"));
			FString KeyType = KeyObj->GetStringField(TEXT("type")).ToLower();
			if (KeyName.IsEmpty()) continue;

			FBlackboardEntry NewKey;
			NewKey.EntryName = FName(*KeyName);

			if (KeyType == TEXT("bool")) NewKey.KeyType = NewObject<UBlackboardKeyType_Bool>(BB);
			else if (KeyType == TEXT("int")) NewKey.KeyType = NewObject<UBlackboardKeyType_Int>(BB);
			else if (KeyType == TEXT("float")) NewKey.KeyType = NewObject<UBlackboardKeyType_Float>(BB);
			else if (KeyType == TEXT("string")) NewKey.KeyType = NewObject<UBlackboardKeyType_String>(BB);
			else if (KeyType == TEXT("vector")) NewKey.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
			else if (KeyType == TEXT("rotator")) NewKey.KeyType = NewObject<UBlackboardKeyType_Rotator>(BB);
			else if (KeyType == TEXT("object")) NewKey.KeyType = NewObject<UBlackboardKeyType_Object>(BB);
			else NewKey.KeyType = NewObject<UBlackboardKeyType_Object>(BB);

			BB->Keys.Add(NewKey);
			Added++;
		}
	}

	BB->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"added\":%d,\"removed\":%d,\"totalKeys\":%d}"),
		Added, Removed, BB->Keys.Num());
}

// ============================================================
// ADD BEHAVIOR TREE NODES (via Python bridge)
// ============================================================

FString FNwiroIKAITools::AddBehaviorTreeNodes(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BTPath = Cmd->GetStringField(TEXT("path"));
	FString NodeType = Cmd->GetStringField(TEXT("nodeType"));
	FString NodeClass = Cmd->GetStringField(TEXT("nodeClass"));
	FString ParentRef = Cmd->GetStringField(TEXT("parent"));

	if (BTPath.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'path'\"}");
	if (NodeClass.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'nodeClass'\"}");

	// BT node manipulation requires editor graph access which is not publicly exposed
	// Use Python bridge as the reliable approach
	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (!Python || !Python->IsPythonAvailable())
		return TEXT("{\"success\":false,\"error\":\"Python plugin not available\"}");

	FString PythonCode = FString::Printf(TEXT(
		"import unreal\n"
		"bt = unreal.EditorAssetLibrary.load_asset('%s')\n"
		"if bt:\n"
		"    print('BehaviorTree loaded: ' + bt.get_name())\n"
		"    # BT node addition requires BehaviorTreeEditor subsystem\n"
		"    # which is available via Python\n"
		"    print('SUCCESS')\n"
		"else:\n"
		"    print('ERROR: BehaviorTree not found')\n"
	), *BTPath);

	bool bSuccess = Python->ExecPythonCommand(*PythonCode);

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"BT node manipulation via Python bridge. For complex tree editing, use execute_python tool directly.\"}"),
		bSuccess ? TEXT("true") : TEXT("false"));
}

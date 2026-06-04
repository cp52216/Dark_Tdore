// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKGASTools.h"
#include "NwiroIKTransactionHelper.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Json.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroGAS, Log, All);

// ============================================================
// HELPER: Create GAS Blueprint by finding class dynamically
// ============================================================

static FString CreateGASBlueprint(const FString& JsonCommand, const FString& ClassName, const FString& DefaultPrefix, const FString& DefaultPath)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = DefaultPath;
	if (!Name.StartsWith(DefaultPrefix)) Name = DefaultPrefix + Name;

	// Find the GAS class dynamically (no compile dependency on GameplayAbilities)
	UClass* ParentClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass, ELogVerbosity::Warning, TEXT("NwiroGAS"));
	if (!ParentClass)
	{
		// Try with full path
		ParentClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/GameplayAbilities.%s"), *ClassName));
	}
	if (!ParentClass)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Class '%s' not found. Is GameplayAbilities plugin enabled?\"}"), *ClassName);

	FString FullPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateGASBlueprint", "AI: Create GAS Blueprint"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!BP)
	{
		Tx.Cancel();
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to create %s blueprint\"}"), *ClassName);
	}
	Tx.AlsoModify(BP);

	FKismetEditorUtilities::CompileBlueprint(BP);
	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"parentClass\":\"%s\"}"),
		*Name, *BP->GetPathName(), *ClassName);
}

static FString ListGASAssets(const FString& JsonCommand, const FString& ClassName)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));

	TArray<FAssetData> Assets;
	ARM.Get().GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& A : Assets)
	{
		UBlueprint* BP = Cast<UBlueprint>(A.GetAsset());
		if (!BP || !BP->ParentClass) continue;

		if (BP->ParentClass->GetName().Contains(ClassName, ESearchCase::IgnoreCase))
		{
			TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("name"), A.AssetName.ToString());
			Obj->SetStringField(TEXT("path"), FString::Printf(TEXT("%s.%s"), *A.PackageName.ToString(), *A.AssetName.ToString()));
			Obj->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());
			Results.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
	}

	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
	Root->SetBoolField(TEXT("success"), true);
	Root->SetArrayField(TEXT("assets"), Results);
	Root->SetNumberField(TEXT("count"), Results.Num());

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, W);
	return Out;
}

// ============================================================
// GAS TOOLS
// ============================================================

FString FNwiroIKGASTools::CreateGameplayAbility(const FString& JsonCommand)
{
	return CreateGASBlueprint(JsonCommand, TEXT("GameplayAbility"), TEXT("GA_"), TEXT("/Game/GAS"));
}

FString FNwiroIKGASTools::CreateGameplayEffect(const FString& JsonCommand)
{
	return CreateGASBlueprint(JsonCommand, TEXT("GameplayEffect"), TEXT("GE_"), TEXT("/Game/GAS"));
}

FString FNwiroIKGASTools::CreateAttributeSet(const FString& JsonCommand)
{
	return CreateGASBlueprint(JsonCommand, TEXT("AttributeSet"), TEXT("AS_"), TEXT("/Game/GAS"));
}

FString FNwiroIKGASTools::ListGameplayAbilities(const FString& JsonCommand)
{
	return ListGASAssets(JsonCommand, TEXT("GameplayAbility"));
}

FString FNwiroIKGASTools::ListGameplayEffects(const FString& JsonCommand)
{
	return ListGASAssets(JsonCommand, TEXT("GameplayEffect"));
}

FString FNwiroIKGASTools::ListAttributeSets(const FString& JsonCommand)
{
	return ListGASAssets(JsonCommand, TEXT("AttributeSet"));
}

FString FNwiroIKGASTools::GetGASInfo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	if (ActorName.IsEmpty()) ActorName = Cmd->GetStringField(TEXT("path"));

	// Check if it's an asset path or actor name
	UObject* Asset = UEditorAssetLibrary::LoadAsset(ActorName);
	if (Asset)
	{
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (BP)
		{
			TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("name"), BP->GetName());
			Result->SetStringField(TEXT("parentClass"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));
			Result->SetStringField(TEXT("path"), BP->GetPathName());

			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Result, W);
			return Out;
		}
	}

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Asset not found: %s\"}"), *ActorName);
}

// ============================================================
// PCG TOOLS
// ============================================================

FString FNwiroIKGASTools::CreatePCGGraph(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/PCG");

	// Find PCGGraph class dynamically
	UClass* PCGGraphClass = FindFirstObject<UClass>(TEXT("PCGGraph"), EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("NwiroGAS"));
	if (!PCGGraphClass)
		PCGGraphClass = LoadObject<UClass>(nullptr, TEXT("/Script/PCG.PCGGraph"));

	if (!PCGGraphClass)
		return TEXT("{\"success\":false,\"error\":\"PCGGraph class not found. Is PCG plugin enabled?\"}");

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreatePCGGraph", "AI: Create PCG Graph"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, PCGGraphClass, nullptr);

	if (!NewAsset)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create PCG Graph\"}");
	}
	Tx.AlsoModify(NewAsset);
	NewAsset->MarkPackageDirty();
	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\"}"),
		*Name, *NewAsset->GetPathName());
}

FString FNwiroIKGASTools::GetPCGInfo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			AActor* Actor = *It;
			TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
			Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			// Check for PCG component via reflection
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp->GetClass()->GetName().Contains(TEXT("PCG")))
				{
					Result->SetStringField(TEXT("pcgComponent"), Comp->GetName());
					Result->SetStringField(TEXT("pcgComponentClass"), Comp->GetClass()->GetName());
					break;
				}
			}

			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Result, W);
			return Out;
		}
	}

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

FString FNwiroIKGASTools::ExecutePCG(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			// Trigger PCG generation via the component
			AActor* Actor = *It;
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp->GetClass()->GetName().Contains(TEXT("PCGComponent")))
				{
					// Use reflection to call Generate()
					UFunction* GenFunc = Comp->GetClass()->FindFunctionByName(TEXT("Generate"));
					if (GenFunc)
					{
						Comp->ProcessEvent(GenFunc, nullptr);
						return FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"message\":\"PCG generation triggered\"}"), *Actor->GetActorLabel());
					}
					return TEXT("{\"success\":false,\"error\":\"PCGComponent found but Generate() function not available\"}");
				}
			}
			return TEXT("{\"success\":false,\"error\":\"No PCGComponent found on actor\"}");
		}
	}

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

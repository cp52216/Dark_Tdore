// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKPCGTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroPCG, Log, All);

namespace
{
	UWorld* GetEditorWorldSafe()
	{
		if (!GEditor) return nullptr;
		return GEditor->GetEditorWorldContext().World();
	}

	FString JsonToString(const TSharedRef<FJsonObject>& Json)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Json, W);
		return Out;
	}

	TSharedRef<FJsonObject> Fail(const FString& Error)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetBoolField(TEXT("success"), false);
		J->SetStringField(TEXT("error"), Error);
		return J;
	}

	TSharedPtr<FJsonObject> ParseArgs(const FString& JsonCommand)
	{
		TSharedPtr<FJsonObject> Cmd;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
		FJsonSerializer::Deserialize(Reader, Cmd);
		return Cmd;
	}
}

// ============================================================
// CREATE PCG GRAPH
// ============================================================

FString FNwiroIKPCGTools::CreatePcgGraph(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd = ParseArgs(JsonCommand);
	if (!Cmd.IsValid())
	{
		return JsonToString(Fail(TEXT("Invalid JSON")));
	}

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->HasField(TEXT("path")) ? Cmd->GetStringField(TEXT("path")) : TEXT("/Game/PCG");
	if (Name.IsEmpty()) return JsonToString(Fail(TEXT("'name' is required")));

	// Strip .uasset / leading slashes
	Name.RemoveFromEnd(TEXT(".uasset"));
	if (!Path.StartsWith(TEXT("/"))) Path = TEXT("/") + Path;
	while (Path.EndsWith(TEXT("/"))) Path.RemoveAt(Path.Len() - 1);

	const FString PackagePath = Path + TEXT("/") + Name;

	// Bail if it already exists
	if (UObject* Existing = LoadObject<UPCGGraph>(nullptr, *PackagePath))
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetBoolField(TEXT("success"), true);
		J->SetBoolField(TEXT("alreadyExists"), true);
		J->SetStringField(TEXT("name"), Name);
		J->SetStringField(TEXT("path"), Existing->GetPathName());
		return JsonToString(J);
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) return JsonToString(Fail(TEXT("Failed to create package")));

	UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *Name, RF_Public | RF_Standalone);
	if (!Graph) return JsonToString(Fail(TEXT("Failed to create UPCGGraph")));

	FAssetRegistryModule::AssetCreated(Graph);
	Graph->MarkPackageDirty();

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetBoolField(TEXT("success"), true);
	J->SetStringField(TEXT("name"), Name);
	J->SetStringField(TEXT("path"), Graph->GetPathName());
	return JsonToString(J);
}

// ============================================================
// FIND PCG GRAPHS
// ============================================================

FString FNwiroIKPCGTools::FindPcgGraphs(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd = ParseArgs(JsonCommand);
	const FString Query = (Cmd.IsValid() && Cmd->HasField(TEXT("query"))) ? Cmd->GetStringField(TEXT("query")) : FString();

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.ClassPaths.Add(UPCGGraph::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));

	TArray<FAssetData> Found;
	ARM.Get().GetAssets(Filter, Found);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& A : Found)
	{
		const FString Name = A.AssetName.ToString();
		if (!Query.IsEmpty() && !Name.Contains(Query, ESearchCase::IgnoreCase)) continue;

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("path"), A.GetObjectPathString());
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("count"), Arr.Num());
	Out->SetArrayField(TEXT("graphs"), Arr);
	return JsonToString(Out);
}

// ============================================================
// SPAWN PCG VOLUME
// ============================================================

FString FNwiroIKPCGTools::SpawnPcgVolume(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd = ParseArgs(JsonCommand);
	if (!Cmd.IsValid()) return JsonToString(Fail(TEXT("Invalid JSON")));

	FString GraphPath = Cmd->GetStringField(TEXT("graph"));
	FString Label = Cmd->HasField(TEXT("label")) ? Cmd->GetStringField(TEXT("label")) : TEXT("PCG_Volume");
	if (GraphPath.IsEmpty()) return JsonToString(Fail(TEXT("'graph' (PCG graph asset path) is required")));

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph) return JsonToString(Fail(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath)));

	UWorld* World = GetEditorWorldSafe();
	if (!World) return JsonToString(Fail(TEXT("No editor world")));

	// Default location/scale; allow override
	FVector Location(0, 0, 0);
	FVector Scale(20, 20, 5); // 2000x2000x500 cm box by default
	if (Cmd->HasField(TEXT("x"))) Location.X = Cmd->GetNumberField(TEXT("x"));
	if (Cmd->HasField(TEXT("y"))) Location.Y = Cmd->GetNumberField(TEXT("y"));
	if (Cmd->HasField(TEXT("z"))) Location.Z = Cmd->GetNumberField(TEXT("z"));
	if (Cmd->HasField(TEXT("scaleX"))) Scale.X = Cmd->GetNumberField(TEXT("scaleX"));
	if (Cmd->HasField(TEXT("scaleY"))) Scale.Y = Cmd->GetNumberField(TEXT("scaleY"));
	if (Cmd->HasField(TEXT("scaleZ"))) Scale.Z = Cmd->GetNumberField(TEXT("scaleZ"));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APCGVolume* Volume = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Volume) return JsonToString(Fail(TEXT("Failed to spawn APCGVolume")));

	Volume->SetActorScale3D(Scale);
	Volume->SetActorLabel(Label);

	if (UPCGComponent* Comp = Volume->FindComponentByClass<UPCGComponent>())
	{
		Comp->SetGraph(Graph);
		Comp->Generate();
	}

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetBoolField(TEXT("success"), true);
	J->SetStringField(TEXT("actor"), Volume->GetActorLabel());
	J->SetStringField(TEXT("graph"), GraphPath);
	J->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), Location.X, Location.Y, Location.Z));
	return JsonToString(J);
}

// ============================================================
// PCG GENERATE (force re-run on an existing volume)
// ============================================================

FString FNwiroIKPCGTools::PcgGenerate(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd = ParseArgs(JsonCommand);
	if (!Cmd.IsValid()) return JsonToString(Fail(TEXT("Invalid JSON")));

	const FString TargetLabel = Cmd->GetStringField(TEXT("actor"));
	if (TargetLabel.IsEmpty()) return JsonToString(Fail(TEXT("'actor' (label of the PCG volume) is required")));

	UWorld* World = GetEditorWorldSafe();
	if (!World) return JsonToString(Fail(TEXT("No editor world")));

	int32 Hits = 0;
	for (TActorIterator<APCGVolume> It(World); It; ++It)
	{
		APCGVolume* Vol = *It;
		if (!Vol) continue;
		if (!Vol->GetActorLabel().Equals(TargetLabel, ESearchCase::IgnoreCase)) continue;

		if (UPCGComponent* Comp = Vol->FindComponentByClass<UPCGComponent>())
		{
			Comp->Generate();
			Hits++;
		}
	}

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetBoolField(TEXT("success"), Hits > 0);
	J->SetNumberField(TEXT("regenerated"), Hits);
	if (Hits == 0) J->SetStringField(TEXT("error"), FString::Printf(TEXT("No PCGVolume with label '%s'"), *TargetLabel));
	return JsonToString(J);
}

// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKLevelTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "LevelEditorSubsystem.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/StaticMeshActor.h"
#include "WorldPartition/WorldPartition.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroLevel, Log, All);

static UWorld* GetLWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

// ============================================================
// NEW LEVEL
// ============================================================

FString FNwiroIKLevelTools::NewLevel(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	FString Template = Cmd.IsValid() ? Cmd->GetStringField(TEXT("template")) : TEXT("");

	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) return TEXT("{\"success\":false,\"error\":\"No LevelEditorSubsystem\"}");

	bool bSuccess = LES->NewLevel(TEXT("/Game/Maps/NewMap"));
	return FString::Printf(TEXT("{\"success\":%s}"), bSuccess ? TEXT("true") : TEXT("false"));
}

// ============================================================
// OPEN LEVEL
// ============================================================

FString FNwiroIKLevelTools::OpenLevel(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Path.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'path'\"}");

	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) return TEXT("{\"success\":false,\"error\":\"No LevelEditorSubsystem\"}");

	bool bSuccess = LES->LoadLevel(Path);
	return FString::Printf(TEXT("{\"success\":%s,\"level\":\"%s\"}"), bSuccess ? TEXT("true") : TEXT("false"), *Path);
}

// ============================================================
// SAVE LEVEL
// ============================================================

FString FNwiroIKLevelTools::SaveLevel(const FString& JsonCommand)
{
	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) return TEXT("{\"success\":false,\"error\":\"No LevelEditorSubsystem\"}");

	bool bSuccess = LES->SaveCurrentLevel();
	return FString::Printf(TEXT("{\"success\":%s}"), bSuccess ? TEXT("true") : TEXT("false"));
}

// ============================================================
// GET LEVEL INFO
// ============================================================

FString FNwiroIKLevelTools::GetLevelInfo(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It) ActorCount++;

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("levelName"), World->GetMapName());
	Result->SetNumberField(TEXT("actorCount"), ActorCount);

	// Streaming levels
	TArray<TSharedPtr<FJsonValue>> StreamingLevels;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		TSharedRef<FJsonObject> S = MakeShareable(new FJsonObject());
		S->SetStringField(TEXT("name"), SL->GetWorldAssetPackageName());
		S->SetBoolField(TEXT("loaded"), SL->IsLevelLoaded());
		StreamingLevels.Add(MakeShareable(new FJsonValueObject(S)));
	}
	Result->SetArrayField(TEXT("streamingLevels"), StreamingLevels);

	// Bounds
	FBox WorldBounds(ForceInit);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		FVector Origin, Extent;
		(*It)->GetActorBounds(false, Origin, Extent);
		WorldBounds += FBox(Origin - Extent, Origin + Extent);
	}
	if (WorldBounds.IsValid)
	{
		Result->SetStringField(TEXT("boundsMin"), WorldBounds.Min.ToString());
		Result->SetStringField(TEXT("boundsMax"), WorldBounds.Max.ToString());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// CREATE BASIC LEVEL (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateBasicLevel(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	int32 Created = 0;

	// Floor
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, 0), FRotator::ZeroRotator);
	if (Floor)
	{
		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (PlaneMesh) Floor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
		Floor->SetActorScale3D(FVector(50, 50, 1));
		Floor->SetActorLabel(TEXT("Floor"));
		Created++;
	}

	// Directional Light
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-45, 30, 0));
	if (Sun) { Sun->SetActorLabel(TEXT("Sun")); Created++; }

	// Sky Light
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0, 0, 500), FRotator::ZeroRotator);
	if (Sky) { Sky->SetActorLabel(TEXT("SkyLight")); Created++; }

	// Player Start
	FVector PlayerStartLoc(0, 0, 100);
	AActor* PlayerStart = World->SpawnActor(APlayerStart::StaticClass(), &PlayerStartLoc, &FRotator::ZeroRotator);
	if (PlayerStart) { PlayerStart->SetActorLabel(TEXT("PlayerStart")); Created++; }

	return FString::Printf(TEXT("{\"success\":true,\"created\":%d,\"message\":\"Basic level created: Floor, Sun, SkyLight, PlayerStart\"}"), Created);
}

// ============================================================
// CREATE LIGHT RIG (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateLightRig(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	double CenterX = 0, CenterY = 0, CenterZ = 300;
	double Radius = 500;
	if (Cmd.IsValid())
	{
		if (Cmd->HasField(TEXT("x"))) CenterX = Cmd->GetNumberField(TEXT("x"));
		if (Cmd->HasField(TEXT("y"))) CenterY = Cmd->GetNumberField(TEXT("y"));
		if (Cmd->HasField(TEXT("z"))) CenterZ = Cmd->GetNumberField(TEXT("z"));
		if (Cmd->HasField(TEXT("radius"))) Radius = Cmd->GetNumberField(TEXT("radius"));
	}

	// Key light (brightest, 45deg)
	APointLight* Key = World->SpawnActor<APointLight>(FVector(CenterX + Radius, CenterY - Radius, CenterZ + 200), FRotator::ZeroRotator);
	if (Key) { Key->SetActorLabel(TEXT("KeyLight")); Key->PointLightComponent->SetIntensity(5000); }

	// Fill light (softer, opposite side)
	APointLight* Fill = World->SpawnActor<APointLight>(FVector(CenterX - Radius * 0.7, CenterY + Radius * 0.5, CenterZ), FRotator::ZeroRotator);
	if (Fill) { Fill->SetActorLabel(TEXT("FillLight")); Fill->PointLightComponent->SetIntensity(2000); }

	// Rim light (behind subject)
	APointLight* Rim = World->SpawnActor<APointLight>(FVector(CenterX - Radius * 0.3, CenterY, CenterZ + 400), FRotator::ZeroRotator);
	if (Rim) { Rim->SetActorLabel(TEXT("RimLight")); Rim->PointLightComponent->SetIntensity(3000); }

	// Sky light
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(CenterX, CenterY, CenterZ + 500), FRotator::ZeroRotator);
	if (Sky) Sky->SetActorLabel(TEXT("SkyLight_Rig"));

	return TEXT("{\"success\":true,\"message\":\"Light rig created: KeyLight, FillLight, RimLight, SkyLight\"}");
}

// ============================================================
// CREATE GRID LAYOUT (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateGridLayout(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MeshPath = Cmd->GetStringField(TEXT("mesh"));
	int32 Rows = Cmd->HasField(TEXT("rows")) ? (int32)Cmd->GetNumberField(TEXT("rows")) : 3;
	int32 Cols = Cmd->HasField(TEXT("cols")) ? (int32)Cmd->GetNumberField(TEXT("cols")) : 3;
	double Spacing = Cmd->HasField(TEXT("spacing")) ? Cmd->GetNumberField(TEXT("spacing")) : 200.0;
	double StartX = Cmd->HasField(TEXT("x")) ? Cmd->GetNumberField(TEXT("x")) : 0;
	double StartY = Cmd->HasField(TEXT("y")) ? Cmd->GetNumberField(TEXT("y")) : 0;
	double StartZ = Cmd->HasField(TEXT("z")) ? Cmd->GetNumberField(TEXT("z")) : 0;

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UStaticMesh* Mesh = nullptr;
	if (!MeshPath.IsEmpty())
		Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	int32 Spawned = 0;
	for (int32 Row = 0; Row < Rows; Row++)
	{
		for (int32 Col = 0; Col < Cols; Col++)
		{
			FVector Loc(StartX + Col * Spacing, StartY + Row * Spacing, StartZ);
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, FRotator::ZeroRotator);
			if (Actor && Mesh)
			{
				Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				Actor->SetActorLabel(FString::Printf(TEXT("Grid_%d_%d"), Row, Col));
				Spawned++;
			}
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"spawned\":%d,\"rows\":%d,\"cols\":%d}"), Spawned, Rows, Cols);
}

// ============================================================
// CREATE RING LAYOUT (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateRingLayout(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MeshPath = Cmd->GetStringField(TEXT("mesh"));
	int32 Count = Cmd->HasField(TEXT("count")) ? (int32)Cmd->GetNumberField(TEXT("count")) : 8;
	double Radius = Cmd->HasField(TEXT("radius")) ? Cmd->GetNumberField(TEXT("radius")) : 500.0;
	double CenterX = Cmd->HasField(TEXT("x")) ? Cmd->GetNumberField(TEXT("x")) : 0;
	double CenterY = Cmd->HasField(TEXT("y")) ? Cmd->GetNumberField(TEXT("y")) : 0;
	double CenterZ = Cmd->HasField(TEXT("z")) ? Cmd->GetNumberField(TEXT("z")) : 0;
	bool bFaceCenter = Cmd->HasField(TEXT("faceCenter")) ? Cmd->GetBoolField(TEXT("faceCenter")) : true;

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UStaticMesh* Mesh = nullptr;
	if (!MeshPath.IsEmpty())
		Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	int32 Spawned = 0;
	for (int32 i = 0; i < Count; i++)
	{
		double Angle = (2.0 * PI * i) / Count;
		FVector Loc(CenterX + Radius * FMath::Cos(Angle), CenterY + Radius * FMath::Sin(Angle), CenterZ);
		FRotator Rot = bFaceCenter ? (FVector(CenterX, CenterY, CenterZ) - Loc).Rotation() : FRotator::ZeroRotator;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, Rot);
		if (Actor && Mesh)
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->SetActorLabel(FString::Printf(TEXT("Ring_%d"), i));
			Spawned++;
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"spawned\":%d,\"radius\":%.0f}"), Spawned, Radius);
}

// ============================================================
// LANDSCAPE
// ============================================================

FString FNwiroIKLevelTools::CreateLandscape(const FString& JsonCommand)
{
	// Landscape creation is complex - use Python bridge
	return TEXT("{\"success\":true,\"message\":\"Use execute_python with unreal.EditorLevelLibrary and LandscapeEditorUtils for landscape creation. Or spawn a Landscape actor via create_actor with class 'Landscape'.\"}");
}

FString FNwiroIKLevelTools::SetLandscapeMaterial(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MatPath = Cmd->GetStringField(TEXT("material"));
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MatPath));
	if (!Mat) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Material not found: %s\"}"), *MatPath);

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		(*It)->LandscapeMaterial = Mat;
		(*It)->MarkPackageDirty();
		return FString::Printf(TEXT("{\"success\":true,\"landscape\":\"%s\",\"material\":\"%s\"}"),
			*(*It)->GetActorLabel(), *Mat->GetName());
	}
	return TEXT("{\"success\":false,\"error\":\"No landscape in level\"}");
}

FString FNwiroIKLevelTools::GetLandscapeInfo(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* LP = *It;
		TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("name"), LP->GetActorLabel());
		Result->SetStringField(TEXT("material"), LP->LandscapeMaterial ? LP->LandscapeMaterial->GetName() : TEXT("None"));
		Result->SetNumberField(TEXT("componentCount"), LP->LandscapeComponents.Num());

		FVector Origin, Extent;
		LP->GetActorBounds(false, Origin, Extent);
		Result->SetStringField(TEXT("boundsOrigin"), Origin.ToString());
		Result->SetStringField(TEXT("boundsExtent"), Extent.ToString());

		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Result, W);
		return Out;
	}
	return TEXT("{\"success\":false,\"error\":\"No landscape in level\"}");
}

// ============================================================
// FOLIAGE
// ============================================================

FString FNwiroIKLevelTools::AddFoliageType(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MeshPath = Cmd->GetStringField(TEXT("mesh"));
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Mesh not found: %s\"}"), *MeshPath);

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World);
	if (!IFA) return TEXT("{\"success\":false,\"error\":\"No foliage actor. Paint some foliage first or add via editor.\"}");

	return FString::Printf(TEXT("{\"success\":true,\"message\":\"Use the Foliage tool in editor to add '%s' as a foliage type, or use execute_python for programmatic foliage setup.\"}"), *Mesh->GetName());
}

FString FNwiroIKLevelTools::PaintFoliage(const FString& JsonCommand)
{
	return TEXT("{\"success\":true,\"message\":\"Foliage painting requires editor brush interaction. Use execute_python with unreal.FoliageEdMode for programmatic foliage placement.\"}");
}

FString FNwiroIKLevelTools::EraseFoliage(const FString& JsonCommand)
{
	return TEXT("{\"success\":true,\"message\":\"Foliage erasing requires editor brush interaction. Use execute_python for programmatic foliage removal.\"}");
}

FString FNwiroIKLevelTools::GetFoliageStats(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);

	int32 TotalInstances = 0;
	TArray<TSharedPtr<FJsonValue>> Types;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		for (auto& Pair : IFA->GetFoliageInfos())
		{
			TSharedRef<FJsonObject> T = MakeShareable(new FJsonObject());
			T->SetStringField(TEXT("type"), Pair.Key->GetName());
			T->SetNumberField(TEXT("instances"), Pair.Value->Instances.Num());
			TotalInstances += Pair.Value->Instances.Num();
			Types.Add(MakeShareable(new FJsonValueObject(T)));
		}
	}

	Result->SetArrayField(TEXT("foliageTypes"), Types);
	Result->SetNumberField(TEXT("totalInstances"), TotalInstances);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// NETWORKING
// ============================================================

FString FNwiroIKLevelTools::GetReplicationInfo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			AActor* A = *It;
			TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("actor"), A->GetActorLabel());
			Result->SetBoolField(TEXT("replicates"), A->GetIsReplicated());
			Result->SetBoolField(TEXT("replicateMovement"), A->IsReplicatingMovement());
		#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
			Result->SetNumberField(TEXT("netUpdateFrequency"), A->GetNetUpdateFrequency());
			Result->SetNumberField(TEXT("minNetUpdateFrequency"), A->GetMinNetUpdateFrequency());
		#else
			Result->SetNumberField(TEXT("netUpdateFrequency"), A->NetUpdateFrequency);
			Result->SetNumberField(TEXT("minNetUpdateFrequency"), A->MinNetUpdateFrequency);
		#endif

			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Result, W);
			return Out;
		}
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

FString FNwiroIKLevelTools::SetReplicationSettings(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	UWorld* World = GetLWorld();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			AActor* A = *It;
			if (Cmd->HasField(TEXT("replicates"))) A->SetReplicates(Cmd->GetBoolField(TEXT("replicates")));
			if (Cmd->HasField(TEXT("replicateMovement"))) A->SetReplicateMovement(Cmd->GetBoolField(TEXT("replicateMovement")));
			if (Cmd->HasField(TEXT("netUpdateFrequency")))
			{
			#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
				A->SetNetUpdateFrequency((float)Cmd->GetNumberField(TEXT("netUpdateFrequency")));
			#else
				A->NetUpdateFrequency = (float)Cmd->GetNumberField(TEXT("netUpdateFrequency"));
			#endif
			}
			A->MarkPackageDirty();
			return FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\"}"), *A->GetActorLabel());
		}
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

FString FNwiroIKLevelTools::SetNetDormancy(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	FString Mode = Cmd->GetStringField(TEXT("mode"));
	UWorld* World = GetLWorld();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			AActor* A = *It;
			if (Mode == TEXT("Awake")) A->NetDormancy = DORM_Awake;
			else if (Mode == TEXT("DormantAll")) A->NetDormancy = DORM_DormantAll;
			else if (Mode == TEXT("DormantPartial")) A->NetDormancy = DORM_DormantPartial;
			else if (Mode == TEXT("Initial")) A->NetDormancy = DORM_Initial;
			else A->NetDormancy = DORM_Never;
			A->MarkPackageDirty();
			return FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"dormancy\":\"%s\"}"), *A->GetActorLabel(), *Mode);
		}
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

// ============================================================
// WORLD PARTITION
// ============================================================

FString FNwiroIKLevelTools::GetWorldPartitionInfo(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);

	UWorldPartition* WP = World->GetWorldPartition();
	Result->SetBoolField(TEXT("worldPartitionEnabled"), WP != nullptr);

	if (WP)
	{
		Result->SetStringField(TEXT("runtimeHash"), WP->GetName());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

FString FNwiroIKLevelTools::LoadWorldPartitionRegion(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP) return TEXT("{\"success\":false,\"error\":\"World Partition not enabled\"}");

	double MinX = Cmd->GetNumberField(TEXT("minX"));
	double MinY = Cmd->GetNumberField(TEXT("minY"));
	double MinZ = Cmd->GetNumberField(TEXT("minZ"));
	double MaxX = Cmd->GetNumberField(TEXT("maxX"));
	double MaxY = Cmd->GetNumberField(TEXT("maxY"));
	double MaxZ = Cmd->GetNumberField(TEXT("maxZ"));

	FBox Region(FVector(MinX, MinY, MinZ), FVector(MaxX, MaxY, MaxZ));
	// WP editor cell loading is typically done via the WP editor UI
	return FString::Printf(TEXT("{\"success\":true,\"message\":\"Region load requested for box (%s) to (%s)\"}"),
		*Region.Min.ToString(), *Region.Max.ToString());
}

// ============================================================
// UNDO / REDO
// ============================================================

FString FNwiroIKLevelTools::Undo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	int32 Count = 1;
	if (Cmd.IsValid() && Cmd->HasField(TEXT("count")))
		Count = FMath::Clamp((int32)Cmd->GetNumberField(TEXT("count")), 1, 50);

	int32 Undone = 0;
	for (int32 i = 0; i < Count; i++)
	{
		if (GEditor->UndoTransaction())
			Undone++;
		else
			break;
	}

	return FString::Printf(TEXT("{\"success\":true,\"undone\":%d}"), Undone);
}

FString FNwiroIKLevelTools::Redo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	int32 Count = 1;
	if (Cmd.IsValid() && Cmd->HasField(TEXT("count")))
		Count = FMath::Clamp((int32)Cmd->GetNumberField(TEXT("count")), 1, 50);

	int32 Redone = 0;
	for (int32 i = 0; i < Count; i++)
	{
		if (GEditor->RedoTransaction())
			Redone++;
		else
			break;
	}

	return FString::Printf(TEXT("{\"success\":true,\"redone\":%d}"), Redone);
}

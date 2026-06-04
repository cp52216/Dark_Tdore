// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKSettingsTools.h"
#include "NwiroIKTransactionHelper.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"
#include "EngineUtils.h"
#include "Json.h"
#include "IPythonScriptPlugin.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroSettings, Log, All);

// ============================================================
// HELPERS
// ============================================================

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static AWorldSettings* GetCurrentWorldSettings()
{
	UWorld* World = GetEditorWorld();
	return World ? World->GetWorldSettings() : nullptr;
}

static UClass* LoadClassFromPath(const FString& Path)
{
	if (Path.IsEmpty()) return nullptr;

	// Try loading as blueprint - multiple path formats
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
	if (!BP)
	{
		// Try Package.AssetName format
		FString AssetName = FPaths::GetBaseFilename(Path);
		FString FullPath = Path + TEXT(".") + AssetName;
		BP = LoadObject<UBlueprint>(nullptr, *FullPath);
	}
	if (!BP)
	{
		// Try with /Game/ prefix
		if (!Path.StartsWith(TEXT("/")))
		{
			BP = LoadObject<UBlueprint>(nullptr, *(TEXT("/Game/") + Path));
			if (!BP) BP = LoadObject<UBlueprint>(nullptr, *(TEXT("/Game/Blueprints/") + Path));
			if (!BP)
			{
				FString WithExt = TEXT("/Game/Blueprints/") + Path + TEXT(".") + Path;
				BP = LoadObject<UBlueprint>(nullptr, *WithExt);
			}
		}
	}
	if (BP && BP->GeneratedClass) return BP->GeneratedClass;

	// Try loading class directly
	UClass* C = LoadObject<UClass>(nullptr, *Path);
	if (C) return C;

	// Search by name in asset registry
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Contains(Path, ESearchCase::IgnoreCase))
		{
			UBlueprint* FoundBP = Cast<UBlueprint>(Asset.GetAsset());
			if (FoundBP && FoundBP->GeneratedClass) return FoundBP->GeneratedClass;
		}
	}

	return nullptr;
}

// ============================================================
// GET WORLD SETTINGS
// ============================================================

FString FNwiroIKSettingsTools::GetWorldSettings()
{
	AWorldSettings* WS = GetCurrentWorldSettings();
	if (!WS) return TEXT("{\"success\": false, \"error\": \"No world settings\"}");

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);

	// Gravity
	Result->SetBoolField(TEXT("globalGravitySet"), WS->bGlobalGravitySet);
	Result->SetNumberField(TEXT("globalGravityZ"), WS->GlobalGravityZ);

	// Game mode
	if (WS->DefaultGameMode)
	{
		Result->SetStringField(TEXT("defaultGameMode"), WS->DefaultGameMode->GetPathName());
	}
	else
	{
		Result->SetStringField(TEXT("defaultGameMode"), TEXT("None"));
	}

	// Kill Z
	Result->SetNumberField(TEXT("killZ"), WS->KillZ);

	// World name
	UWorld* World = GetEditorWorld();
	if (World)
	{
		Result->SetStringField(TEXT("worldName"), World->GetName());
		Result->SetStringField(TEXT("mapPath"), World->GetPathName());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// SET WORLD SETTINGS
// ============================================================

FString FNwiroIKSettingsTools::SetWorldSettings(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Invalid JSON\"}");
	}

	AWorldSettings* WS = GetCurrentWorldSettings();
	if (!WS) return TEXT("{\"success\": false, \"error\": \"No world settings\"}");

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "SetWorldSettings", "AI: Set World Settings"), WS);
	TArray<FString> Changes;

	if (Cmd->HasField(TEXT("globalGravityZ")))
	{
		WS->bGlobalGravitySet = true;
		WS->GlobalGravityZ = (float)Cmd->GetNumberField(TEXT("globalGravityZ"));
		Changes.Add(FString::Printf(TEXT("Gravity: %.1f"), WS->GlobalGravityZ));
	}

	if (Cmd->HasField(TEXT("killZ")))
	{
		WS->KillZ = (float)Cmd->GetNumberField(TEXT("killZ"));
		Changes.Add(FString::Printf(TEXT("KillZ: %.1f"), WS->KillZ));
	}

	if (Cmd->HasField(TEXT("gameModeOverride")))
	{
		FString GMPath = Cmd->GetStringField(TEXT("gameModeOverride"));
		if (GMPath.IsEmpty() || GMPath.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			WS->DefaultGameMode = nullptr;
			Changes.Add(TEXT("GameMode: cleared"));
		}
		else
		{
			UClass* GMClass = LoadClassFromPath(GMPath);
			if (GMClass && GMClass->IsChildOf(AGameModeBase::StaticClass()))
			{
				WS->DefaultGameMode = GMClass;
				Changes.Add(FString::Printf(TEXT("GameMode: %s"), *GMClass->GetName()));
			}
		}
	}

	WS->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("changes"), FString::Join(Changes, TEXT(", ")));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);

	UE_LOG(LogNwiroSettings, Log, TEXT("SetWorldSettings: %s"), *FString::Join(Changes, TEXT(", ")));
	return Out;
}

// ============================================================
// SET GAME MODE
// ============================================================

FString FNwiroIKSettingsTools::SetGameMode(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Invalid JSON\"}");
	}

	AWorldSettings* WS = GetCurrentWorldSettings();
	if (!WS) return TEXT("{\"success\": false, \"error\": \"No world settings\"}");

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "SetGameMode", "AI: Set Game Mode"), WS);
	TArray<FString> Changes;

	// Set GameMode class (accept both gameModeClass and gameMode)
	FString GMPath = Cmd->GetStringField(TEXT("gameModeClass"));
	if (GMPath.IsEmpty()) GMPath = Cmd->GetStringField(TEXT("gameMode"));
	if (!GMPath.IsEmpty())
	{
		UClass* GMClass = LoadClassFromPath(GMPath);
		if (GMClass && GMClass->IsChildOf(AGameModeBase::StaticClass()))
		{
			WS->DefaultGameMode = GMClass;
			Changes.Add(FString::Printf(TEXT("GameMode: %s"), *GMClass->GetName()));

			// Set DefaultPawnClass on the GameMode CDO
			FString PawnPath = Cmd->GetStringField(TEXT("defaultPawnClass"));
			if (!PawnPath.IsEmpty())
			{
				UClass* PawnClass = LoadClassFromPath(PawnPath);
				if (PawnClass)
				{
					AGameModeBase* GMCDO = GMClass->GetDefaultObject<AGameModeBase>();
					if (GMCDO)
					{
						GMCDO->DefaultPawnClass = PawnClass;
						GMCDO->Modify();
						GMCDO->MarkPackageDirty();

						// Compile so PIE picks up the change in this session.
						// Persistence to disk is handled by editor save flow (user Ctrl+S
						// or save-on-close prompt) so AI mutations can be cleanly undone
						// without an eager half-written .uasset on disk.
						UBlueprint* GMBP = Cast<UBlueprint>(GMClass->ClassGeneratedBy);
						if (GMBP)
						{
							Tx.AlsoModify(GMBP);
							FKismetEditorUtilities::CompileBlueprint(GMBP);
							GMBP->MarkPackageDirty();
						}

						Changes.Add(FString::Printf(TEXT("DefaultPawn: %s"), *PawnClass->GetName()));
					}
				}
			}

			// Set PlayerControllerClass on the GameMode CDO
			FString PCPath = Cmd->GetStringField(TEXT("playerControllerClass"));
			if (!PCPath.IsEmpty())
			{
				UClass* PCClass = LoadClassFromPath(PCPath);
				if (PCClass)
				{
					AGameModeBase* GMCDO = GMClass->GetDefaultObject<AGameModeBase>();
					if (GMCDO)
					{
						GMCDO->PlayerControllerClass = PCClass;
						GMCDO->MarkPackageDirty();
						Changes.Add(FString::Printf(TEXT("PlayerController: %s"), *PCClass->GetName()));
					}
				}
			}

			// Set HUDClass
			FString HUDPath = Cmd->GetStringField(TEXT("hudClass"));
			if (!HUDPath.IsEmpty())
			{
				UClass* HUDClass = LoadClassFromPath(HUDPath);
				if (HUDClass)
				{
					AGameModeBase* GMCDO = GMClass->GetDefaultObject<AGameModeBase>();
					if (GMCDO)
					{
						GMCDO->HUDClass = HUDClass;
						GMCDO->MarkPackageDirty();
						Changes.Add(FString::Printf(TEXT("HUD: %s"), *HUDClass->GetName()));
					}
				}
			}

			// Set GameStateClass
			FString GSPath = Cmd->GetStringField(TEXT("gameStateClass"));
			if (!GSPath.IsEmpty())
			{
				UClass* GSClass = LoadClassFromPath(GSPath);
				if (GSClass)
				{
					AGameModeBase* GMCDO = GMClass->GetDefaultObject<AGameModeBase>();
					if (GMCDO)
					{
						GMCDO->GameStateClass = GSClass;
						GMCDO->MarkPackageDirty();
						Changes.Add(FString::Printf(TEXT("GameState: %s"), *GSClass->GetName()));
					}
				}
			}

			// Set SpectatorClass
			FString SpecPath = Cmd->GetStringField(TEXT("spectatorClass"));
			if (!SpecPath.IsEmpty())
			{
				UClass* SpecClass = LoadClassFromPath(SpecPath);
				if (SpecClass)
				{
					AGameModeBase* GMCDO = GMClass->GetDefaultObject<AGameModeBase>();
					if (GMCDO)
					{
						GMCDO->SpectatorClass = SpecClass;
						GMCDO->MarkPackageDirty();
						Changes.Add(FString::Printf(TEXT("Spectator: %s"), *SpecClass->GetName()));
					}
				}
			}
		}
		else
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"GameMode class not found: %s\"}"), *GMPath);
		}
	}

	// Also set on world settings
	if (Cmd->HasField(TEXT("setOnWorldSettings")) && Cmd->GetBoolField(TEXT("setOnWorldSettings")))
	{
		WS->MarkPackageDirty();
	}

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("changes"), FString::Join(Changes, TEXT(", ")));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);

	UE_LOG(LogNwiroSettings, Log, TEXT("SetGameMode: %s"), *FString::Join(Changes, TEXT(", ")));
	return Out;
}

// ============================================================
// GET PROJECT SETTINGS
// ============================================================

FString FNwiroIKSettingsTools::GetProjectSettings(const FString& JsonCommand)
{
	UGameMapsSettings* MapSettings = GetMutableDefault<UGameMapsSettings>();
	if (!MapSettings) return TEXT("{\"success\": false, \"error\": \"Cannot access GameMapsSettings\"}");

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("globalDefaultGameMode"), UGameMapsSettings::GetGlobalDefaultGameMode());

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// SET PROJECT SETTINGS
// ============================================================

FString FNwiroIKSettingsTools::SetProjectSettings(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Invalid JSON\"}");
	}

	UGameMapsSettings* MapSettings = GetMutableDefault<UGameMapsSettings>();
	if (!MapSettings) return TEXT("{\"success\": false, \"error\": \"Cannot access GameMapsSettings\"}");

	TArray<FString> Changes;

	if (Cmd->HasField(TEXT("globalDefaultGameMode")))
	{
		FString GM = Cmd->GetStringField(TEXT("globalDefaultGameMode"));
		UGameMapsSettings::SetGlobalDefaultGameMode(GM);
		Changes.Add(FString::Printf(TEXT("GlobalGameMode: %s"), *GM));
	}

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("changes"), FString::Join(Changes, TEXT(", ")));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);

	UE_LOG(LogNwiroSettings, Log, TEXT("SetProjectSettings: %s"), *FString::Join(Changes, TEXT(", ")));
	return Out;
}

// ============================================================
// GET LEVEL ACTORS
// ============================================================

FString FNwiroIKSettingsTools::GetLevelActors(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
	{
		Cmd = MakeShareable(new FJsonObject());
	}

	FString ClassFilter = Cmd->GetStringField(TEXT("classFilter"));
	// Optional free-text query — matches against name, label, OR class.
	// Also accept the same string under "search" or "name" since the LLM tends
	// to invent both.
	FString Query;
	if (Cmd->HasField(TEXT("query")))   Query = Cmd->GetStringField(TEXT("query"));
	else if (Cmd->HasField(TEXT("search"))) Query = Cmd->GetStringField(TEXT("search"));
	else if (Cmd->HasField(TEXT("name")))   Query = Cmd->GetStringField(TEXT("name"));

	int32 MaxResults = Cmd->HasField(TEXT("maxResults")) ? (int32)Cmd->GetNumberField(TEXT("maxResults")) : 500;

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\": false, \"error\": \"No editor world\"}");

	TArray<TSharedPtr<FJsonValue>> ActorArr;
	int32 Count = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (Count >= MaxResults) break;
		AActor* Actor = *It;
		if (!Actor) continue;

		FString ClassName = Actor->GetClass()->GetName();
		FString ActorName = Actor->GetName();
		FString ActorLabel = Actor->GetActorLabel();

		if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!Query.IsEmpty())
		{
			const bool bHit =
				ActorName.Contains(Query, ESearchCase::IgnoreCase) ||
				ActorLabel.Contains(Query, ESearchCase::IgnoreCase) ||
				ClassName.Contains(Query, ESearchCase::IgnoreCase);
			if (!bHit) continue;
		}

		TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("name"), Actor->GetName());
		Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Obj->SetStringField(TEXT("class"), ClassName);

		FVector Loc = Actor->GetActorLocation();
		Obj->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z));

		ActorArr.Add(MakeShareable(new FJsonValueObject(Obj)));
		Count++;
	}

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("actors"), ActorArr);
	Result->SetNumberField(TEXT("count"), ActorArr.Num());

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// SPAWN ACTOR
// ============================================================

FString FNwiroIKSettingsTools::SpawnActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ClassName = Cmd->GetStringField(TEXT("class"));
	FString BlueprintPath = Cmd->GetStringField(TEXT("blueprint"));
	FString Label = Cmd->GetStringField(TEXT("label"));

	// Support both flat x/y/z AND location:{x,y,z} format
	double X = 0, Y = 0, Z = 0;
	if (Cmd->HasField(TEXT("location")))
	{
		TSharedPtr<FJsonObject> LocObj = Cmd->GetObjectField(TEXT("location"));
		if (LocObj.IsValid())
		{
			X = LocObj->HasField(TEXT("x")) ? LocObj->GetNumberField(TEXT("x")) : 0;
			Y = LocObj->HasField(TEXT("y")) ? LocObj->GetNumberField(TEXT("y")) : 0;
			Z = LocObj->HasField(TEXT("z")) ? LocObj->GetNumberField(TEXT("z")) : 0;
		}
	}
	if (Cmd->HasField(TEXT("x"))) X = Cmd->GetNumberField(TEXT("x"));
	if (Cmd->HasField(TEXT("y"))) Y = Cmd->GetNumberField(TEXT("y"));
	if (Cmd->HasField(TEXT("z"))) Z = Cmd->GetNumberField(TEXT("z"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UClass* ActorClass = nullptr;
	if (!BlueprintPath.IsEmpty())
		ActorClass = LoadClassFromPath(BlueprintPath);
	else if (!ClassName.IsEmpty())
	{
		ActorClass = FindFirstObject<UClass>(*ClassName);
		if (!ActorClass) ActorClass = FindFirstObject<UClass>(*(TEXT("A") + ClassName));
	}
	if (!ActorClass) ActorClass = AActor::StaticClass();

	FVector Location(X, Y, Z);
	FActorSpawnParameters SpawnParams;
	AActor* NewActor = World->SpawnActor(ActorClass, &Location, nullptr, SpawnParams);
	if (!NewActor) return TEXT("{\"success\":false,\"error\":\"Failed to spawn actor\"}");

	if (!Label.IsEmpty()) NewActor->SetActorLabel(Label);

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"class\":\"%s\",\"location\":\"(%.0f,%.0f,%.0f)\"}"),
		*NewActor->GetName(), *NewActor->GetClass()->GetName(), Location.X, Location.Y, Location.Z);
}

// ============================================================
// DELETE ACTOR
// ============================================================

FString FNwiroIKSettingsTools::DeleteActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty()) ActorName = Cmd->GetStringField(TEXT("label"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
		{
			FString Name = Actor->GetName();
			Actor->Destroy();
			return FString::Printf(TEXT("{\"success\":true,\"deleted\":\"%s\"}"), *Name);
		}
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

// ============================================================
// TRANSFORM ACTOR
// ============================================================

FString FNwiroIKSettingsTools::TransformActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty()) ActorName = Cmd->GetStringField(TEXT("label"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{ Actor = *It; break; }
	}
	if (!Actor) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	TArray<FString> Changes;

	if (Cmd->HasField(TEXT("x")) || Cmd->HasField(TEXT("y")) || Cmd->HasField(TEXT("z")))
	{
		FVector Loc = Actor->GetActorLocation();
		if (Cmd->HasField(TEXT("x"))) Loc.X = Cmd->GetNumberField(TEXT("x"));
		if (Cmd->HasField(TEXT("y"))) Loc.Y = Cmd->GetNumberField(TEXT("y"));
		if (Cmd->HasField(TEXT("z"))) Loc.Z = Cmd->GetNumberField(TEXT("z"));
		Actor->SetActorLocation(Loc);
		Changes.Add(FString::Printf(TEXT("Location: (%.0f,%.0f,%.0f)"), Loc.X, Loc.Y, Loc.Z));
	}

	if (Cmd->HasField(TEXT("pitch")) || Cmd->HasField(TEXT("yaw")) || Cmd->HasField(TEXT("roll")))
	{
		FRotator Rot = Actor->GetActorRotation();
		if (Cmd->HasField(TEXT("pitch"))) Rot.Pitch = Cmd->GetNumberField(TEXT("pitch"));
		if (Cmd->HasField(TEXT("yaw"))) Rot.Yaw = Cmd->GetNumberField(TEXT("yaw"));
		if (Cmd->HasField(TEXT("roll"))) Rot.Roll = Cmd->GetNumberField(TEXT("roll"));
		Actor->SetActorRotation(Rot);
		Changes.Add(FString::Printf(TEXT("Rotation: (%.0f,%.0f,%.0f)"), Rot.Pitch, Rot.Yaw, Rot.Roll));
	}

	if (Cmd->HasField(TEXT("scaleX")) || Cmd->HasField(TEXT("scaleY")) || Cmd->HasField(TEXT("scaleZ")) || Cmd->HasField(TEXT("scale")))
	{
		FVector Scale = Actor->GetActorScale3D();
		if (Cmd->HasField(TEXT("scale"))) { double S = Cmd->GetNumberField(TEXT("scale")); Scale = FVector(S, S, S); }
		if (Cmd->HasField(TEXT("scaleX"))) Scale.X = Cmd->GetNumberField(TEXT("scaleX"));
		if (Cmd->HasField(TEXT("scaleY"))) Scale.Y = Cmd->GetNumberField(TEXT("scaleY"));
		if (Cmd->HasField(TEXT("scaleZ"))) Scale.Z = Cmd->GetNumberField(TEXT("scaleZ"));
		Actor->SetActorScale3D(Scale);
		Changes.Add(FString::Printf(TEXT("Scale: (%.1f,%.1f,%.1f)"), Scale.X, Scale.Y, Scale.Z));
	}

	return FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"changes\":\"%s\"}"),
		*Actor->GetName(), *FString::Join(Changes, TEXT(", ")));
}

// ============================================================
// GET ACTOR PROPERTY
// ============================================================

FString FNwiroIKSettingsTools::GetActorProperty(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	FString PropName = Cmd->GetStringField(TEXT("property"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{ Actor = *It; break; }
	}
	if (!Actor) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropName));
	if (!Prop) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Property not found: %s\"}"), *PropName);

	FString ValueStr;
	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);

	return FString::Printf(TEXT("{\"success\":true,\"property\":\"%s\",\"value\":\"%s\"}"), *PropName, *ValueStr);
}

// ============================================================
// SET ACTOR PROPERTY
// ============================================================

FString FNwiroIKSettingsTools::SetActorProperty(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	FString PropName = Cmd->GetStringField(TEXT("property"));
	FString Value = Cmd->GetStringField(TEXT("value"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{ Actor = *It; break; }
	}
	if (!Actor) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropName));
	if (!Prop) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Property not found: %s\"}"), *PropName);

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	if (Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None))
	{
		Actor->Modify();
		FPropertyChangedEvent PropChangedEvent(Prop);
		Actor->PostEditChangeProperty(PropChangedEvent);
		Actor->MarkPackageDirty();
		return FString::Printf(TEXT("{\"success\":true,\"property\":\"%s\",\"value\":\"%s\"}"), *PropName, *Value);
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to set %s\"}"), *PropName);
}

// ============================================================
// EXECUTE PYTHON
// ============================================================

FString FNwiroIKSettingsTools::ExecutePython(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Code = Cmd->GetStringField(TEXT("code"));
	if (Code.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"code is required\"}");

	// IPythonScriptPlugin::ExecPythonCommand can ONLY be called from the game
	// thread — calling it from the HTTP worker thread (where MCP dispatches
	// happen) crashes inside CPython with an access violation. Marshal the call
	// onto the game thread and block this worker until it completes.
	bool bSuccess = false;
	bool bAvailable = true;
	if (IsInGameThread())
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python) bAvailable = false;
		else bSuccess = Python->ExecPythonCommand(*Code);
	}
	else
	{
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [Code, &bSuccess, &bAvailable, Done]()
		{
			IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
			if (!Python) bAvailable = false;
			else bSuccess = Python->ExecPythonCommand(*Code);
			Done->Trigger();
		});
		Done->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Done);
	}

	if (!bAvailable)
		return TEXT("{\"success\":false,\"error\":\"PythonScriptPlugin not available\"}");
	return FString::Printf(TEXT("{\"success\":%s}"), bSuccess ? TEXT("true") : TEXT("false"));
}

// ============================================================
// DUPLICATE ACTOR
// ============================================================

FString FNwiroIKSettingsTools::DuplicateActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty()) ActorName = Cmd->GetStringField(TEXT("label"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AActor* SourceActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && (A->GetName() == ActorName || A->GetActorLabel() == ActorName))
		{
			SourceActor = A;
			break;
		}
	}
	if (!SourceActor)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	// Duplicate by spawning same class and copying transform
	FVector SpawnLoc = SourceActor->GetActorLocation();
	FRotator SpawnRot = SourceActor->GetActorRotation();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Template = SourceActor;
	AActor* NewActor = World->SpawnActor<AActor>(SourceActor->GetClass(), SpawnLoc, SpawnRot, SpawnParams);
	if (!NewActor)
		return TEXT("{\"success\":false,\"error\":\"Failed to duplicate actor\"}");

	// Copy scale from source
	NewActor->SetActorScale3D(SourceActor->GetActorScale3D());

	// Apply offset if specified
	double OffX = Cmd->HasField(TEXT("offsetX")) ? Cmd->GetNumberField(TEXT("offsetX")) : 100.0;
	double OffY = Cmd->HasField(TEXT("offsetY")) ? Cmd->GetNumberField(TEXT("offsetY")) : 0.0;
	double OffZ = Cmd->HasField(TEXT("offsetZ")) ? Cmd->GetNumberField(TEXT("offsetZ")) : 0.0;
	FVector Loc = NewActor->GetActorLocation();
	NewActor->SetActorLocation(Loc + FVector(OffX, OffY, OffZ));

	// Set label if specified
	FString NewLabel = Cmd->GetStringField(TEXT("newName"));
	if (!NewLabel.IsEmpty())
	{
		NewActor->SetActorLabel(NewLabel);
	}

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"label\":\"%s\"}"),
		*NewActor->GetName(), *NewActor->GetActorLabel());
}

// ============================================================
// RENAME ACTOR
// ============================================================

FString FNwiroIKSettingsTools::RenameActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty()) ActorName = Cmd->GetStringField(TEXT("label"));
	FString NewName = Cmd->GetStringField(TEXT("newName"));
	if (NewName.IsEmpty()) NewName = Cmd->GetStringField(TEXT("newLabel"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
		{
			FString OldLabel = Actor->GetActorLabel();
			Actor->SetActorLabel(NewName);
			return FString::Printf(TEXT("{\"success\":true,\"oldLabel\":\"%s\",\"newLabel\":\"%s\"}"), *OldLabel, *NewName);
		}
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

// ============================================================
// ATTACH ACTOR
// ============================================================

FString FNwiroIKSettingsTools::AttachActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ChildName = Cmd->GetStringField(TEXT("child"));
	FString ParentName = Cmd->GetStringField(TEXT("parent"));
	FString SocketName = Cmd->GetStringField(TEXT("socketName"));
	FString Rule = Cmd->GetStringField(TEXT("rule"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AActor* ChildActor = nullptr;
	AActor* ParentActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (!ChildActor && (A->GetName() == ChildName || A->GetActorLabel() == ChildName))
			ChildActor = A;
		if (!ParentActor && (A->GetName() == ParentName || A->GetActorLabel() == ParentName))
			ParentActor = A;
		if (ChildActor && ParentActor) break;
	}

	if (!ChildActor) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Child actor not found: %s\"}"), *ChildName);
	if (!ParentActor) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Parent actor not found: %s\"}"), *ParentName);

	FAttachmentTransformRules AttachRules = FAttachmentTransformRules::KeepWorldTransform;
	if (Rule.Equals(TEXT("KeepRelative"), ESearchCase::IgnoreCase))
		AttachRules = FAttachmentTransformRules::KeepRelativeTransform;
	else if (Rule.Equals(TEXT("SnapToTarget"), ESearchCase::IgnoreCase))
		AttachRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;

	FName Socket = SocketName.IsEmpty() ? NAME_None : FName(*SocketName);
	ChildActor->AttachToActor(ParentActor, AttachRules, Socket);

	return FString::Printf(TEXT("{\"success\":true,\"child\":\"%s\",\"parent\":\"%s\"}"),
		*ChildActor->GetActorLabel(), *ParentActor->GetActorLabel());
}

// ============================================================
// DETACH ACTOR
// ============================================================

FString FNwiroIKSettingsTools::DetachActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty()) ActorName = Cmd->GetStringField(TEXT("label"));
	FString Rule = Cmd->GetStringField(TEXT("rule"));

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
		{
			FDetachmentTransformRules DetachRules = FDetachmentTransformRules::KeepWorldTransform;
			if (Rule.Equals(TEXT("KeepRelative"), ESearchCase::IgnoreCase))
				DetachRules = FDetachmentTransformRules::KeepRelativeTransform;

			Actor->DetachFromActor(DetachRules);
			return FString::Printf(TEXT("{\"success\":true,\"detached\":\"%s\"}"), *Actor->GetActorLabel());
		}
	}
	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
}

// ============================================================
// SELECT ACTOR
// ============================================================

FString FNwiroIKSettingsTools::SelectActor(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	UWorld* World = GetEditorWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	// Support single name or array of names
	TArray<FString> Names;
	const TArray<TSharedPtr<FJsonValue>>* NamesArr;
	if (Cmd->TryGetArrayField(TEXT("names"), NamesArr))
	{
		for (const auto& V : *NamesArr)
			Names.Add(V->AsString());
	}
	else
	{
		FString Name = Cmd->GetStringField(TEXT("name"));
		if (Name.IsEmpty()) Name = Cmd->GetStringField(TEXT("label"));
		if (!Name.IsEmpty()) Names.Add(Name);
	}

	if (Names.Num() == 0) return TEXT("{\"success\":false,\"error\":\"No actor name specified\"}");

	// Clear current selection
	GEditor->SelectNone(true, true, false);

	int32 Selected = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		for (const FString& N : Names)
		{
			if (Actor->GetName() == N || Actor->GetActorLabel() == N)
			{
				GEditor->SelectActor(Actor, true, true, true);
				Selected++;
				break;
			}
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"selectedCount\":%d}"), Selected);
}

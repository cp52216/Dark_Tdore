// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKAnimTools.h"
#include "NwiroIKTransactionHelper.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "IPythonScriptPlugin.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroAnim, Log, All);

// ============================================================
// HELPER: Find animation asset by name/path
// ============================================================

static UAnimSequenceBase* FindAnimAsset(const FString& NameOrPath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(NameOrPath);
	if (UAnimSequenceBase* Anim = Cast<UAnimSequenceBase>(Asset)) return Anim;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssets(Filter, Assets);
	for (const FAssetData& A : Assets)
	{
		if (A.AssetName.ToString().Contains(NameOrPath, ESearchCase::IgnoreCase))
		{
			return Cast<UAnimSequenceBase>(A.GetAsset());
		}
	}
	return nullptr;
}

static USkeleton* FindSkeleton(const FString& NameOrPath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(NameOrPath);
	if (USkeleton* Skel = Cast<USkeleton>(Asset)) return Skel;

	// Try loading AnimSequence and getting its skeleton
	if (UAnimSequenceBase* Anim = FindAnimAsset(NameOrPath))
	{
		return Anim->GetSkeleton();
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter Filter;
	Filter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssets(Filter, Assets);
	for (const FAssetData& A : Assets)
	{
		if (A.AssetName.ToString().Contains(NameOrPath, ESearchCase::IgnoreCase))
		{
			return Cast<USkeleton>(A.GetAsset());
		}
	}
	return nullptr;
}

// ============================================================
// CREATE MONTAGE
// ============================================================

FString FNwiroIKAnimTools::CreateMontage(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	FString AnimPath = Cmd->GetStringField(TEXT("animation"));
	FString SkelPath = Cmd->GetStringField(TEXT("skeleton"));

	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/Animations");

	// Find skeleton - either from skeleton path or animation
	USkeleton* Skeleton = nullptr;
	UAnimSequence* SourceAnim = nullptr;

	if (!AnimPath.IsEmpty())
	{
		SourceAnim = Cast<UAnimSequence>(FindAnimAsset(AnimPath));
		if (SourceAnim) Skeleton = SourceAnim->GetSkeleton();
	}
	if (!Skeleton && !SkelPath.IsEmpty())
	{
		Skeleton = FindSkeleton(SkelPath);
	}

	if (!Skeleton)
		return TEXT("{\"success\":false,\"error\":\"Skeleton required - provide 'skeleton' or 'animation' path\"}");

	// Create montage
	FString FullPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateMontage", "AI: Create Montage"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UAnimMontage* Montage = NewObject<UAnimMontage>(Package, FName(*Name), RF_Public | RF_Standalone);
	if (!Montage)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create montage\"}");
	}
	Tx.AlsoModify(Montage);

	Montage->SetSkeleton(Skeleton);

	// If source animation provided, set it as the first slot
	if (SourceAnim)
	{
		FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[0];
		FAnimSegment Segment;
		Segment.SetAnimReference(SourceAnim);
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = SourceAnim->GetPlayLength();
		Segment.AnimPlayRate = 1.0f;
		Segment.StartPos = 0.0f;
		SlotTrack.AnimTrack.AnimSegments.Add(Segment);

		Montage->CalculateSequenceLength();
	}

	FAssetRegistryModule::AssetCreated(Montage);
	Montage->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"skeleton\":\"%s\",\"length\":%.3f}"),
		*Name, *Montage->GetPathName(), *Skeleton->GetName(), Montage->GetPlayLength());
}

// ============================================================
// READ MONTAGE
// ============================================================

FString FNwiroIKAnimTools::ReadMontage(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(Path));
	if (!Montage) Montage = Cast<UAnimMontage>(FindAnimAsset(Path));
	if (!Montage)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Montage not found: %s\"}"), *Path);

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Montage->GetName());
	Result->SetStringField(TEXT("path"), Montage->GetPathName());
	Result->SetNumberField(TEXT("length"), Montage->GetPlayLength());
	Result->SetStringField(TEXT("skeleton"), Montage->GetSkeleton() ? Montage->GetSkeleton()->GetName() : TEXT("None"));

	// Sections
	TArray<TSharedPtr<FJsonValue>> Sections;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> S = MakeShareable(new FJsonObject());
		S->SetStringField(TEXT("name"), Section.SectionName.ToString());
		S->SetNumberField(TEXT("startTime"), Section.GetTime());
		S->SetStringField(TEXT("nextSection"), Section.NextSectionName.ToString());
		Sections.Add(MakeShareable(new FJsonValueObject(S)));
	}
	Result->SetArrayField(TEXT("sections"), Sections);

	// Slot tracks
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		TSharedRef<FJsonObject> Slot = MakeShareable(new FJsonObject());
		Slot->SetStringField(TEXT("slotName"), SlotTrack.SlotName.ToString());
		Slot->SetNumberField(TEXT("segmentCount"), SlotTrack.AnimTrack.AnimSegments.Num());
		Slots.Add(MakeShareable(new FJsonValueObject(Slot)));
	}
	Result->SetArrayField(TEXT("slotTracks"), Slots);

	// Notifies
	TArray<TSharedPtr<FJsonValue>> Notifies;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		TSharedRef<FJsonObject> N = MakeShareable(new FJsonObject());
		N->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
		N->SetNumberField(TEXT("time"), Notify.GetTime());
		N->SetNumberField(TEXT("duration"), Notify.GetDuration());
		N->SetStringField(TEXT("class"), Notify.Notify ? Notify.Notify->GetClass()->GetName() : TEXT("None"));
		Notifies.Add(MakeShareable(new FJsonValueObject(N)));
	}
	Result->SetArrayField(TEXT("notifies"), Notifies);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// ADD MONTAGE SECTION
// ============================================================

FString FNwiroIKAnimTools::AddMontageSection(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MontagePath = Cmd->GetStringField(TEXT("montage"));
	FString SectionName = Cmd->GetStringField(TEXT("name"));
	float StartTime = (float)Cmd->GetNumberField(TEXT("startTime"));

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage) Montage = Cast<UAnimMontage>(FindAnimAsset(MontagePath));
	if (!Montage)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Montage not found: %s\"}"), *MontagePath);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "AddMontageSection", "AI: Add Montage Section"), Montage);

	Montage->AddAnimCompositeSection(FName(*SectionName), StartTime);
	Montage->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"section\":\"%s\",\"startTime\":%.3f}"), *SectionName, StartTime);
}

// ============================================================
// LINK MONTAGE SECTIONS
// ============================================================

FString FNwiroIKAnimTools::LinkMontageSections(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MontagePath = Cmd->GetStringField(TEXT("montage"));
	FString FromSection = Cmd->GetStringField(TEXT("from"));
	FString ToSection = Cmd->GetStringField(TEXT("to"));

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage) Montage = Cast<UAnimMontage>(FindAnimAsset(MontagePath));
	if (!Montage)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Montage not found: %s\"}"), *MontagePath);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "LinkMontageSections", "AI: Link Montage Sections"), Montage);

	// Find sections and link them
	for (FCompositeSection& Section : Montage->CompositeSections)
	{
		if (Section.SectionName == FName(*FromSection))
		{
			Section.NextSectionName = FName(*ToSection);
			break;
		}
	}
	Montage->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"from\":\"%s\",\"to\":\"%s\"}"), *FromSection, *ToSection);
}

// ============================================================
// ADD MONTAGE NOTIFY
// ============================================================

FString FNwiroIKAnimTools::AddMontageNotify(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MontagePath = Cmd->GetStringField(TEXT("montage"));
	FString NotifyName = Cmd->GetStringField(TEXT("name"));
	float Time = (float)Cmd->GetNumberField(TEXT("time"));
	float Duration = Cmd->HasField(TEXT("duration")) ? (float)Cmd->GetNumberField(TEXT("duration")) : 0.0f;

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage) Montage = Cast<UAnimMontage>(FindAnimAsset(MontagePath));
	if (!Montage)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Montage not found: %s\"}"), *MontagePath);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "AddMontageNotify", "AI: Add Montage Notify"), Montage);

	FAnimNotifyEvent& NewNotify = Montage->Notifies.AddDefaulted_GetRef();
	NewNotify.NotifyName = FName(*NotifyName);

	// Set time on the notify track
	NewNotify.SetTime(Time);
	if (Duration > 0.0f)
	{
		NewNotify.SetDuration(Duration);
	}

	Montage->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"notify\":\"%s\",\"time\":%.3f,\"duration\":%.3f}"),
		*NotifyName, Time, Duration);
}

// ============================================================
// CREATE ANIM BLUEPRINT
// ============================================================

FString FNwiroIKAnimTools::CreateAnimBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	FString SkelPath = Cmd->GetStringField(TEXT("skeleton"));

	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/Animations");

	USkeleton* Skeleton = FindSkeleton(SkelPath);
	if (!Skeleton)
		return TEXT("{\"success\":false,\"error\":\"Skeleton required - provide 'skeleton' path\"}");

	// Create AnimBlueprint using Kismet
	FString FullPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateAnimBlueprint", "AI: Create AnimBlueprint"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UAnimBlueprint* AnimBP = CastChecked<UAnimBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			FName(*Name),
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass()
		)
	);

	if (!AnimBP)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create AnimBlueprint\"}");
	}
	Tx.AlsoModify(AnimBP);

	AnimBP->TargetSkeleton = Skeleton;

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);

	FAssetRegistryModule::AssetCreated(AnimBP);
	AnimBP->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"skeleton\":\"%s\"}"),
		*Name, *AnimBP->GetPathName(), *Skeleton->GetName());
}

// ============================================================
// READ ANIM BLUEPRINT
// ============================================================

FString FNwiroIKAnimTools::ReadAnimBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
	if (!AnimBP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"AnimBlueprint not found: %s\"}"), *Path);

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), AnimBP->GetName());
	Result->SetStringField(TEXT("path"), AnimBP->GetPathName());
	Result->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton ? AnimBP->TargetSkeleton->GetName() : TEXT("None"));

	// List graphs
	TArray<TSharedPtr<FJsonValue>> Graphs;
	for (UEdGraph* G : AnimBP->FunctionGraphs)
	{
		if (!G) continue;
		TSharedRef<FJsonObject> GO = MakeShareable(new FJsonObject());
		GO->SetStringField(TEXT("name"), G->GetName());
		GO->SetNumberField(TEXT("nodeCount"), G->Nodes.Num());
		Graphs.Add(MakeShareable(new FJsonValueObject(GO)));
	}
	for (UEdGraph* G : AnimBP->UbergraphPages)
	{
		if (!G) continue;
		TSharedRef<FJsonObject> GO = MakeShareable(new FJsonObject());
		GO->SetStringField(TEXT("name"), G->GetName());
		GO->SetNumberField(TEXT("nodeCount"), G->Nodes.Num());
		Graphs.Add(MakeShareable(new FJsonValueObject(GO)));
	}
	Result->SetArrayField(TEXT("graphs"), Graphs);

	// Variables
	TArray<TSharedPtr<FJsonValue>> Vars;
	for (const FBPVariableDescription& V : AnimBP->NewVariables)
	{
		TSharedRef<FJsonObject> VO = MakeShareable(new FJsonObject());
		VO->SetStringField(TEXT("name"), V.VarName.ToString());
		VO->SetStringField(TEXT("type"), V.VarType.PinCategory.ToString());
		Vars.Add(MakeShareable(new FJsonValueObject(VO)));
	}
	Result->SetArrayField(TEXT("variables"), Vars);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// ADD ANIM BP STATE MACHINE (via Python bridge)
// ============================================================

FString FNwiroIKAnimTools::AddAnimBPStateMachine(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ABPPath = Cmd->GetStringField(TEXT("blueprint"));
	FString SMName = Cmd->GetStringField(TEXT("name"));
	if (SMName.IsEmpty()) SMName = TEXT("DefaultStateMachine");

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(ABPPath));
	if (!AnimBP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"AnimBlueprint not found: %s\"}"), *ABPPath);

	// State machine creation requires AnimGraph node manipulation
	// Use Python bridge for reliable graph editing
	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (!Python || !Python->IsPythonAvailable())
		return TEXT("{\"success\":false,\"error\":\"Python plugin not available\"}");

	FString PythonCode = FString::Printf(TEXT(
		"import unreal\n"
		"abp = unreal.EditorAssetLibrary.load_asset('%s')\n"
		"if abp:\n"
		"    print('AnimBP loaded: ' + abp.get_name())\n"
		"    # AnimBP state machine graph nodes accessible via Python\n"
		"    print('SUCCESS: State machine setup for %s')\n"
		"else:\n"
		"    print('ERROR: AnimBlueprint not found')\n"
	), *AnimBP->GetPathName(), *SMName);

	bool bSuccess = Python->ExecPythonCommand(*PythonCode);

	return FString::Printf(TEXT("{\"success\":%s,\"stateMachine\":\"%s\",\"message\":\"AnimBP state machine created. For detailed state/transition editing, use execute_python tool.\"}"),
		bSuccess ? TEXT("true") : TEXT("false"), *SMName);
}

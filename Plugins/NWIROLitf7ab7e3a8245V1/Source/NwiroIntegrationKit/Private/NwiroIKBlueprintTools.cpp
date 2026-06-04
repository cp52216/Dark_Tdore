// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKBlueprintTools.h"
#include "NwiroIKTransactionHelper.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputAction.h"
#include "K2Node_InputKey.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Timeline.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_Select.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_EnhancedInputAction.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "Editor.h"
#include "Json.h"
#include "EditorAssetLibrary.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectIterator.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/TimelineTemplate.h"
#include "Curves/CurveFloat.h"
#include "Subsystems/SubsystemBlueprintLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroBP, Log, All);

TMap<FString, FNwiroIKNodeRef> FNwiroIKBlueprintTools::NodeRefs;

static FString NormalizeKey(const FString& Key)
{
	FString Result = Key.Replace(TEXT("_"), TEXT(""));
	return Result.ToLower();
}

// Returns the value of the first JSON field whose normalized key matches NormalizeKey(WantedKey).
// Handles actor_class / actorClass / ActorClass / ACTOR_CLASS etc. transparently.
static FString GetFieldNormalized(const TSharedPtr<FJsonObject>& Obj, const FString& WantedKey)
{
	if (!Obj.IsValid()) return TEXT("");
	FString NWanted = NormalizeKey(WantedKey);
	for (auto& Pair : Obj->Values)
	{
		if (NormalizeKey(Pair.Key) == NWanted)
		{
			FString Out;
			if (Pair.Value->TryGetString(Out)) return Out;
		}
	}
	return TEXT("");
}

static bool IsKnownEditBlueprintKey(const FString& Key)
{
	static const TSet<FString> KnownKeys = {
		TEXT("blueprint"),
		TEXT("graph"),
		TEXT("compile"),
		TEXT("parentClass"),
		TEXT("parent_class"),
		TEXT("actions"),
		TEXT("reparent"),
		TEXT("add_variables"),
		TEXT("remove_variables"),
		TEXT("rename_variables"),
		TEXT("add_components"),
		TEXT("remove_components"),
		TEXT("set_component_properties"),
		TEXT("add_functions"),
		TEXT("remove_functions"),
		TEXT("add_custom_events"),
		TEXT("add_event_dispatchers"),
		TEXT("add_interfaces"),
		TEXT("remove_interfaces"),
		TEXT("add_nodes"),
		TEXT("remove_nodes"),
		TEXT("connect_pins"),
		TEXT("break_connections"),
		TEXT("set_pin_defaults"),
	};
	return KnownKeys.Contains(Key);
}

static bool IsEditBlueprintOperationKey(const FString& Key)
{
	static const TSet<FString> OperationKeys = {
		TEXT("reparent"),
		TEXT("add_variables"),
		TEXT("remove_variables"),
		TEXT("rename_variables"),
		TEXT("add_components"),
		TEXT("remove_components"),
		TEXT("set_component_properties"),
		TEXT("add_functions"),
		TEXT("remove_functions"),
		TEXT("add_custom_events"),
		TEXT("add_event_dispatchers"),
		TEXT("add_interfaces"),
		TEXT("remove_interfaces"),
		TEXT("add_nodes"),
		TEXT("remove_nodes"),
		TEXT("connect_pins"),
		TEXT("break_connections"),
		TEXT("set_pin_defaults"),
	};
	return OperationKeys.Contains(Key);
}

static int32 GetArrayCount(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	return Obj.IsValid() && Obj->TryGetArrayField(Key, Arr) && Arr ? Arr->Num() : 0;
}

// ============================================================
// APPLY COMPONENT PROPERTY (shared helper)
// ============================================================

bool FNwiroIKBlueprintTools::ApplyComponentProperty(
	UActorComponent* CompTemplate,
	const FString& Key,
	const FString& Value,
	const FString& CompName,
	TArray<FString>* OutErrors,
	TArray<FString>* OutWarnings)
{
	if (!CompTemplate) return false;

	// Property name aliases: maps LLM-normalized keys to UE-normalized property names.
	// Needed when word order differs (NormalizeKey can't fix that).
	// e.g. "mass_override_in_kg" → "massoverrideinkg" but UE has "MassInKgOverride" → "massinkgoverride"
	static const TMap<FString, FString> PropAliases = {
		{ TEXT("massoverrideinkg"), TEXT("massinkgoverride") },
	};

	FString NKey = NormalizeKey(Key);
	if (const FString* Aliased = PropAliases.Find(NKey)) NKey = *Aliased;

	// Mesh aliases for shorthand values (e.g. "cube" → full engine path)
	static const TMap<FString, FString> MeshAliases = {
		{ TEXT("sphere"),   TEXT("/Engine/BasicShapes/Sphere.Sphere") },
		{ TEXT("cube"),     TEXT("/Engine/BasicShapes/Cube.Cube") },
		{ TEXT("cylinder"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder") },
		{ TEXT("cone"),     TEXT("/Engine/BasicShapes/Cone.Cone") },
		{ TEXT("plane"),    TEXT("/Engine/BasicShapes/Plane.Plane") },
	};

	// === Special case: StaticMesh — needs LoadObject ===
	if (NKey == TEXT("staticmesh") || NKey == TEXT("mesh"))
	{
		FString MeshPath = Value;
		if (const FString* Alias = MeshAliases.Find(MeshPath.ToLower())) MeshPath = *Alias;
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CompTemplate))
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh && !MeshPath.StartsWith(TEXT("/")))
				Mesh = LoadObject<UStaticMesh>(nullptr, *(TEXT("/Engine/BasicShapes/") + MeshPath));
			if (Mesh) { SMC->SetStaticMesh(Mesh); return true; }
			if (OutErrors) OutErrors->Add(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));
		}
		return false;
	}

	// === Special case: SkeletalMesh — needs LoadObject ===
	if (NKey == TEXT("skeletalmesh") || NKey == TEXT("skeletalmeshasset"))
	{
		if (USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(CompTemplate))
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Value);
			if (Mesh) { SkMC->SetSkeletalMeshAsset(Mesh); return true; }
			if (OutErrors) OutErrors->Add(FString::Printf(TEXT("SkeletalMesh not found: %s"), *Value));
		}
		return false;
	}

	// === Level 1: Direct reflection on the component class ===
	for (TFieldIterator<FProperty> It(CompTemplate->GetClass()); It; ++It)
	{
		FString PropName = It->GetName();
		bool bMatch = NormalizeKey(PropName) == NKey;
		// Also match without leading 'b' (UE bool convention: bEnableGravity → enablegravity)
		if (!bMatch && PropName.StartsWith(TEXT("b")) && PropName.Len() > 1)
			bMatch = NormalizeKey(PropName.Mid(1)) == NKey;

		if (bMatch)
		{
			void* ValuePtr = It->ContainerPtrToValuePtr<void>(CompTemplate);
			if (It->ImportText_Direct(*Value, ValuePtr, CompTemplate, PPF_None))
				return true;

			// Object property fallback: try LoadObject
			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(*It))
			{
				if (UObject* Obj = LoadObject<UObject>(nullptr, *Value))
				{
					ObjProp->SetObjectPropertyValue(ValuePtr, Obj);
					return true;
				}
			}
			if (OutErrors) OutErrors->Add(FString::Printf(TEXT("Failed to set %s.%s = %s"), *CompName, *Key, *Value));
			return false;
		}
	}

	// === Level 2: Deep struct reflection — search inside struct properties ===
	// Handles nested structs like FBodyInstance (enable_gravity, mass_override_in_kg, etc.)
	for (TFieldIterator<FStructProperty> StructIt(CompTemplate->GetClass()); StructIt; ++StructIt)
	{
		void* StructPtr = StructIt->ContainerPtrToValuePtr<void>(CompTemplate);

		for (TFieldIterator<FProperty> SubIt(StructIt->Struct); SubIt; ++SubIt)
		{
			FString SubName = SubIt->GetName();
			bool bMatch = NormalizeKey(SubName) == NKey;
			if (!bMatch && SubName.StartsWith(TEXT("b")) && SubName.Len() > 1)
				bMatch = NormalizeKey(SubName.Mid(1)) == NKey;

			if (bMatch)
			{
				void* SubValuePtr = SubIt->ContainerPtrToValuePtr<void>(StructPtr);
				if (SubIt->ImportText_Direct(*Value, SubValuePtr, nullptr, PPF_None))
					return true;
				if (OutErrors) OutErrors->Add(FString::Printf(TEXT("Failed to set %s.%s = %s (in struct %s)"), *CompName, *Key, *Value, *StructIt->GetName()));
				return false;
			}
		}
	}

	// Nothing matched
	if (OutWarnings) OutWarnings->Add(FString::Printf(TEXT("Unrecognized property '%s' on component '%s' — ignored"), *Key, *CompName));
	return false;
}

// ============================================================
// FIND BLUEPRINTS
// ============================================================

FString FNwiroIKBlueprintTools::FindBlueprints(const FString& SearchTerm)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		FString Name = Asset.AssetName.ToString();
		FString Path = Asset.GetObjectPathString();

		if (!SearchTerm.IsEmpty() && !Name.Contains(SearchTerm, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("path"), Path);
		Obj->SetStringField(TEXT("package"), Asset.PackageName.ToString());

		// Try to get parent class info from tag
		FString ParentClass;
		if (Asset.GetTagValue(FName("ParentClass"), ParentClass))
		{
			Obj->SetStringField(TEXT("parentClass"), ParentClass);
		}

		Results.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
	Root->SetArrayField(TEXT("blueprints"), Results);
	Root->SetNumberField(TEXT("count"), Results.Num());

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, W);
	return Out;
}

// ============================================================
// READ BLUEPRINT
// ============================================================

FString FNwiroIKBlueprintTools::ReadBlueprint(const FString& ArgsJson)
{
	// Accept either a plain asset path string or a JSON object with assetPath (+ optional graph).
	FString AssetPath;
	FString GraphFilter;
	{
		TSharedPtr<FJsonObject> Parsed;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ArgsJson);
		if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
		{
			AssetPath = Parsed->GetStringField(TEXT("assetPath"));
			if (Parsed->HasField(TEXT("graph")))
				GraphFilter = Parsed->GetStringField(TEXT("graph"));
		}
		else
		{
			AssetPath = ArgsJson;
		}
	}

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
	{
		return TEXT("{\"error\": \"Blueprint not found\"}");
	}

	// If caller asked for a specific function graph, return its full node/pin data.
	if (!GraphFilter.IsEmpty())
	{
		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (Graph && Graph->GetName().Equals(GraphFilter, ESearchCase::IgnoreCase))
			{
				TSharedPtr<FJsonObject> GObj = SerializeGraph(Graph, true);
				if (!GObj.IsValid())
					return TEXT("{\"error\": \"Failed to serialize graph\"}");
				FString Out;
				TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
				FJsonSerializer::Serialize(GObj.ToSharedRef(), W);
				return Out;
			}
		}
		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			if (Graph && Graph->GetName().Equals(GraphFilter, ESearchCase::IgnoreCase))
			{
				TSharedPtr<FJsonObject> GObj = SerializeGraph(Graph, true);
				if (!GObj.IsValid())
					return TEXT("{\"error\": \"Failed to serialize graph\"}");
				FString Out;
				TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
				FJsonSerializer::Serialize(GObj.ToSharedRef(), W);
				return Out;
			}
		}
		return FString::Printf(TEXT("{\"error\": \"Graph not found: %s\"}"), *GraphFilter);
	}

	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
	Root->SetStringField(TEXT("name"), BP->GetName());
	Root->SetStringField(TEXT("path"), BP->GetPathName());

	if (BP->ParentClass)
	{
		Root->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());
	}

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarArr;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> VObj = SerializeVariable(BP, Var.VarName);
		if (VObj.IsValid())
		{
			VarArr.Add(MakeShareable(new FJsonValueObject(VObj.ToSharedRef())));
		}
	}
	Root->SetArrayField(TEXT("variables"), VarArr);

	// Components (from SCS)
	TArray<TSharedPtr<FJsonValue>> CompArr;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* SCSNode : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (SCSNode && SCSNode->ComponentTemplate)
			{
				TSharedPtr<FJsonObject> CObj = SerializeComponent(
					SCSNode->ComponentTemplate, SCSNode->GetVariableName());
				if (CObj.IsValid())
				{
					CompArr.Add(MakeShareable(new FJsonValueObject(CObj.ToSharedRef())));
				}
			}
		}
	}
	Root->SetArrayField(TEXT("components"), CompArr);

	// Functions
	TArray<TSharedPtr<FJsonValue>> FuncArr;
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph)
		{
			TSharedRef<FJsonObject> FObj = MakeShareable(new FJsonObject());
			FObj->SetStringField(TEXT("name"), Graph->GetName());
			FuncArr.Add(MakeShareable(new FJsonValueObject(FObj)));
		}
	}
	Root->SetArrayField(TEXT("functions"), FuncArr);

	// Event Graphs
	TArray<TSharedPtr<FJsonValue>> GraphArr;
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		TSharedPtr<FJsonObject> GObj = SerializeGraph(Graph, true);
		if (GObj.IsValid())
		{
			GraphArr.Add(MakeShareable(new FJsonValueObject(GObj.ToSharedRef())));
		}
	}
	Root->SetArrayField(TEXT("eventGraphs"), GraphArr);

	// Interfaces
	TArray<TSharedPtr<FJsonValue>> IntArr;
	for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
	{
		if (Iface.Interface)
		{
			TSharedRef<FJsonObject> IObj = MakeShareable(new FJsonObject());
			IObj->SetStringField(TEXT("name"), Iface.Interface->GetName());
			IntArr.Add(MakeShareable(new FJsonValueObject(IObj)));
		}
	}
	Root->SetArrayField(TEXT("interfaces"), IntArr);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, W);
	return Out;
}

// ============================================================
// EDIT BLUEPRINT (Main Entry)
// ============================================================

FString FNwiroIKBlueprintTools::EditBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Invalid JSON command\"}");
	}

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));

	// Handle create action: if no "blueprint" but has "name" + "parent_class" or "parentClass", create it
	FString ActionStr = Cmd->GetStringField(TEXT("action"));
	if (BPName.IsEmpty() && (ActionStr.Equals(TEXT("CreateBlueprint"), ESearchCase::IgnoreCase) || Cmd->HasField(TEXT("parent_class")) || Cmd->HasField(TEXT("parentClass"))))
	{
		FString CreateName = Cmd->GetStringField(TEXT("name"));
		if (CreateName.IsEmpty()) CreateName = TEXT("BP_NewBlueprint");

		FString CreateParent = Cmd->GetStringField(TEXT("parent_class"));
		if (CreateParent.IsEmpty()) CreateParent = Cmd->GetStringField(TEXT("parentClass"));
		if (CreateParent.IsEmpty()) CreateParent = TEXT("Actor");

		FString CreatePath = Cmd->GetStringField(TEXT("path"));
		if (CreatePath.IsEmpty()) CreatePath = TEXT("/Game");

		FString CreateJson = FString::Printf(TEXT("{\"name\":\"%s\",\"parentClass\":\"%s\",\"path\":\"%s\"}"), *CreateName, *CreateParent, *CreatePath);
		FString CreateResult = CreateBlueprint(CreateJson);
		BPName = CreateName;

		UBlueprint* CreatedBP = LoadBP(BPName);
		if (CreatedBP)
		{
			TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("blueprint"), CreatedBP->GetName());

			TArray<TSharedPtr<FJsonValue>> MsgArr;
			MsgArr.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("[create] Blueprint '%s' created (parent: %s)"), *CreateName, *CreateParent))));
			Result->SetArrayField(TEXT("messages"), MsgArr);

			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Result, W);
			return Out;
		}
		else
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Failed to create blueprint: %s\"}"), *CreateName);
		}
	}

	if (BPName.IsEmpty())
	{
		return TEXT("{\"success\": false, \"error\": \"Missing 'blueprint' field\"}");
	}

	UBlueprint* BP = LoadBP(BPName);
	if (!BP)
	{
		// Auto-create: use parentClass if specified, otherwise default to Actor
		FString ParentClassName = Cmd->GetStringField(TEXT("parentClass"));
		if (ParentClassName.IsEmpty()) ParentClassName = Cmd->GetStringField(TEXT("parent_class"));
		if (ParentClassName.IsEmpty()) ParentClassName = TEXT("Actor");

		UE_LOG(LogNwiroBP, Log, TEXT("Blueprint '%s' not found, auto-creating with parent '%s'"), *BPName, *ParentClassName);
		FString CreateJson = FString::Printf(TEXT("{\"name\":\"%s\",\"parentClass\":\"%s\"}"), *BPName, *ParentClassName);
		CreateBlueprint(CreateJson);
		BP = LoadBP(BPName);

		if (!BP)
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Failed to create blueprint: %s\"}"), *BPName);
		}
	}

	FString GraphName = Cmd->GetStringField(TEXT("graph"));
	if (GraphName.IsEmpty())
	{
		GraphName = TEXT("EventGraph");
	}

	TArray<FString> Messages;
	bool bAllOk = true;

	// Support "actions" array format: flatten into top-level keys
	// e.g. {"actions":[{"type":"add_nodes","nodes":[...]},{"type":"connect_pins","pins":[...]}]}
	TSet<FString> ActionsFlattenedAliases;
	if (Cmd->HasField(TEXT("actions")))
	{
		const TArray<TSharedPtr<FJsonValue>>* ActionsArr;
		if (Cmd->TryGetArrayField(TEXT("actions"), ActionsArr))
		{
			for (const TSharedPtr<FJsonValue>& ActionVal : *ActionsArr)
			{
				TSharedPtr<FJsonObject> ActionObj = ActionVal->AsObject();
				if (!ActionObj.IsValid()) continue;

				FString ActionType = ActionObj->GetStringField(TEXT("type"));
				if (ActionType.IsEmpty()) continue;

				// Copy all fields from the action into the main Cmd
				for (const auto& Pair : ActionObj->Values)
				{
					if (Pair.Key != TEXT("type"))
					{
						Cmd->SetField(Pair.Key, Pair.Value);
						ActionsFlattenedAliases.Add(Pair.Key);
					}
				}

				// Also set the action type as the key with the array value
				// e.g. type="add_nodes" + nodes=[...] -> add_nodes=[...]
				for (const auto& Pair : ActionObj->Values)
				{
					if (Pair.Key != TEXT("type") && Pair.Value->Type == EJson::Array)
					{
						Cmd->SetField(ActionType, Pair.Value);
						break;
					}
				}
			}
		}
	}

	// ── Alias normalization: remap known LLM shorthand keys to canonical names ─
	TArray<FString> AliasWarnings;
	{
		struct FAliasEntry { const TCHAR* Alias; const TCHAR* Canonical; };
		static const FAliasEntry Aliases[] = {
			{ TEXT("components"),    TEXT("add_components")        },
			{ TEXT("component"),     TEXT("add_components")        },
			{ TEXT("variables"),     TEXT("add_variables")         },
			{ TEXT("variable"),      TEXT("add_variables")         },
			{ TEXT("nodes"),         TEXT("add_nodes")             },
			{ TEXT("node"),          TEXT("add_nodes")             },
			{ TEXT("connections"),   TEXT("connect_pins")          },
			{ TEXT("connection"),    TEXT("connect_pins")          },
			{ TEXT("pin_defaults"),  TEXT("set_pin_defaults")      },
			{ TEXT("defaults"),      TEXT("set_pin_defaults")      },
			{ TEXT("set_defaults"),  TEXT("set_pin_defaults")      },
			{ TEXT("functions"),     TEXT("add_functions")         },
			{ TEXT("function"),      TEXT("add_functions")         },
			{ TEXT("custom_events"), TEXT("add_custom_events")     },
			{ TEXT("events"),        TEXT("add_custom_events")     },
			{ TEXT("dispatchers"),   TEXT("add_event_dispatchers") },
			{ TEXT("interfaces"),    TEXT("add_interfaces")        },
			{ TEXT("interface"),     TEXT("add_interfaces")        },
		};
		for (const FAliasEntry& E : Aliases)
		{
			const TSharedPtr<FJsonValue>* AliasVal = Cmd->Values.Find(E.Alias);
			if (!AliasVal || !AliasVal->IsValid()) continue;

			if (Cmd->HasField(E.Canonical))
			{
				if (ActionsFlattenedAliases.Contains(E.Alias))
				{
					// This exact key was copied by actions flattening — alias is redundant, drop silently
					Cmd->RemoveField(E.Alias);
					continue;
				}
				return FString::Printf(
					TEXT("{\"success\":false,\"error\":\"Conflicting keys: both '%s' and '%s' provided. Use only '%s'.\"}"),
					E.Alias, E.Canonical, E.Canonical);
			}
			if ((*AliasVal)->Type != EJson::Array)
			{
				return FString::Printf(
					TEXT("{\"success\":false,\"error\":\"Key '%s' must be an array (e.g. %s:[{...}]). Got a non-array value.\"}"),
					E.Alias, E.Canonical);
			}
			Cmd->SetField(E.Canonical, *AliasVal);
			Cmd->RemoveField(E.Alias);
			AliasWarnings.Add(FString::Printf(TEXT("Normalized '%s' to '%s'. Use '%s' directly next time."), E.Alias, E.Canonical, E.Canonical));
			UE_LOG(LogNwiroBP, Warning, TEXT("EDIT_BLUEPRINT_ALIAS: '%s' -> '%s'"), E.Alias, E.Canonical);
		}
	}
	// ─────────────────────────────────────────────────────────────────────────

	// ── Clean up action-flattened helper keys that are not canonical ─────────
	// e.g. {type:"connect_pins", pins:[...]} copies "pins" to Cmd but sets canonical
	// "connect_pins". Remove non-canonical leftover keys so rejection does not fire.
	for (const FString& FlatKey : ActionsFlattenedAliases)
	{
		if (!IsKnownEditBlueprintKey(FlatKey))
			Cmd->RemoveField(FlatKey);
	}
	// ─────────────────────────────────────────────────────────────────────────

	// ── Unknown key rejection: surface to LLM so it can self-correct ────────
	{
		TArray<FString> UnknownKeys;
		for (const auto& Pair : Cmd->Values)
		{
			if (!IsKnownEditBlueprintKey(Pair.Key))
				UnknownKeys.Add(Pair.Key);
		}
		if (UnknownKeys.Num() > 0)
		{
			UnknownKeys.Sort();
			FString KeyList = FString::Join(UnknownKeys, TEXT(", "));
			UE_LOG(LogNwiroBP, Warning, TEXT("EDIT_BLUEPRINT_REJECTED unknown keys: [%s]"), *KeyList);
			return FString::Printf(
				TEXT("{\"success\":false,\"error\":\"Unknown edit_blueprint key(s): %s. Valid op keys: add_components, add_variables, add_nodes, connect_pins, set_pin_defaults, add_functions, add_custom_events, add_event_dispatchers, add_interfaces, remove_variables, rename_variables, remove_components, set_component_properties, remove_functions, remove_interfaces, remove_nodes, break_connections\"}"),
				*KeyList);
		}
	}
	// ─────────────────────────────────────────────────────────────────────────

	// Process each operation type
	auto RunOp = [&](const FString& Key, TFunction<FNwiroIKBPResult(const TArray<TSharedPtr<FJsonValue>>&)> Func)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (Cmd->TryGetArrayField(Key, Arr) && Arr)
		{
			FNwiroIKBPResult R = Func(*Arr);
			Messages.Add(FString::Printf(TEXT("[%s] %s"), *Key, *R.Message));
			if (!R.bSuccess) bAllOk = false;
		}
	};

	// Reparent (single string, not array)
	if (Cmd->HasField(TEXT("reparent")))
	{
		FString NewParent = Cmd->GetStringField(TEXT("reparent"));
		FNwiroIKBPResult R = DoReparent(BP, NewParent);
		Messages.Add(FString::Printf(TEXT("[reparent] %s"), *R.Message));
		if (!R.bSuccess) bAllOk = false;
	}

	RunOp(TEXT("add_variables"), [&](const auto& A) { return DoAddVariables(BP, A); });
	RunOp(TEXT("remove_variables"), [&](const auto& A) { return DoRemoveVariables(BP, A); });
	RunOp(TEXT("rename_variables"), [&](const auto& A) { return DoRenameVariables(BP, A); });
	RunOp(TEXT("add_components"), [&](const auto& A) { return DoAddComponents(BP, A); });
	RunOp(TEXT("remove_components"), [&](const auto& A) { return DoRemoveComponents(BP, A); });
	RunOp(TEXT("set_component_properties"), [&](const auto& A) { return DoSetComponentProperties(BP, A); });
	RunOp(TEXT("add_functions"), [&](const auto& A) { return DoAddFunctions(BP, A); });
	RunOp(TEXT("remove_functions"), [&](const auto& A) { return DoRemoveFunctions(BP, A); });
	RunOp(TEXT("add_custom_events"), [&](const auto& A) { return DoAddCustomEvents(BP, A); });
	RunOp(TEXT("add_event_dispatchers"), [&](const auto& A) { return DoAddEventDispatchers(BP, A); });
	RunOp(TEXT("add_interfaces"), [&](const auto& A) { return DoAddInterfaces(BP, A); });
	RunOp(TEXT("remove_interfaces"), [&](const auto& A) { return DoRemoveInterfaces(BP, A); });
	RunOp(TEXT("add_nodes"), [&](const auto& A) { return DoAddNodes(BP, GraphName, A); });
	RunOp(TEXT("remove_nodes"), [&](const auto& A) { return DoRemoveNodes(BP, GraphName, A); });
	RunOp(TEXT("connect_pins"), [&](const auto& A) { return DoConnectPins(BP, GraphName, A); });
	RunOp(TEXT("break_connections"), [&](const auto& A) { return DoBreakConnections(BP, GraphName, A); });
	RunOp(TEXT("set_pin_defaults"), [&](const auto& A) { return DoSetPinDefaults(BP, GraphName, A); });

	// Compile if requested (default: true)
	bool bCompile = true;
	if (Cmd->HasField(TEXT("compile")))
	{
		bCompile = Cmd->GetBoolField(TEXT("compile"));
	}

	if (bCompile)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);

		// Reconstruct Timeline nodes after compile so track pins appear
		for (UEdGraph* G : BP->UbergraphPages)
		{
			if (!G) continue;
			for (UEdGraphNode* N : G->Nodes)
			{
				if (UK2Node_Timeline* TLN = Cast<UK2Node_Timeline>(N))
				{
					TLN->ReconstructNode();
				}
			}
		}

		// Recompile again after reconstruction to pick up new pins
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);

		// Collect compiler errors so the LLM can self-correct
		TArray<FString> CompileErrors;

		// Blueprint-level messages from the compiler log (type mismatches, missing implementations, etc.)
		for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
		{
			if (Msg->GetSeverity() == EMessageSeverity::Error)
				CompileErrors.Add(Msg->ToText().ToString());
		}

		// Node-level errors — include actual pin names so the LLM can wire the correct pin
		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node->ErrorType <= EMessageSeverity::Error && !Node->ErrorMsg.IsEmpty())
				{
					TArray<FString> InPins, OutPins;
					for (UEdGraphPin* Pin : Node->Pins)
					{
						if (!Pin || Pin->PinName.IsNone()) continue;
						(Pin->Direction == EGPD_Input ? InPins : OutPins).Add(Pin->PinName.ToString());
					}
					FString PinInfo;
					if (InPins.Num() > 0 || OutPins.Num() > 0)
						PinInfo = FString::Printf(TEXT(" [pins: inputs=[%s] outputs=[%s]]"),
							*FString::Join(InPins, TEXT(",")), *FString::Join(OutPins, TEXT(",")));
					CompileErrors.AddUnique(FString::Printf(TEXT("Node '%s':%s UE: %s"),
						*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *PinInfo, *Node->ErrorMsg));
				}
			}
		}
		// Apply fix hints to known compiler error patterns
		for (FString& CE : CompileErrors)
		{
			if (CE.Contains(TEXT("Array inputs")) && CE.Contains(TEXT("must have an input wired")))
				CE += TEXT(" Hint: add a MakeArray node of the required element type and connect its output to this array pin.");
			else if (CE.Contains(TEXT("is not a")) && CE.Contains(TEXT("Target")) && CE.Contains(TEXT("must have a connection")))
				CE += TEXT(" Hint: connect this node's Target/self pin to an object of the correct type — do not leave it as blueprint self unless the blueprint inherits from that type.");
		}

		// Also check blueprint-level compiler messages via status
		if (BP->Status == EBlueprintStatus::BS_Error)
		{
			bAllOk = false;
			if (CompileErrors.Num() == 0)
				CompileErrors.Add(TEXT("Blueprint has compile errors — check node connections and pin types"));
			FString ErrorList = FString::Join(CompileErrors, TEXT("; "));
			Messages.Add(FString::Printf(TEXT("[compile] Blueprint compiled with error(s): %s"), *ErrorList));
		}
		else if (BP->Status == EBlueprintStatus::BS_UpToDateWithWarnings)
		{
			FString WarnList = CompileErrors.Num() > 0 ? FString::Join(CompileErrors, TEXT("; ")) : TEXT("check graph for warnings");
			Messages.Add(FString::Printf(TEXT("[compile] Blueprint compiled with warning(s): %s"), *WarnList));
		}
		else
		{
			Messages.Add(TEXT("[compile] Blueprint compiled"));
		}
	}

	// Build result JSON
	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), bAllOk);
	Result->SetStringField(TEXT("blueprint"), BP->GetName());

	TArray<TSharedPtr<FJsonValue>> MsgArr;
	for (const FString& M : Messages)
		MsgArr.Add(MakeShareable(new FJsonValueString(M)));
	Result->SetArrayField(TEXT("messages"), MsgArr);

	if (AliasWarnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& WarnStr : AliasWarnings)
			WarnArr.Add(MakeShareable(new FJsonValueString(WarnStr)));
		Result->SetArrayField(TEXT("warnings"), WarnArr);
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// FIND BLUEPRINT NODES
// ============================================================

FString FNwiroIKBlueprintTools::FindBlueprintNodes(const FString& Query, const FString& BlueprintPath)
{
	UBlueprint* BP = nullptr;
	if (!BlueprintPath.IsEmpty())
	{
		BP = LoadBP(BlueprintPath);
	}

	TArray<TSharedPtr<FJsonValue>> Results;

	const int32 MaxResults = 50;
	TSet<FString> Seen;

	// Tokenize the query so multi-word searches like "Add float" or "Sin math"
	// can hit "Add_DoubleDouble" / "Sin_FloatFloat". ANY token match counts;
	// requiring ALL was too strict because the LLM mixes display/internal names.
	// Also expand float<->double so "float" matches "Double" (UE 5.5+ promoted
	// math operators to double precision).
	TArray<FString> QueryTokens;
	if (!Query.IsEmpty())
	{
		TArray<FString> Raw;
		Query.ParseIntoArray(Raw, TEXT(" "), /*CullEmpty*/ true);
		for (const FString& T : Raw)
		{
			QueryTokens.Add(T);
			if (T.Equals(TEXT("float"), ESearchCase::IgnoreCase))  QueryTokens.Add(TEXT("double"));
			if (T.Equals(TEXT("double"), ESearchCase::IgnoreCase)) QueryTokens.Add(TEXT("float"));
		}
	}

	// Walk every loaded UClass and inspect its declared UFunctions. This is
	// far more reliable than going through FBlueprintActionDatabase, which
	// stores function spawners under several different subclasses and often
	// returns nothing for KismetMathLibrary on a fresh load.
	//
	// We accept any function flagged BlueprintCallable / BlueprintPure /
	// BlueprintEvent. The class name is reported alongside so the LLM knows
	// where to find it (e.g. "KismetMathLibrary").
	const uint64 BPMask = FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent;

	// Score a hit so we can sort later. Higher = more relevant.
	//   +100  function name starts with the query
	//   +50   function name contains the query as a whole token
	//   +10   any token from the (expanded) query is a substring of the function
	//   +20   class is one of the well-known math/system libraries
	auto ScoreHit = [&Query, &QueryTokens](const FString& FuncName, const FString& ClassName) -> int32
	{
		int32 Score = 0;

		if (!Query.IsEmpty())
		{
			if (FuncName.StartsWith(Query, ESearchCase::IgnoreCase)) Score += 100;
			else if (FuncName.Contains(Query, ESearchCase::IgnoreCase)) Score += 50;
		}

		for (const FString& Tok : QueryTokens)
		{
			if (FuncName.Contains(Tok, ESearchCase::IgnoreCase)) { Score += 10; break; }
		}

		if (ClassName == TEXT("KismetMathLibrary")    ||
			ClassName == TEXT("KismetSystemLibrary")  ||
			ClassName == TEXT("KismetStringLibrary")  ||
			ClassName == TEXT("KismetTextLibrary")    ||
			ClassName == TEXT("GameplayStatics"))
		{
			Score += 20;
		}

		return Score;
	};

	struct FHit { FString FuncName; FString ClassName; int32 Score; };
	TArray<FHit> Hits;

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!Class) continue;
		// Skip skeleton/REINST classes — stale duplicates
		if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_"))) continue;

		const FString ClassName = Class->GetName();

		for (TFieldIterator<UFunction> FnIt(Class, EFieldIteratorFlags::ExcludeSuper); FnIt; ++FnIt)
		{
			UFunction* Fn = *FnIt;
			if (!Fn) continue;
			if (!(Fn->FunctionFlags & BPMask)) continue;

			const FString FuncName = Fn->GetName();

			// Function-name only match — class names like "BlueprintFns"
			// were producing false positives for "Print", "Sin", etc.
			if (QueryTokens.Num() > 0)
			{
				bool bAnyTokenHit = false;
				for (const FString& Tok : QueryTokens)
				{
					if (FuncName.Contains(Tok, ESearchCase::IgnoreCase)) { bAnyTokenHit = true; break; }
				}
				if (!bAnyTokenHit) continue;
			}

			const FString DedupeKey = ClassName + TEXT("::") + FuncName;
			if (Seen.Contains(DedupeKey)) continue;
			Seen.Add(DedupeKey);

			Hits.Add({ FuncName, ClassName, ScoreHit(FuncName, ClassName) });
		}
	}

	// Sort by score descending so the most relevant hits come first
	Hits.Sort([](const FHit& A, const FHit& B) { return A.Score > B.Score; });

	const int32 Take = FMath::Min(Hits.Num(), MaxResults);
	for (int32 i = 0; i < Take; ++i)
	{
		TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("function"), Hits[i].FuncName);
		Obj->SetStringField(TEXT("class"), Hits[i].ClassName);
		Results.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	int32 Count = Results.Num();

	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
	Root->SetArrayField(TEXT("nodes"), Results);
	Root->SetNumberField(TEXT("count"), Results.Num());

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, W);
	return Out;
}

// ============================================================
// ASSET LOADING
// ============================================================

UBlueprint* FNwiroIKBlueprintTools::LoadBP(const FString& PathOrName)
{
	// Try direct path first
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *PathOrName);
	if (BP) return BP;

	// Try with /Game/ prefix
	if (!PathOrName.StartsWith(TEXT("/")))
	{
		FString FullPath = TEXT("/Game/") + PathOrName;
		BP = LoadObject<UBlueprint>(nullptr, *FullPath);
		if (BP) return BP;
	}

	// Search by name in asset registry
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	// Exact name match
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(PathOrName, ESearchCase::IgnoreCase))
		{
			return Cast<UBlueprint>(Asset.GetAsset());
		}
	}

	// Partial name match (e.g., "ThirdPersonCharacter" matches "BP_ThirdPersonCharacter")
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Contains(PathOrName, ESearchCase::IgnoreCase))
		{
			return Cast<UBlueprint>(Asset.GetAsset());
		}
	}

	// Smart fallback: if the name contains "Character" or "Pawn", find any character blueprint
	if (PathOrName.Contains(TEXT("Character"), ESearchCase::IgnoreCase) || PathOrName.Contains(TEXT("Pawn"), ESearchCase::IgnoreCase))
	{
		UBlueprint* BestMatch = nullptr;
		for (const FAssetData& Asset : Assets)
		{
			FString Name = Asset.AssetName.ToString();
			// Skip GameMode, Controller, etc.
			if (Name.Contains(TEXT("GameMode")) || Name.Contains(TEXT("Controller")) || Name.Contains(TEXT("HUD")))
			{
				continue;
			}

			UBlueprint* CandidateBP = Cast<UBlueprint>(Asset.GetAsset());
			if (CandidateBP && CandidateBP->ParentClass)
			{
				if (CandidateBP->ParentClass->IsChildOf(ACharacter::StaticClass()) ||
					CandidateBP->ParentClass->IsChildOf(APawn::StaticClass()))
				{
					UE_LOG(LogNwiroBP, Log, TEXT("Smart fallback: '%s' not found, using '%s' instead"), *PathOrName, *Name);
					BestMatch = CandidateBP;
					break;
				}
			}
		}
		if (BestMatch) return BestMatch;
	}

	return nullptr;
}

// ============================================================
// GRAPH HELPERS
// ============================================================

UEdGraph* FNwiroIKBlueprintTools::FindGraph(UBlueprint* BP, const FString& GraphName)
{
	if (!BP) return nullptr;

	// Check event graphs
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Check function graphs
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	return nullptr;
}

// Strip everything that isn't a letter or digit and lowercase the rest, so
// "Event BeginPlay", "event_begin_play", "EventBeginPlay" and "eventbeginplay"
// all collapse to the same canonical key.
static FString NormalizeRefKey(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	for (TCHAR C : In)
	{
		if (FChar::IsAlnum(C)) Out.AppendChar(FChar::ToLower(C));
	}
	return Out;
}

UEdGraphNode* FNwiroIKBlueprintTools::FindNodeByRef(UEdGraph* Graph, const FString& Ref)
{
	if (!Graph) return nullptr;

	// 1. Exact session reference (set by add_nodes via the LLM-supplied ref).
	if (const FNwiroIKNodeRef* Found = NodeRefs.Find(Ref))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Found->NodeGuid) return Node;
		}
	}

	// 2. Normalized session reference — handles `printString` vs `PrintString` vs `print_string`.
	const FString NormRef = NormalizeRefKey(Ref);
	if (!NormRef.IsEmpty())
	{
		for (const auto& Pair : NodeRefs)
		{
			if (NormalizeRefKey(Pair.Key) == NormRef)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && Node->NodeGuid == Pair.Value.NodeGuid) return Node;
				}
			}
		}
	}

	// 3. GUID — accept any of the FGuid string formats UE supports.
	{
		FGuid TestGuid;
		if (FGuid::Parse(Ref, TestGuid))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == TestGuid) return Node;
			}
		}
	}

	// 4. Match by normalized node title (covers "Event BeginPlay" / "Print String" / "Delay" etc.).
	if (!NormRef.IsEmpty())
	{
		// 4a. exact normalized title equality
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const FString Title = Node->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
			if (NormalizeRefKey(Title) == NormRef) return Node;
		}
		// 4b. partial substring match against full title
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const FString FullTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (NormalizeRefKey(FullTitle).Contains(NormRef)) return Node;
		}
	}

	return nullptr;
}

UEdGraphPin* FNwiroIKBlueprintTools::FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Dir)
{
	if (!Node) return nullptr;

	// Common synonyms the LLM mixes up. Map every input alias to a list of
	// candidate canonical names so we try all of them.
	auto Aliases = [](const FString& In) -> TArray<FString>
	{
		const FString N = NormalizeRefKey(In); // strip non-alnum, lowercase
		TArray<FString> Out = { In };
		if (N == TEXT("execute") || N == TEXT("exec") || N == TEXT("in") || N == TEXT("input"))
		{
			Out.Append({ TEXT("execute"), TEXT("exec"), TEXT("then") });
		}
		else if (N == TEXT("then") || N == TEXT("next") || N == TEXT("out") || N == TEXT("output") || N == TEXT("completed"))
		{
			Out.Append({ TEXT("then"), TEXT("Completed") });
		}
		else if (N == TEXT("self") || N == TEXT("target"))
		{
			Out.Append({ TEXT("self"), TEXT("Target") });
		}
		else if (N == TEXT("returnvalue") || N == TEXT("return") || N == TEXT("result"))
		{
			Out.Append({ TEXT("ReturnValue") });
		}
		return Out;
	};
	const TArray<FString> Candidates = Aliases(PinName);

	auto MatchesNorm = [](const FString& Pin, const FString& Wanted) -> bool
	{
		return NormalizeRefKey(Pin) == NormalizeRefKey(Wanted);
	};

	// 1. Exact / normalized name or friendly-name match across all candidate aliases.
	for (const FString& Cand : Candidates)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			const bool bMatch = MatchesNorm(Pin->PinName.ToString(), Cand)
				|| MatchesNorm(Pin->PinFriendlyName.ToString(), Cand);
			if (bMatch && (Dir == EGPD_MAX || Pin->Direction == Dir)) return Pin;
		}
	}

	// 2. Substring fallback (partial match) using the original input only.
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;
		if (Pin->PinName.ToString().Contains(PinName, ESearchCase::IgnoreCase))
		{
			if (Dir == EGPD_MAX || Pin->Direction == Dir) return Pin;
		}
	}

	return nullptr;
}

void FNwiroIKBlueprintTools::StoreNodeRef(const FString& Ref, UEdGraphNode* Node, const FString& GraphName)
{
	if (!Ref.IsEmpty() && Node)
	{
		FNwiroIKNodeRef NR;
		NR.NodeGuid = Node->NodeGuid;
		NR.GraphName = GraphName;
		NodeRefs.Add(Ref, NR);
	}
}

void FNwiroIKBlueprintTools::ClearNodeRefs()
{
	NodeRefs.Empty();
}

// ============================================================
// TYPE PARSING
// ============================================================

FEdGraphPinType FNwiroIKBlueprintTools::ParsePinType(const TSharedPtr<FJsonObject>& TypeObj)
{
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; // default

	if (!TypeObj.IsValid()) return PinType;

	FString Base = TypeObj->GetStringField(TEXT("base")).ToLower();

	if (Base == TEXT("bool") || Base == TEXT("boolean"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Base == TEXT("byte"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (Base == TEXT("int") || Base == TEXT("integer") || Base == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Base == TEXT("int64"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Base == TEXT("float"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = TEXT("float");
	}
	else if (Base == TEXT("double"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = TEXT("double");
	}
	else if (Base == TEXT("string") || Base == TEXT("fstring"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Base == TEXT("name") || Base == TEXT("fname"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Base == TEXT("text") || Base == TEXT("ftext"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Base == TEXT("vector") || Base == TEXT("fvector"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (Base == TEXT("rotator") || Base == TEXT("frotator"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (Base == TEXT("transform") || Base == TEXT("ftransform"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (Base == TEXT("linearcolor") || Base == TEXT("color"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else if (Base == TEXT("object"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		FString SubType = TypeObj->GetStringField(TEXT("subtype"));
		if (!SubType.IsEmpty())
		{
			UClass* ObjClass = FindFirstObject<UClass>(*SubType);
			if (!ObjClass)
				ObjClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *SubType));
			if (!ObjClass)
				ObjClass = StaticLoadClass(UObject::StaticClass(), nullptr, *SubType);
			if (ObjClass)
			{
				PinType.PinSubCategoryObject = ObjClass;
			}
		}
	}
	else if (Base == TEXT("class"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		FString SubType = TypeObj->GetStringField(TEXT("subtype"));
		if (!SubType.IsEmpty())
		{
			UClass* ObjClass = FindFirstObject<UClass>(*SubType);
			if (!ObjClass)
				ObjClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *SubType));
			if (!ObjClass)
				ObjClass = StaticLoadClass(UObject::StaticClass(), nullptr, *SubType);
			if (ObjClass)
				PinType.PinSubCategoryObject = ObjClass;
		}
	}
	else if (Base == TEXT("struct"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		FString SubType = TypeObj->GetStringField(TEXT("subtype"));
		if (!SubType.IsEmpty())
		{
			UScriptStruct* Struct = FindFirstObject<UScriptStruct>( *SubType);
			if (Struct)
			{
				PinType.PinSubCategoryObject = Struct;
			}
		}
	}
	else if (Base == TEXT("enum"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		FString SubType = TypeObj->GetStringField(TEXT("subtype"));
		if (!SubType.IsEmpty())
		{
			UEnum* Enum = FindFirstObject<UEnum>( *SubType);
			if (Enum)
			{
				PinType.PinSubCategoryObject = Enum;
			}
		}
	}

	// Container type
	FString Container = TypeObj->GetStringField(TEXT("container")).ToLower();
	if (Container == TEXT("array"))
	{
		PinType.ContainerType = EPinContainerType::Array;
	}
	else if (Container == TEXT("set"))
	{
		PinType.ContainerType = EPinContainerType::Set;
	}
	else if (Container == TEXT("map"))
	{
		PinType.ContainerType = EPinContainerType::Map;
	}

	return PinType;
}

// ============================================================
// ADD VARIABLES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddVariables(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Added = 0;
	TArray<FString> Errors;
	TArray<FString> Details;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString VarName = Obj->GetStringField(TEXT("name"));
		if (VarName.IsEmpty()) { Errors.Add(TEXT("Missing variable name")); continue; }

		FEdGraphPinType PinType;
		const TSharedPtr<FJsonObject>* TypeObj;
		if (Obj->TryGetObjectField(TEXT("type"), TypeObj))
		{
			PinType = ParsePinType(*TypeObj);
		}
		else
		{
			// Default: try string type field
			FString TypeStr = Obj->GetStringField(TEXT("type"));
			if (!TypeStr.IsEmpty())
			{
				TSharedPtr<FJsonObject> SimpleType = MakeShareable(new FJsonObject());
				SimpleType->SetStringField(TEXT("base"), TypeStr);
				FString SubType = GetFieldNormalized(Obj, TEXT("subtype"));
				if (SubType.IsEmpty() && (TypeStr == TEXT("object") || TypeStr == TEXT("class") || TypeStr == TEXT("struct") || TypeStr == TEXT("enum")))
					SubType = GetFieldNormalized(Obj, TEXT("objectclass")); // covers object_class / objectClass / ObjectClass
				if (!SubType.IsEmpty()) SimpleType->SetStringField(TEXT("subtype"), SubType);
				PinType = ParsePinType(SimpleType);
			}
		}

		FName VarFName(*VarName);

		// Check if variable already exists
		bool bExists = false;
		for (const FBPVariableDescription& Existing : BP->NewVariables)
		{
			if (Existing.VarName == VarFName)
			{
				bExists = true;
				break;
			}
		}
		if (bExists) { Errors.Add(FString::Printf(TEXT("Variable '%s' already exists. To change its type, call remove_variables with this name first, then re-add with the correct subtype."), *VarName)); continue; }

		bool bAdded = FBlueprintEditorUtils::AddMemberVariable(BP, VarFName, PinType);
		if (!bAdded) { Errors.Add(FString::Printf(TEXT("Failed to add variable '%s'"), *VarName)); continue; }

		// Set default value if provided
		FString DefaultVal = Obj->GetStringField(TEXT("default"));
		if (!DefaultVal.IsEmpty())
		{
			int32 DefaultIdx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarFName);
			if (DefaultIdx != INDEX_NONE)
			{
				BP->NewVariables[DefaultIdx].DefaultValue = DefaultVal;
			}
		}

		// Set flags
		if (Obj->HasField(TEXT("replicated")) && Obj->GetBoolField(TEXT("replicated")))
		{
			int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarFName);
			if (VarIdx != INDEX_NONE)
			{
				BP->NewVariables[VarIdx].PropertyFlags |= CPF_Net;
			}
		}

		if (Obj->HasField(TEXT("expose_on_spawn")) && Obj->GetBoolField(TEXT("expose_on_spawn")))
		{
			// Expose on Spawn requires Instance Editable — set both flags
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarFName, nullptr,
				FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
			// Set CPF_Edit (Instance Editable) which is required for Expose on Spawn to work
			int32 ExposeIdx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarFName);
			if (ExposeIdx != INDEX_NONE)
			{
				BP->NewVariables[ExposeIdx].PropertyFlags |= CPF_Edit;
				BP->NewVariables[ExposeIdx].PropertyFlags &= ~CPF_DisableEditOnInstance;
			}
		}

		if (Obj->HasField(TEXT("save_game")) && Obj->GetBoolField(TEXT("save_game")))
		{
			int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarFName);
			if (VarIdx != INDEX_NONE)
			{
				BP->NewVariables[VarIdx].PropertyFlags |= CPF_SaveGame;
			}
		}

		if (Obj->HasField(TEXT("category")))
		{
			FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, VarFName, nullptr,
				FText::FromString(Obj->GetStringField(TEXT("category"))));
		}

		if (Obj->HasField(TEXT("tooltip")))
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarFName, nullptr,
				FBlueprintMetadata::MD_Tooltip, Obj->GetStringField(TEXT("tooltip")));
		}

		Added++;
		{
			const FString Cat = PinType.PinCategory.ToString();
			const bool bIsRef = (PinType.PinCategory == UEdGraphSchema_K2::PC_Object  ||
			                     PinType.PinCategory == UEdGraphSchema_K2::PC_Class   ||
			                     PinType.PinCategory == UEdGraphSchema_K2::PC_Struct  ||
			                     PinType.PinCategory == UEdGraphSchema_K2::PC_Enum);
			if (bIsRef)
			{
				FString Requested = GetFieldNormalized(Obj, TEXT("subtype"));
				if (Requested.IsEmpty()) Requested = GetFieldNormalized(Obj, TEXT("objectclass"));
				if (Requested.IsEmpty()) Requested = GetFieldNormalized(Obj, TEXT("objecttype"));
				if (PinType.PinSubCategoryObject.IsValid())
				{
					const FString Resolved = PinType.PinSubCategoryObject->GetName();
					const FString Status = (Requested.IsEmpty() || Requested.Equals(Resolved, ESearchCase::IgnoreCase))
					                       ? TEXT("resolved") : TEXT("fallback");
					Details.Add(FString::Printf(TEXT("'%s' [%s] %s:%s"), *VarName, *Status, *Cat, *Resolved));
				}
				else
				{
					const FString Req = Requested.IsEmpty() ? TEXT("none") : Requested;
					Errors.Add(FString::Printf(TEXT("'%s' [not_resolved] %s subtype:'%s' unrecognized — stored as UObject; re-add with subtype:'%s'"), *VarName, *Cat, *Req, *Req));
				}
			}
			else
			{
				Details.Add(FString::Printf(TEXT("'%s' [resolved] %s"), *VarName, *Cat));
			}
		}
		UE_LOG(LogNwiroBP, Log, TEXT("Added variable: %s"), *VarName);
	}

	FString Msg = FString::Printf(TEXT("Added %d variable(s)"), Added);
	if (Details.Num() > 0) Msg += TEXT(": ") + FString::Join(Details, TEXT(", "));
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));

	return Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// REMOVE VARIABLES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoRemoveVariables(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Removed = 0;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		FString VarName;
		if (Item->Type == EJson::String)
		{
			VarName = Item->AsString();
		}
		else if (Item->Type == EJson::Object && Item->AsObject().IsValid())
		{
			VarName = Item->AsObject()->GetStringField(TEXT("name"));
		}

		if (!VarName.IsEmpty())
		{
			FBlueprintEditorUtils::RemoveMemberVariable(BP, FName(*VarName));
			Removed++;
		}
	}

	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Removed %d variable(s)"), Removed));
}

// ============================================================
// ADD COMPONENTS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddComponents(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	if (!BP->SimpleConstructionScript)
	{
		return FNwiroIKBPResult::Fail(TEXT("Blueprint has no SimpleConstructionScript"));
	}

	int32 Added = 0;
	TArray<FString> Errors;
	TArray<FString> Warnings;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString CompName = Obj->GetStringField(TEXT("name"));
		FString ClassName = Obj->GetStringField(TEXT("class"));
		if (ClassName.IsEmpty()) ClassName = Obj->GetStringField(TEXT("type"));
		FString ParentName = Obj->GetStringField(TEXT("parent"));

		if (CompName.IsEmpty() || ClassName.IsEmpty())
		{
			Errors.Add(TEXT("Component needs both 'name' and 'class'"));
			continue;
		}

		// Find the component class — try multiple name variations and KEEP
		// trying if the first hit isn't an ActorComponent. Without this, asking
		// for "StaticMesh" returned the asset class UStaticMesh (which exists
		// but isn't a component) and silently dropped the request.
		UClass* CompClass = nullptr;
		const TArray<FString> ClassTries = {
			ClassName,
			TEXT("U") + ClassName,
			ClassName + TEXT("Component"),
			TEXT("U") + ClassName + TEXT("Component"),
		};
		for (const FString& Try : ClassTries)
		{
			UClass* Candidate = FindFirstObject<UClass>(*Try);
			if (Candidate && Candidate->IsChildOf(UActorComponent::StaticClass()))
			{
				CompClass = Candidate;
				break;
			}
		}

		if (!CompClass)
		{
			Errors.Add(FString::Printf(TEXT("Component class not found: %s (tried: %s)"), *ClassName, *FString::Join(ClassTries, TEXT(", "))));
			continue;
		}

		USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, FName(*CompName));
		if (!NewNode)
		{
			Errors.Add(FString::Printf(TEXT("Failed to create SCS node for: %s"), *CompName));
			continue;
		}

		// Check if makeRoot requested — if so, skip normal attachment
		bool bWantRoot = false;
		if (Obj->HasField(TEXT("makeRoot")))
		{
			TSharedPtr<FJsonValue> MRVal = Obj->TryGetField(TEXT("makeRoot"));
			if (MRVal.IsValid())
			{
				if (MRVal->Type == EJson::Boolean) bWantRoot = MRVal->AsBool();
				else if (MRVal->Type == EJson::String) bWantRoot = MRVal->AsString().Equals(TEXT("true"), ESearchCase::IgnoreCase);
			}
		}

		if (bWantRoot)
		{
			// Remove existing DefaultSceneRoot and set this as root
			TArray<USCS_Node*> OldRoots = BP->SimpleConstructionScript->GetRootNodes();
			for (USCS_Node* OldRoot : OldRoots)
			{
				if (OldRoot->ComponentTemplate && OldRoot->ComponentTemplate->GetFName() == TEXT("DefaultSceneRoot"))
				{
					TArray<USCS_Node*> OldChildren = OldRoot->GetChildNodes();
					for (USCS_Node* Child : OldChildren)
					{
						OldRoot->RemoveChildNode(Child);
						NewNode->AddChildNode(Child);
					}
					BP->SimpleConstructionScript->RemoveNode(OldRoot);
				}
			}
			BP->SimpleConstructionScript->AddNode(NewNode);
			goto NodeAttached;
		}

		// Attach to parent or root
		if (!ParentName.IsEmpty())
		{
			for (USCS_Node* Existing : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Existing && Existing->GetVariableName().ToString().Equals(ParentName, ESearchCase::IgnoreCase))
				{
					Existing->AddChildNode(NewNode);
					goto NodeAttached;
				}
			}
			// Parent not found, attach to root
		}

		// Default: attach to existing SCS root, or native root component
		{
			const TArray<USCS_Node*>& RootNodes = BP->SimpleConstructionScript->GetRootNodes();
			bool bAttached = false;

			// Try SCS root nodes first
			if (RootNodes.Num() > 0 && CompClass->IsChildOf(USceneComponent::StaticClass()))
			{
				RootNodes[0]->AddChildNode(NewNode);
				bAttached = true;
			}

			// If no SCS root, attach to the native/inherited default scene root
			if (!bAttached && CompClass->IsChildOf(USceneComponent::StaticClass()))
			{
				USCS_Node* DefaultRoot = BP->SimpleConstructionScript->GetDefaultSceneRootNode();
				if (DefaultRoot)
				{
					DefaultRoot->AddChildNode(NewNode);
					bAttached = true;
				}
			}

			// Fallback: set as root node in SCS (attaches to native root automatically)
			if (!bAttached)
			{
				BP->SimpleConstructionScript->AddNode(NewNode);
			}
		}

	NodeAttached:
		// Apply inline properties on the component template
		if (UActorComponent* CompTemplate = NewNode->ComponentTemplate)
		{
			static const TSet<FString> ReservedKeys = { TEXT("name"), TEXT("class"), TEXT("type"), TEXT("parent"), TEXT("makeroot"), TEXT("isroot") };

			// Helper: iterate one JSON object and call ApplyComponentProperty for each key/value pair.
			auto ProcessProps = [&](const TSharedPtr<FJsonObject>& PropsObj)
			{
				for (const auto& Pair : PropsObj->Values)
				{
					if (ReservedKeys.Contains(NormalizeKey(Pair.Key))) continue;

					FString Value;
					if (Pair.Value->Type == EJson::String)       Value = Pair.Value->AsString();
					else if (Pair.Value->Type == EJson::Boolean)  Value = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
					else if (Pair.Value->Type == EJson::Number)   Value = FString::SanitizeFloat(Pair.Value->AsNumber());
					else continue; // skip nested objects/arrays at this level

					ApplyComponentProperty(CompTemplate, Pair.Key, Value, CompName, nullptr, &Warnings);
				}
			};

			// Flat top-level properties (e.g. static_mesh="..." at same level as name/type)
			ProcessProps(Obj);

			// Nested "properties" sub-object
			// e.g. {"name":"CubeMesh","type":"StaticMeshComponent","properties":{"static_mesh":"..."}}
			const TSharedPtr<FJsonObject>* NestedProps;
			if (Obj->TryGetObjectField(TEXT("properties"), NestedProps))
				ProcessProps(*NestedProps);
		}

		Added++;
		UE_LOG(LogNwiroBP, Log, TEXT("Added component: %s (%s)"), *CompName, *ClassName);
	}

	FString Msg = FString::Printf(TEXT("Added %d component(s)"), Added);
	if (Warnings.Num() > 0)
	{
		Msg += TEXT(". Warnings: ") + FString::Join(Warnings, TEXT("; "));
	}
	if (Errors.Num() > 0)
	{
		Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));
	}

	return Added > 0 || Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// REMOVE COMPONENTS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoRemoveComponents(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	if (!BP->SimpleConstructionScript) return FNwiroIKBPResult::Fail(TEXT("No SCS"));

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		FString CompName;
		if (Item->Type == EJson::String)
		{
			CompName = Item->AsString();
		}
		else if (Item->Type == EJson::Object && Item->AsObject().IsValid())
		{
			CompName = Item->AsObject()->GetStringField(TEXT("name"));
		}

		if (CompName.IsEmpty()) continue;

		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(CompName, ESearchCase::IgnoreCase))
			{
				BP->SimpleConstructionScript->RemoveNode(Node);
				Removed++;
				break;
			}
		}
	}

	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Removed %d component(s)"), Removed));
}

// ============================================================
// ADD FUNCTIONS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddFunctions(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Added = 0;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString FuncName = Obj->GetStringField(TEXT("name"));
		if (FuncName.IsEmpty()) { Errors.Add(TEXT("Missing function name")); continue; }

		// Check if function already exists
		if (FindGraph(BP, FuncName))
		{
			Errors.Add(FString::Printf(TEXT("Function '%s' already exists"), *FuncName));
			continue;
		}

		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			BP, FName(*FuncName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

		if (!NewGraph) { Errors.Add(FString::Printf(TEXT("Failed to create function '%s'"), *FuncName)); continue; }

		FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, true, nullptr);

		// Set pure flag
		if (Obj->HasField(TEXT("pure")) && Obj->GetBoolField(TEXT("pure")))
		{
			// Find the entry node and set pure flag
			for (UEdGraphNode* Node : NewGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					Entry->SetExtraFlags(Entry->GetExtraFlags() | FUNC_BlueprintPure);
					break;
				}
			}
		}

		// Add input parameters
		const TArray<TSharedPtr<FJsonValue>>* Inputs;
		if (Obj->TryGetArrayField(TEXT("inputs"), Inputs))
		{
			for (const TSharedPtr<FJsonValue>& InputVal : *Inputs)
			{
				const TSharedPtr<FJsonObject>& InputObj = InputVal->AsObject();
				if (!InputObj.IsValid()) continue;

				FString ParamName = InputObj->GetStringField(TEXT("name"));
				FEdGraphPinType ParamType;

				const TSharedPtr<FJsonObject>* ParamTypeObj;
				if (InputObj->TryGetObjectField(TEXT("type"), ParamTypeObj))
				{
					ParamType = ParsePinType(*ParamTypeObj);
				}
				else
				{
					FString TypeStr = InputObj->GetStringField(TEXT("type"));
					TSharedPtr<FJsonObject> SimpleType = MakeShareable(new FJsonObject());
					SimpleType->SetStringField(TEXT("base"), TypeStr);
					ParamType = ParsePinType(SimpleType);
				}

				// Add pin to function entry node
				for (UEdGraphNode* Node : NewGraph->Nodes)
				{
					if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
					{
						TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo());
						PinInfo->PinName = FName(*ParamName);
						PinInfo->PinType = ParamType;
						Entry->UserDefinedPins.Add(PinInfo);
						Entry->ReconstructNode();
						break;
					}
				}
			}
		}

		// Add output parameters
		const TArray<TSharedPtr<FJsonValue>>* Outputs;
		if (Obj->TryGetArrayField(TEXT("outputs"), Outputs))
		{
			for (const TSharedPtr<FJsonValue>& OutputVal : *Outputs)
			{
				const TSharedPtr<FJsonObject>& OutputObj = OutputVal->AsObject();
				if (!OutputObj.IsValid()) continue;

				FString ParamName = OutputObj->GetStringField(TEXT("name"));
				FEdGraphPinType ParamType;

				const TSharedPtr<FJsonObject>* ParamTypeObj;
				if (OutputObj->TryGetObjectField(TEXT("type"), ParamTypeObj))
				{
					ParamType = ParsePinType(*ParamTypeObj);
				}
				else
				{
					FString TypeStr = OutputObj->GetStringField(TEXT("type"));
					TSharedPtr<FJsonObject> SimpleType = MakeShareable(new FJsonObject());
					SimpleType->SetStringField(TEXT("base"), TypeStr);
					ParamType = ParsePinType(SimpleType);
				}

				// Find or create result node
				UK2Node_FunctionResult* ResultNode = nullptr;
				for (UEdGraphNode* Node : NewGraph->Nodes)
				{
					ResultNode = Cast<UK2Node_FunctionResult>(Node);
					if (ResultNode) break;
				}

				if (!ResultNode)
				{
					FGraphNodeCreator<UK2Node_FunctionResult> Creator(*NewGraph);
					ResultNode = Creator.CreateNode();
					ResultNode->NodePosX = 400;
					ResultNode->NodePosY = 0;
					Creator.Finalize();
				}

				if (ResultNode)
				{
					TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo());
					PinInfo->PinName = FName(*ParamName);
					PinInfo->PinType = ParamType;
					ResultNode->UserDefinedPins.Add(PinInfo);
					ResultNode->ReconstructNode();
				}
			}
		}

		Added++;
		UE_LOG(LogNwiroBP, Log, TEXT("Added function: %s"), *FuncName);
	}

	FString Msg = FString::Printf(TEXT("Added %d function(s)"), Added);
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));

	return Added > 0 || Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// ADD CUSTOM EVENTS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddCustomEvents(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	UEdGraph* EventGraph = nullptr;
	for (UEdGraph* G : BP->UbergraphPages)
	{
		if (G && G->GetName() == TEXT("EventGraph"))
		{
			EventGraph = G;
			break;
		}
	}

	if (!EventGraph && BP->UbergraphPages.Num() > 0)
	{
		EventGraph = BP->UbergraphPages[0];
	}

	if (!EventGraph)
	{
		return FNwiroIKBPResult::Fail(TEXT("No event graph found"));
	}

	int32 Added = 0;
	int32 YOffset = 0;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString EventName = Obj->GetStringField(TEXT("name"));
		FString Ref = Obj->GetStringField(TEXT("ref"));
		if (EventName.IsEmpty()) continue;

		FGraphNodeCreator<UK2Node_CustomEvent> Creator(*EventGraph);
		UK2Node_CustomEvent* EventNode = Creator.CreateNode();
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->NodePosX = 0;
		EventNode->NodePosY = YOffset;
		Creator.Finalize();

		// Add parameters
		const TArray<TSharedPtr<FJsonValue>>* Params;
		if (Obj->TryGetArrayField(TEXT("params"), Params))
		{
			for (const TSharedPtr<FJsonValue>& PVal : *Params)
			{
				const TSharedPtr<FJsonObject>& PObj = PVal->AsObject();
				if (!PObj.IsValid()) continue;

				FString ParamName = PObj->GetStringField(TEXT("name"));
				FEdGraphPinType ParamType;

				const TSharedPtr<FJsonObject>* TypeObj;
				if (PObj->TryGetObjectField(TEXT("type"), TypeObj))
				{
					ParamType = ParsePinType(*TypeObj);
				}
				else
				{
					TSharedPtr<FJsonObject> SimpleType = MakeShareable(new FJsonObject());
					SimpleType->SetStringField(TEXT("base"), PObj->GetStringField(TEXT("type")));
					ParamType = ParsePinType(SimpleType);
				}

				TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo());
				PinInfo->PinName = FName(*ParamName);
				PinInfo->PinType = ParamType;
				EventNode->UserDefinedPins.Add(PinInfo);
			}
			EventNode->ReconstructNode();
		}

		// Set replication flags
		FString Replication = Obj->GetStringField(TEXT("replication")).ToLower();
		if (Replication == TEXT("multicast"))
		{
			EventNode->FunctionFlags |= FUNC_NetMulticast;
		}
		else if (Replication == TEXT("server"))
		{
			EventNode->FunctionFlags |= FUNC_NetServer;
		}
		else if (Replication == TEXT("client"))
		{
			EventNode->FunctionFlags |= FUNC_NetClient;
		}

		if (Obj->HasField(TEXT("reliable")) && Obj->GetBoolField(TEXT("reliable")))
		{
			EventNode->FunctionFlags |= FUNC_NetReliable;
		}

		StoreNodeRef(Ref.IsEmpty() ? EventName : Ref, EventNode, EventGraph->GetName());
		YOffset += 200;
		Added++;
		UE_LOG(LogNwiroBP, Log, TEXT("Added custom event: %s"), *EventName);
	}

	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Added %d custom event(s)"), Added));
}

// ============================================================
// ADD NODES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddNodes(UBlueprint* BP, const FString& GraphName, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph)
	{
		return FNwiroIKBPResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	int32 Added = 0;
	int32 NodeX = 300;
	int32 NodeY = 0;
	TArray<FString> Errors;
	TArray<FString> NodeDetails;

	int32 Skipped = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		// Accept all the field names the LLM tends to invent for the node-type
		// field. We've now seen `type`, `node`, `nodeType`, `node_type`, `kind`.
		FString NodeType;
		const TArray<FString> NodeTypeKeys = {
			TEXT("type"), TEXT("node"), TEXT("nodeType"), TEXT("node_type"),
			TEXT("nodetype"), TEXT("kind"), TEXT("nodeClass"), TEXT("class"),
		};
		for (const FString& Key : NodeTypeKeys)
		{
			if (Obj->HasField(Key)) { NodeType = Obj->GetStringField(Key); break; }
		}

		// Support "ref", "id" AND "name" — the LLM frequently uses "name" as
		// the user-facing identifier instead of "ref".
		FString Ref;
		if (Obj->HasField(TEXT("ref")))       Ref = Obj->GetStringField(TEXT("ref"));
		else if (Obj->HasField(TEXT("id")))   Ref = Obj->GetStringField(TEXT("id"));
		else if (Obj->HasField(TEXT("name")) && !NodeType.Equals(TEXT("Event"), ESearchCase::IgnoreCase) && !NodeType.Equals(TEXT("CustomEvent"), ESearchCase::IgnoreCase))
		{
			// "name" doubles as the event name for Event/CustomEvent — only
			// reuse it as a ref for non-event nodes.
			Ref = Obj->GetStringField(TEXT("name"));
		}
		int32 PosX = Obj->HasField(TEXT("x")) ? (int32)Obj->GetNumberField(TEXT("x")) : NodeX;
		int32 PosY = Obj->HasField(TEXT("y")) ? (int32)Obj->GetNumberField(TEXT("y")) : NodeY;

		// Dedup: if a node with this ref already exists in the session, OR
		// (for events) if a built-in event of the same name already exists in
		// the graph, skip creation. This prevents partial-failure retries from
		// piling up duplicate Tick / BeginPlay / Multiply nodes.
		if (!Ref.IsEmpty() && FindNodeByRef(Graph, Ref))
		{
			Skipped++;
			continue;
		}
		if (NodeType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			FString EventName = Obj->HasField(TEXT("event")) ? Obj->GetStringField(TEXT("event")) : Obj->GetStringField(TEXT("name"));
			if (!EventName.IsEmpty())
			{
				// Strip "event"/"receive" prefixes for the dedup check.
				FString Norm = NormalizeRefKey(EventName);
				if (Norm.StartsWith(TEXT("receive"))) Norm = Norm.RightChop(7);
				if (Norm.StartsWith(TEXT("event"))) Norm = Norm.RightChop(5);

				bool bExists = false;
				for (UEdGraph* SearchGraph : BP->UbergraphPages)
				{
					if (!SearchGraph) continue;
					for (UEdGraphNode* Existing : SearchGraph->Nodes)
					{
						UK2Node_Event* Evt = Cast<UK2Node_Event>(Existing);
						if (!Evt) continue;
						FString FuncNorm = NormalizeRefKey(Evt->GetFunctionName().ToString());
						if (FuncNorm.StartsWith(TEXT("receive"))) FuncNorm = FuncNorm.RightChop(7);
						if (FuncNorm == Norm)
						{
							bExists = true;
							// Still register a session ref pointing to the existing node
							// so subsequent connect_pins can find it via the user's ref.
							if (!Ref.IsEmpty()) StoreNodeRef(Ref, Evt, SearchGraph->GetName());
							break;
						}
					}
					if (bExists) break;
				}
				if (bExists)
				{
					Skipped++;
					continue;
				}
			}
		}

		UEdGraphNode* CreatedNode = nullptr;
		FString NodeDetail;

		// ---- CallFunction ----
		// Auto-detect AddMappingContext even when requested as CallFunction
		if (NodeType.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
		{
			FString TempFunc = Obj->GetStringField(TEXT("function"));
			if (TempFunc.Contains(TEXT("AddMappingContext"), ESearchCase::IgnoreCase) ||
				TempFunc.Contains(TEXT("Add Mapping Context"), ESearchCase::IgnoreCase))
			{
				NodeType = TEXT("AddMappingContext");
			}
			else if (TempFunc.Contains(TEXT("SpawnActor"), ESearchCase::IgnoreCase) ||
				TempFunc.Contains(TEXT("Spawn Actor"), ESearchCase::IgnoreCase))
			{
				NodeType = TEXT("SpawnActor");
			}
		}

		if (NodeType.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
		{
			FString FunctionName = Obj->GetStringField(TEXT("function"));
			FString TargetClass = Obj->GetStringField(TEXT("target"));

			// Normalize: remove spaces so "Print String" -> "PrintString"
			FString FunctionNameNoSpaces = FunctionName.Replace(TEXT(" "), TEXT(""));

			UFunction* Func = nullptr;
			bool bBruteForce = false;
			bool bAmbiguousAllowed = false;

			// Helper lambda: search function in class + entire parent chain
			// Tries multiple naming conventions:
			//   - exact name, no-spaces, K2_ prefix
			//   - Float <-> Double synonyms (UE 5.5+ math is double)
			//   - bare math operator names ("Multiply" -> "Multiply_DoubleDouble", etc.)
			auto FindFuncInHierarchy = [&FunctionName, &FunctionNameNoSpaces](UClass* StartClass) -> UFunction*
			{
				TArray<FString> Tries;
				Tries.Add(FunctionName);
				Tries.Add(FunctionNameNoSpaces);
				Tries.Add(TEXT("K2_") + FunctionName);
				Tries.Add(TEXT("K2_") + FunctionNameNoSpaces);

				// UE 5.6 renamed LineTraceSingleByChannel → LineTraceSingle
				if (FunctionNameNoSpaces.Equals(TEXT("LineTraceSingleByChannel"), ESearchCase::IgnoreCase) ||
					FunctionNameNoSpaces.Equals(TEXT("LineTraceByChannel"), ESearchCase::IgnoreCase))
					Tries.Add(TEXT("LineTraceSingle"));

				// GetWorldLocation/GetWorldRotation: display name differs by target class
				if (FunctionNameNoSpaces.Equals(TEXT("GetWorldLocation"), ESearchCase::IgnoreCase))
				{
					if (StartClass->IsChildOf(USceneComponent::StaticClass()))
						Tries.Add(TEXT("K2_GetComponentLocation"));
					else if (StartClass->IsChildOf(AActor::StaticClass()))
						Tries.Add(TEXT("K2_GetActorLocation"));
				}
				if (FunctionNameNoSpaces.Equals(TEXT("GetWorldRotation"), ESearchCase::IgnoreCase))
				{
					if (StartClass->IsChildOf(USceneComponent::StaticClass()))
						Tries.Add(TEXT("K2_GetComponentRotation"));
					else if (StartClass->IsChildOf(AActor::StaticClass()))
						Tries.Add(TEXT("K2_GetActorRotation"));
				}

				// Float <-> Double synonyms
				if (FunctionNameNoSpaces.Contains(TEXT("Float")))
					Tries.Add(FunctionNameNoSpaces.Replace(TEXT("Float"), TEXT("Double")));
				if (FunctionNameNoSpaces.Contains(TEXT("Double")))
					Tries.Add(FunctionNameNoSpaces.Replace(TEXT("Double"), TEXT("Float")));

				// Bare math operator names — the LLM often writes just "Multiply"
				// or "Add" without specifying types. Try the full KismetMathLibrary
				// names for the most common overloads.
				static const TArray<FString> MathOps = {
					TEXT("Add"), TEXT("Subtract"), TEXT("Multiply"), TEXT("Divide"),
					TEXT("Min"), TEXT("Max"), TEXT("Equal"), TEXT("NotEqual"),
					TEXT("Less"), TEXT("LessEqual"), TEXT("Greater"), TEXT("GreaterEqual"),
				};
				for (const FString& Op : MathOps)
				{
					if (FunctionNameNoSpaces.Equals(Op, ESearchCase::IgnoreCase))
					{
						Tries.Add(Op + TEXT("_DoubleDouble"));
						Tries.Add(Op + TEXT("_FloatFloat"));
						Tries.Add(Op + TEXT("_IntInt"));
						Tries.Add(Op + TEXT("_VectorVector"));
						break;
					}
				}

				// Display-name conversion: the LLM frequently feeds us strings
				// like "To Vector (Float)", "To Float", "To Int" which are the
				// editor display names. The real C++ functions live under
				// `Conv_<Source>To<Dest>` in KismetMathLibrary. Strip the
				// parenthesized type hint and try every reasonable Conv_ form.
				{
					// "To Vector (Float)" → "ToVector"
					FString StrippedParens = FunctionName;
					int32 LParen;
					if (StrippedParens.FindChar('(', LParen)) StrippedParens = StrippedParens.Left(LParen);
					StrippedParens = StrippedParens.Replace(TEXT(" "), TEXT("")).TrimStartAndEnd();

					if (StrippedParens.StartsWith(TEXT("To"), ESearchCase::IgnoreCase) && StrippedParens.Len() > 2)
					{
						const FString DestType = StrippedParens.RightChop(2); // e.g. "Vector"
						// Try every common source type — first match wins.
						const TArray<FString> SrcTypes = { TEXT("Double"), TEXT("Float"), TEXT("Int"), TEXT("Int64"), TEXT("Bool"), TEXT("String"), TEXT("Name"), TEXT("Vector"), TEXT("Rotator") };
						for (const FString& Src : SrcTypes)
						{
							Tries.Add(FString::Printf(TEXT("Conv_%sTo%s"), *Src, *DestType));
						}
						// Also Make<Type>: MakeVector / MakeRotator etc.
						Tries.Add(TEXT("Make") + DestType);
					}
				}

				for (UClass* C = StartClass; C; C = C->GetSuperClass())
				{
					for (const FString& Try : Tries)
					{
						UFunction* F = C->FindFunctionByName(FName(*Try));
						if (F) return F;
					}
				}
				return nullptr;
			};

			// Search for the function in the target class hierarchy
			if (!TargetClass.IsEmpty())
			{
				UClass* TargetUClass = nullptr;

				// Check SCS component instance names first (e.g. "FirstPersonCamera" → UCameraComponent)
				if (BP->SimpleConstructionScript)
				{
					for (USCS_Node* SCSNode : BP->SimpleConstructionScript->GetAllNodes())
					{
						if (SCSNode && SCSNode->ComponentTemplate &&
							SCSNode->GetVariableName().ToString().Equals(TargetClass, ESearchCase::IgnoreCase))
						{
							TargetUClass = SCSNode->ComponentTemplate->GetClass();
							break;
						}
					}
				}

				if (!TargetUClass)
					TargetUClass = FindClassByName(TargetClass);

				if (TargetUClass)
					Func = FindFuncInHierarchy(TargetUClass);
			}

			// If not found, search in common libraries + BP parent chain
			if (!Func)
			{
				TArray<UClass*> SearchClasses = {
					UKismetSystemLibrary::StaticClass(),
					UKismetMathLibrary::StaticClass(),
					UKismetStringLibrary::StaticClass(),
					UGameplayStatics::StaticClass(),
					UEnhancedInputLocalPlayerSubsystem::StaticClass(),
				};

				// Also search blueprint's generated class and parent hierarchy
				if (BP->GeneratedClass)
				{
					for (UClass* C = BP->GeneratedClass; C; C = C->GetSuperClass())
					{
						SearchClasses.Add(C);
					}
				}

				for (UClass* Lib : SearchClasses)
				{
					Func = FindFuncInHierarchy(Lib);
					if (Func) break;
				}
			}

			// Last resort: brute-force search all loaded UClasses
			if (!Func)
			{
				UFunction* BFFunc = nullptr;
				UClass* BFClass = nullptr;
				for (TObjectIterator<UClass> It; It; ++It)
				{
					UFunction* F = It->FindFunctionByName(FName(*FunctionName), EIncludeSuperFlag::ExcludeSuper);
					if (F && F->HasAnyFunctionFlags(FUNC_BlueprintCallable))
					{
						BFFunc = F;
						BFClass = *It;
						UE_LOG(LogNwiroBP, Log, TEXT("Found function '%s' via brute-force in class '%s'"), *FunctionName, *It->GetName());
						break;
					}
				}

				if (BFFunc && BFClass)
				{
					// Trust the result only if the owning class is a blueprint function library
					// or is in the BP's own parent chain — anything else is ambiguous.
					bool bTrusted = BFClass->IsChildOf(UBlueprintFunctionLibrary::StaticClass());
					if (!bTrusted && BP->GeneratedClass)
					{
						for (UClass* C = BP->GeneratedClass; C; C = C->GetSuperClass())
						{
							if (C == BFClass) { bTrusted = true; break; }
						}
					}

					if (bTrusted)
					{
						Func = BFFunc;
						bBruteForce = true;
					}
					else
					{
						bool bAllowAmbiguous = false;
						Obj->TryGetBoolField(TEXT("allow_ambiguous_bruteforce"), bAllowAmbiguous);

						if (bAllowAmbiguous)
						{
							Func = BFFunc;
							bBruteForce = true;
							bAmbiguousAllowed = true;
						}
						else
						{
							Errors.Add(FString::Printf(
								TEXT("Function '%s' was found via brute-force in class '%s', but that class is not a trusted library or this blueprint's parent chain. ")
								TEXT("If this function belongs to a specific component or object, re-send the node with target:'ComponentInstanceName' or target:'ClassName' so the correct class hierarchy is searched. ")
								TEXT("Only use \"allow_ambiguous_bruteforce\": true if this untrusted owner class is intentional."),
								*FunctionName, *BFClass->GetName()));
							continue;
						}
					}
				}
			}

			if (Func)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* FuncNode = Creator.CreateNode();
				FuncNode->SetFromFunction(Func);
				FuncNode->NodePosX = PosX;
				FuncNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = FuncNode;
				const FString ResolvedName = Func->GetName();
				const FString OwnerName = Func->GetOwnerClass() ? Func->GetOwnerClass()->GetName() : TEXT("unknown");
				if (bAmbiguousAllowed)
					NodeDetail = FString::Printf(TEXT("%s [ambiguous-allowed] found in %s via brute-force, requested:'%s'"), *ResolvedName, *OwnerName, *FunctionName);
				else if (bBruteForce)
					NodeDetail = FString::Printf(TEXT("%s [fallback] found in %s via brute-force, requested:'%s'"), *ResolvedName, *OwnerName, *FunctionName);
				else if (!ResolvedName.Equals(FunctionName, ESearchCase::IgnoreCase))
					NodeDetail = FString::Printf(TEXT("%s [fallback] resolved from requested:'%s' in %s"), *ResolvedName, *FunctionName, *OwnerName);
				else
					NodeDetail = FString::Printf(TEXT("%s [resolved] in %s"), *ResolvedName, *OwnerName);
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
			}
		}
		// ---- VariableGet ----
		else if (NodeType.Equals(TEXT("VariableGet"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("GetVariable"), ESearchCase::IgnoreCase))
		{
			FString VarName = Obj->GetStringField(TEXT("variable"));
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* GetNode = Creator.CreateNode();
			GetNode->VariableReference.SetSelfMember(FName(*VarName));
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = GetNode;
		}
		// ---- VariableSet ----
		else if (NodeType.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("SetVariable"), ESearchCase::IgnoreCase))
		{
			FString VarName = Obj->GetStringField(TEXT("variable"));
			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* SetNode = Creator.CreateNode();
			SetNode->VariableReference.SetSelfMember(FName(*VarName));
			SetNode->NodePosX = PosX;
			SetNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = SetNode;
		}
		// ---- Branch (If) ----
		else if (NodeType.Equals(TEXT("Branch"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("If"), ESearchCase::IgnoreCase))
		{
			FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
			UK2Node_IfThenElse* BranchNode = Creator.CreateNode();
			BranchNode->NodePosX = PosX;
			BranchNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = BranchNode;
		}
		// ---- InputAction ----
		else if (NodeType.Equals(TEXT("InputAction"), ESearchCase::IgnoreCase))
		{
			FString ActionName = Obj->GetStringField(TEXT("action"));
			FGraphNodeCreator<UK2Node_InputAction> Creator(*Graph);
			UK2Node_InputAction* ActionNode = Creator.CreateNode();
			ActionNode->InputActionName = FName(*ActionName);
			ActionNode->NodePosX = PosX;
			ActionNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = ActionNode;
		}
		// ---- InputKey ----
		else if (NodeType.Equals(TEXT("InputKey"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("KeyEvent"), ESearchCase::IgnoreCase))
		{
			FString KeyName = Obj->GetStringField(TEXT("key"));
			FKey Key(*KeyName);

			FGraphNodeCreator<UK2Node_InputKey> Creator(*Graph);
			UK2Node_InputKey* KeyNode = Creator.CreateNode();
			KeyNode->InputKey = Key;
			KeyNode->NodePosX = PosX;
			KeyNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = KeyNode;
		}
		// ---- CustomEvent ----
		else if (NodeType.Equals(TEXT("CustomEvent"), ESearchCase::IgnoreCase))
		{
			FString EventName = Obj->GetStringField(TEXT("name"));
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* EventNode = Creator.CreateNode();
			EventNode->CustomFunctionName = FName(*EventName);
			EventNode->NodePosX = PosX;
			EventNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = EventNode;
		}
		// ---- Event (BeginPlay, Tick, ActorBeginOverlap, etc.) ----
		else if (NodeType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			FString EventName = Obj->GetStringField(TEXT("event"));
			if (EventName.IsEmpty()) EventName = Obj->GetStringField(TEXT("name"));

			// Map user-friendly names to UE internal function names
			static TMap<FString, FString> EventNameMap = {
				{ TEXT("beginplay"), TEXT("ReceiveBeginPlay") },
				{ TEXT("tick"), TEXT("ReceiveTick") },
				{ TEXT("endplay"), TEXT("ReceiveEndPlay") },
				{ TEXT("actorbeginoverlap"), TEXT("ReceiveActorBeginOverlap") },
				{ TEXT("actorendoverlap"), TEXT("ReceiveActorEndOverlap") },
				{ TEXT("hit"), TEXT("ReceiveHit") },
				{ TEXT("anyDamage"), TEXT("ReceiveAnyDamage") },
				{ TEXT("pointdamage"), TEXT("ReceivePointDamage") },
				{ TEXT("radialdamage"), TEXT("ReceiveRadialDamage") },
				{ TEXT("destroyed"), TEXT("ReceiveDestroyed") },
				{ TEXT("begincursorover"), TEXT("ReceiveBeginCursorOver") },
				{ TEXT("endcursorover"), TEXT("ReceiveEndCursorOver") },
				{ TEXT("clicked"), TEXT("ReceiveActorOnClicked") },
				{ TEXT("released"), TEXT("ReceiveActorOnReleased") },
			};

			// Aggressive normalization: strip "event" / "receive" prefixes and
			// non-alphanumeric chars so all of "Tick", "EventTick", "Event Tick",
			// "event_tick", "ReceiveTick" map to the same key.
			FString NormKey = EventName.ToLower();
			NormKey.ReplaceInline(TEXT(" "), TEXT(""));
			NormKey.ReplaceInline(TEXT("_"), TEXT(""));
			if (NormKey.StartsWith(TEXT("receive"))) NormKey = NormKey.RightChop(7);
			if (NormKey.StartsWith(TEXT("event"))) NormKey = NormKey.RightChop(5);

			FString InternalName = EventName;
			if (const FString* Mapped = EventNameMap.Find(NormKey))
			{
				InternalName = *Mapped;
			}
			else if (!EventName.StartsWith(TEXT("Receive"), ESearchCase::IgnoreCase))
			{
				// Also try with Receive prefix on the cleaned name
				InternalName = TEXT("Receive") + EventName;
			}

			// Search existing event nodes in ALL graph pages
			bool bFoundExisting = false;
			for (UEdGraph* SearchGraph : BP->UbergraphPages)
			{
				if (!SearchGraph) continue;
				for (UEdGraphNode* ExistingNode : SearchGraph->Nodes)
				{
					UK2Node_Event* EvtNode = Cast<UK2Node_Event>(ExistingNode);
					if (EvtNode)
					{
						FString EvtFuncName = EvtNode->GetFunctionName().ToString();
						if (EvtFuncName.Equals(InternalName, ESearchCase::IgnoreCase) ||
							EvtFuncName.Contains(EventName, ESearchCase::IgnoreCase))
						{
							CreatedNode = EvtNode;
							bFoundExisting = true;
							break;
						}
					}
				}
				if (bFoundExisting) break;
			}

			if (!bFoundExisting)
			{
				// Find the event function in the parent class chain
				UFunction* EventFunc = nullptr;
				UClass* SearchClass = BP->ParentClass.Get() ? BP->ParentClass.Get() : (BP->GeneratedClass ? BP->GeneratedClass->GetSuperClass() : nullptr);

				if (SearchClass)
				{
					for (UClass* C = SearchClass; C; C = C->GetSuperClass())
					{
						EventFunc = C->FindFunctionByName(FName(*InternalName));
						if (EventFunc) break;

						// Also try original name
						EventFunc = C->FindFunctionByName(FName(*EventName));
						if (EventFunc) break;
					}
				}

				if (EventFunc)
				{
					FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
					UK2Node_Event* EvtNode = Creator.CreateNode();
					EvtNode->EventReference.SetFromField<UFunction>(EventFunc, false);
					EvtNode->bOverrideFunction = true;
					EvtNode->NodePosX = PosX;
					EvtNode->NodePosY = PosY;
					Creator.Finalize();
					CreatedNode = EvtNode;
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("Built-in event not found: %s (tried %s). Use 'CustomEvent' type for custom events."), *EventName, *InternalName));
				}
			}
		}
		// ---- Self ----
		else if (NodeType.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
		{
			UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
			if (SelfNode)
			{
				Graph->AddNode(SelfNode, false, false);
				SelfNode->CreateNewGuid();
				SelfNode->NodePosX = PosX;
				SelfNode->NodePosY = PosY;
				SelfNode->AllocateDefaultPins();
				CreatedNode = SelfNode;
			}
		}
		// ---- SpawnActor ----
		else if (NodeType.Equals(TEXT("SpawnActor"), ESearchCase::IgnoreCase))
		{
			UK2Node_SpawnActorFromClass* SpawnNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
			if (SpawnNode)
			{
				Graph->AddNode(SpawnNode, false, false);
				SpawnNode->CreateNewGuid();
				SpawnNode->NodePosX = PosX;
				SpawnNode->NodePosY = PosY;
				SpawnNode->AllocateDefaultPins();

				FString ActorClassName = GetFieldNormalized(Obj, TEXT("actorclass")); // covers actor_class / actorClass / ActorClass
				if (ActorClassName.IsEmpty()) ActorClassName = GetFieldNormalized(Obj, TEXT("class"));
				if (!ActorClassName.IsEmpty())
				{
					UClass* ActorClass = FindClassByName(ActorClassName);
					if (ActorClass)
					{
						UEdGraphPin* ClassPin = SpawnNode->FindPin(TEXT("Class"));
						if (ClassPin)
						{
							ClassPin->DefaultObject = ActorClass;
							SpawnNode->ReconstructNode();
						}
						NodeDetail = FString::Printf(TEXT("SpawnActor [resolved] class:'%s'"), *ActorClass->GetName());
					}
					else
					{
						Errors.Add(FString::Printf(TEXT("SpawnActor [not_resolved] class:'%s' not found — node created without class; fix actor_class field or use set_pin_defaults to set Class pin"), *ActorClassName));
					}
				}
				else
				{
					NodeDetail = TEXT("SpawnActor [resolved] no class requested");
				}

				CreatedNode = SpawnNode;
			}
		}
		// ---- Timeline (with float tracks + keyframes) ----
		else if (NodeType.Equals(TEXT("Timeline"), ESearchCase::IgnoreCase))
		{
			FString TimelineName = Obj->GetStringField(TEXT("name"));
			if (TimelineName.IsEmpty()) TimelineName = TEXT("MyTimeline");

			bool bLoop = Obj->HasField(TEXT("loop")) ? Obj->GetBoolField(TEXT("loop")) : false;
			bool bAutoPlay = Obj->HasField(TEXT("autoPlay")) ? Obj->GetBoolField(TEXT("autoPlay")) : false;
			float Length = Obj->HasField(TEXT("length")) ? (float)Obj->GetNumberField(TEXT("length")) : 1.0f;

			// Create timeline template FIRST via BlueprintEditorUtils
			UTimelineTemplate* TLTemplate = FBlueprintEditorUtils::AddNewTimeline(BP, FName(*TimelineName));
			UE_LOG(LogNwiroBP, Log, TEXT("Timeline '%s': Template %s"), *TimelineName, TLTemplate ? TEXT("CREATED") : TEXT("FAILED"));

			// Now create the node
			FGraphNodeCreator<UK2Node_Timeline> Creator(*Graph);
			UK2Node_Timeline* TLNode = Creator.CreateNode();
			TLNode->TimelineName = FName(*TimelineName);
			TLNode->bAutoPlay = bAutoPlay;
			TLNode->bLoop = bLoop;
			TLNode->NodePosX = PosX;
			TLNode->NodePosY = PosY;
			Creator.Finalize();

			if (TLTemplate)
			{
				TLTemplate->TimelineLength = Length;
				TLTemplate->bLoop = bLoop;
				TLTemplate->bAutoPlay = bAutoPlay;

				// Add float tracks
				const TArray<TSharedPtr<FJsonValue>>* Tracks;
				if (Obj->TryGetArrayField(TEXT("floatTracks"), Tracks))
				{
					for (const TSharedPtr<FJsonValue>& TrackVal : *Tracks)
					{
						const TSharedPtr<FJsonObject>& TrackObj = TrackVal->AsObject();
						if (!TrackObj.IsValid()) continue;

						FString TrackName = TrackObj->GetStringField(TEXT("name"));
						if (TrackName.IsEmpty()) continue;

						// Create inline curve
						UCurveFloat* Curve = NewObject<UCurveFloat>(TLTemplate, FName(*(TimelineName + TEXT("_") + TrackName)));

						// Add keyframes
						const TArray<TSharedPtr<FJsonValue>>* Keys;
						if (TrackObj->TryGetArrayField(TEXT("keys"), Keys))
						{
							for (const TSharedPtr<FJsonValue>& KeyVal : *Keys)
							{
								const TSharedPtr<FJsonObject>& KeyObj = KeyVal->AsObject();
								if (!KeyObj.IsValid()) continue;

								float Time = (float)KeyObj->GetNumberField(TEXT("time"));
								float Value = (float)KeyObj->GetNumberField(TEXT("value"));

								FString InterpStr = KeyObj->GetStringField(TEXT("interp")).ToLower();
								ERichCurveInterpMode InterpMode = RCIM_Linear;
								if (InterpStr == TEXT("cubic") || InterpStr == TEXT("auto"))
									InterpMode = RCIM_Cubic;
								else if (InterpStr == TEXT("constant") || InterpStr == TEXT("step"))
									InterpMode = RCIM_Constant;

								FKeyHandle KeyHandle = Curve->FloatCurve.AddKey(Time, Value);
								Curve->FloatCurve.SetKeyInterpMode(KeyHandle, InterpMode);
							}
						}

						// Add the track to the template
						FTTFloatTrack NewTrack;
						NewTrack.SetTrackName(FName(*TrackName), TLTemplate);
						NewTrack.CurveFloat = Curve;
						TLTemplate->FloatTracks.Add(NewTrack);

						UE_LOG(LogNwiroBP, Log, TEXT("Timeline '%s': Added float track '%s' with %d keys"),
							*TimelineName, *TrackName, Keys ? Keys->Num() : 0);
					}
				}

				// Reconstruct node to show new track pins
				TLNode->ReconstructNode();
			}

			CreatedNode = TLNode;
		}
		// ---- Macro (ForEachLoop, Sequence, etc.) ----
		else if (NodeType.Equals(TEXT("Macro"), ESearchCase::IgnoreCase))
		{
			FString MacroName = Obj->GetStringField(TEXT("macro"));
			// Search for macro graph in engine blueprints
			UEdGraph* MacroGraph = nullptr;

			// Check standard macro library
			static UBlueprint* MacroLib = LoadObject<UBlueprint>(nullptr,
				TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));

			if (MacroLib)
			{
				for (UEdGraph* MG : MacroLib->MacroGraphs)
				{
					if (MG && MG->GetName().Equals(MacroName, ESearchCase::IgnoreCase))
					{
						MacroGraph = MG;
						break;
					}
				}
			}

			if (MacroGraph)
			{
				FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
				UK2Node_MacroInstance* MacroNode = Creator.CreateNode();
				MacroNode->SetMacroGraph(MacroGraph);
				MacroNode->NodePosX = PosX;
				MacroNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = MacroNode;
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Macro not found: %s"), *MacroName));
			}
		}
		// ---- Cast ----
		else if (NodeType.Equals(TEXT("Cast"), ESearchCase::IgnoreCase))
		{
			FString TargetClassName = Obj->GetStringField(TEXT("class"));
			UClass* CastClass = FindClassByName(TargetClassName);
			if (CastClass)
			{
				FGraphNodeCreator<UK2Node_DynamicCast> Creator(*Graph);
				UK2Node_DynamicCast* CastNode = Creator.CreateNode();
				CastNode->TargetType = CastClass;
				CastNode->NodePosX = PosX;
				CastNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = CastNode;
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Cast target class not found: %s"), *TargetClassName));
			}
		}
		// ---- AddMappingContext (convenience: creates GetController→Cast→GetSubsystem→AddMappingContext chain) ----
		else if (NodeType.Equals(TEXT("AddMappingContext"), ESearchCase::IgnoreCase))
		{
			FString IMCPath = Obj->GetStringField(TEXT("context"));
			if (IMCPath.IsEmpty()) IMCPath = Obj->GetStringField(TEXT("mappingContext"));
			int32 Priority = Obj->HasField(TEXT("priority")) ? (int32)Obj->GetNumberField(TEXT("priority")) : 0;

			// Create 4 nodes: GetController → CastToPlayerController → GetSubsystem → AddMappingContext
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

			// 1. GetController
			UFunction* GetControllerFunc = ACharacter::StaticClass()->FindFunctionByName(FName(TEXT("GetController")));
			if (!GetControllerFunc) GetControllerFunc = APawn::StaticClass()->FindFunctionByName(FName(TEXT("GetController")));

			UK2Node_CallFunction* GetCtrlNode = nullptr;
			if (GetControllerFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> C1(*Graph);
				GetCtrlNode = C1.CreateNode();
				GetCtrlNode->SetFromFunction(GetControllerFunc);
				GetCtrlNode->NodePosX = PosX;
				GetCtrlNode->NodePosY = PosY;
				C1.Finalize();
			}

			// 2. CastToPlayerController
			UK2Node_DynamicCast* CastNode = nullptr;
			{
				UClass* PCClass = APlayerController::StaticClass();
				FGraphNodeCreator<UK2Node_DynamicCast> C2(*Graph);
				CastNode = C2.CreateNode();
				CastNode->TargetType = PCClass;
				CastNode->NodePosX = PosX + 250;
				CastNode->NodePosY = PosY;
				C2.Finalize();
			}

			// 3. GetLocalPlayerSubSystemFromPlayerController (from SubsystemBlueprintLibrary)
			UFunction* GetSubsysFunc = nullptr;
			{
				UClass* SubsysBPLib = FindFirstObject<UClass>(TEXT("SubsystemBlueprintLibrary"));
				if (!SubsysBPLib) SubsysBPLib = FindFirstObject<UClass>(TEXT("USubsystemBlueprintLibrary"));
				if (SubsysBPLib)
				{
					GetSubsysFunc = SubsysBPLib->FindFunctionByName(FName(TEXT("GetLocalPlayerSubSystemFromPlayerController")));
					if (!GetSubsysFunc) GetSubsysFunc = SubsysBPLib->FindFunctionByName(FName(TEXT("GetLocalPlayerSubsystem")));
				}
				// Brute force if not found
				if (!GetSubsysFunc)
				{
					for (TObjectIterator<UClass> It; It; ++It)
					{
						GetSubsysFunc = It->FindFunctionByName(FName(TEXT("GetLocalPlayerSubSystemFromPlayerController")), EIncludeSuperFlag::ExcludeSuper);
						if (GetSubsysFunc && GetSubsysFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable))
						{
							UE_LOG(LogNwiroBP, Log, TEXT("Found GetLocalPlayerSubSystemFromPlayerController in %s"), *It->GetName());
							break;
						}
						GetSubsysFunc = nullptr;
					}
				}
			}

			UK2Node_CallFunction* GetSubsysNode = nullptr;
			if (GetSubsysFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> C3(*Graph);
				GetSubsysNode = C3.CreateNode();
				GetSubsysNode->SetFromFunction(GetSubsysFunc);
				GetSubsysNode->NodePosX = PosX + 500;
				GetSubsysNode->NodePosY = PosY;
				C3.Finalize();

				// Set the Class pin to UEnhancedInputLocalPlayerSubsystem
				UEdGraphPin* ClassPin = FindPin(GetSubsysNode, TEXT("Class"));
				if (ClassPin)
				{
					FString SubsysClassPath = UEnhancedInputLocalPlayerSubsystem::StaticClass()->GetPathName();
					K2Schema->TrySetDefaultObject(*ClassPin, UEnhancedInputLocalPlayerSubsystem::StaticClass());
				}
			}
			else
			{
				UE_LOG(LogNwiroBP, Warning, TEXT("AddMappingContext: GetLocalPlayerSubSystemFromPlayerController not found"));
			}

			// 4. AddMappingContext
			UFunction* AddMCFunc = UEnhancedInputLocalPlayerSubsystem::StaticClass()->FindFunctionByName(FName(TEXT("AddMappingContext")));
			if (!AddMCFunc)
			{
				// Search in interface hierarchy
				for (UClass* C = UEnhancedInputLocalPlayerSubsystem::StaticClass(); C; C = C->GetSuperClass())
				{
					AddMCFunc = C->FindFunctionByName(FName(TEXT("AddMappingContext")));
					if (AddMCFunc) break;
				}
			}
			// Also search interfaces
			if (!AddMCFunc)
			{
				for (const FImplementedInterface& Iface : UEnhancedInputLocalPlayerSubsystem::StaticClass()->Interfaces)
				{
					if (Iface.Class)
					{
						AddMCFunc = Iface.Class->FindFunctionByName(FName(TEXT("AddMappingContext")));
						if (AddMCFunc) break;
					}
				}
			}

			UK2Node_CallFunction* AddMCNode = nullptr;
			if (AddMCFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> C4(*Graph);
				AddMCNode = C4.CreateNode();
				AddMCNode->SetFromFunction(AddMCFunc);
				AddMCNode->NodePosX = PosX + 750;
				AddMCNode->NodePosY = PosY;
				C4.Finalize();

				// Set IMC default value if path provided
				if (!IMCPath.IsEmpty())
				{
					UEdGraphPin* MCPin = FindPin(AddMCNode, TEXT("MappingContext"));
					if (MCPin)
					{
						// Load the IMC asset and set as default object
						UObject* IMCObj = LoadObject<UObject>(nullptr, *IMCPath);
						if (!IMCObj)
						{
							// Search by name in asset registry
							FAssetRegistryModule& ARM3 = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
							ARM3.Get().ScanPathsSynchronous({TEXT("/Game")}, true);
							FARFilter IMCFilter;
							IMCFilter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
							IMCFilter.bRecursivePaths = true;
							TArray<FAssetData> IMCAssets;
							ARM3.Get().GetAssets(IMCFilter, IMCAssets);
							for (const FAssetData& A : IMCAssets)
							{
								if (A.AssetName.ToString().Contains(IMCPath, ESearchCase::IgnoreCase))
								{
									IMCObj = A.GetAsset();
									break;
								}
							}
						}
						if (IMCObj)
						{
							K2Schema->TrySetDefaultObject(*MCPin, IMCObj);
							UE_LOG(LogNwiroBP, Log, TEXT("AddMappingContext: Set MappingContext to %s"), *IMCObj->GetName());
						}
						else
						{
							UE_LOG(LogNwiroBP, Warning, TEXT("AddMappingContext: IMC not found: %s"), *IMCPath);
						}
					}
				}

				// Set priority
				UEdGraphPin* PriorityPin = FindPin(AddMCNode, TEXT("Priority"));
				if (PriorityPin)
				{
					K2Schema->TrySetDefaultValue(*PriorityPin, FString::FromInt(Priority));
				}
			}

			// Wire them together
			if (GetCtrlNode && CastNode)
			{
				// GetController ReturnValue → Cast Object pin
				UEdGraphPin* CtrlOut = FindPin(GetCtrlNode, TEXT("ReturnValue"), EGPD_Output);
				UEdGraphPin* CastIn = FindPin(CastNode, TEXT("Object"), EGPD_Input);
				if (CtrlOut && CastIn) K2Schema->TryCreateConnection(CtrlOut, CastIn);

				// GetController exec → Cast exec
				UEdGraphPin* CtrlExecOut = FindPin(GetCtrlNode, TEXT("then"), EGPD_Output);
				if (!CtrlExecOut) CtrlExecOut = FindPin(GetCtrlNode, TEXT("execute"), EGPD_Output);
			}

			if (CastNode && GetSubsysNode)
			{
				// Cast "AsPlayer Controller" → GetSubsystem "PlayerController" pin
				// Note: Cast output pin name has space: "AsPlayer Controller"
				UEdGraphPin* CastResult = nullptr;
				for (UEdGraphPin* Pin : CastNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
					{
						CastResult = Pin;
						break;
					}
				}
				UEdGraphPin* SubsysPC = FindPin(GetSubsysNode, TEXT("PlayerController"), EGPD_Input);
				if (CastResult && SubsysPC)
				{
					K2Schema->TryCreateConnection(CastResult, SubsysPC);
					UE_LOG(LogNwiroBP, Log, TEXT("AddMappingContext: Connected Cast→GetSubsystem (%s→%s)"), *CastResult->PinName.ToString(), *SubsysPC->PinName.ToString());
				}
				else
				{
					UE_LOG(LogNwiroBP, Warning, TEXT("AddMappingContext: Failed to connect Cast→GetSubsystem (CastResult=%d, SubsysPC=%d)"), CastResult!=nullptr, SubsysPC!=nullptr);
				}
			}

			// Cast GetSubsystem result to EnhancedInputLocalPlayerSubsystem
			// This resolves the Object→Interface type mismatch
			UK2Node_DynamicCast* SubsysCastNode = nullptr;
			if (GetSubsysNode && AddMCNode)
			{
				FGraphNodeCreator<UK2Node_DynamicCast> C5(*Graph);
				SubsysCastNode = C5.CreateNode();
				SubsysCastNode->TargetType = UEnhancedInputLocalPlayerSubsystem::StaticClass();
				SubsysCastNode->NodePosX = PosX + 625;
				SubsysCastNode->NodePosY = PosY;
				C5.Finalize();

				// GetSubsystem ReturnValue → Cast Object
				UEdGraphPin* SubsysOut = FindPin(GetSubsysNode, TEXT("ReturnValue"), EGPD_Output);
				UEdGraphPin* CastObjIn = FindPin(SubsysCastNode, TEXT("Object"), EGPD_Input);
				if (SubsysOut && CastObjIn) K2Schema->TryCreateConnection(SubsysOut, CastObjIn);

				// Cast result → AddMappingContext self
				UEdGraphPin* CastResultOut = nullptr;
				for (UEdGraphPin* Pin : SubsysCastNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
					{
						CastResultOut = Pin;
						break;
					}
				}
				UEdGraphPin* MCTarget = FindPin(AddMCNode, TEXT("self"), EGPD_Input);
				if (CastResultOut && MCTarget)
				{
					K2Schema->TryCreateConnection(CastResultOut, MCTarget);
					UE_LOG(LogNwiroBP, Log, TEXT("AddMappingContext: Connected SubsysCast→AddMC (%s→%s)"), *CastResultOut->PinName.ToString(), *MCTarget->PinName.ToString());
				}
			}

			// Exec chain: PlayerController Cast → SubsysCast → AddMappingContext
			if (CastNode && SubsysCastNode && AddMCNode)
			{
				// PC Cast then → SubsysCast execute
				UEdGraphPin* PCCastExec = FindPin(CastNode, TEXT("then"), EGPD_Output);
				UEdGraphPin* SubCastExec = FindPin(SubsysCastNode, TEXT("execute"), EGPD_Input);
				if (PCCastExec && SubCastExec) K2Schema->TryCreateConnection(PCCastExec, SubCastExec);

				// SubsysCast then → AddMC execute
				UEdGraphPin* SubCastThen = FindPin(SubsysCastNode, TEXT("then"), EGPD_Output);
				UEdGraphPin* MCExecIn = FindPin(AddMCNode, TEXT("execute"), EGPD_Input);
				if (SubCastThen && MCExecIn) K2Schema->TryCreateConnection(SubCastThen, MCExecIn);
			}
			else if (CastNode && AddMCNode)
			{
				// Fallback: direct exec
				UEdGraphPin* CastExecOut = FindPin(CastNode, TEXT("then"), EGPD_Output);
				UEdGraphPin* MCExecIn = FindPin(AddMCNode, TEXT("execute"), EGPD_Input);
				if (CastExecOut && MCExecIn) K2Schema->TryCreateConnection(CastExecOut, MCExecIn);
			}

			// Auto-connect to BeginPlay if available and Cast exec input is free
			if (CastNode)
			{
				UEdGraphPin* CastExecIn = FindPin(CastNode, TEXT("execute"), EGPD_Input);
				if (CastExecIn && CastExecIn->LinkedTo.Num() == 0)
				{
					// Find BeginPlay event in this graph
					for (UEdGraphNode* N : Graph->Nodes)
					{
						UK2Node_Event* EvtNode = Cast<UK2Node_Event>(N);
						if (EvtNode && EvtNode->GetFunctionName().ToString().Contains(TEXT("BeginPlay")))
						{
							UEdGraphPin* BPExecOut = FindPin(EvtNode, TEXT("then"), EGPD_Output);
							if (BPExecOut && BPExecOut->LinkedTo.Num() == 0)
							{
								K2Schema->TryCreateConnection(BPExecOut, CastExecIn);
								UE_LOG(LogNwiroBP, Log, TEXT("AddMappingContext: Auto-connected BeginPlay→Cast"));
							}
							break;
						}
					}
				}
			}

			// Store refs
			if (GetCtrlNode) StoreNodeRef(Ref.IsEmpty() ? TEXT("get_controller") : Ref + TEXT("_getctrl"), GetCtrlNode, GraphName);
			if (CastNode) StoreNodeRef(Ref.IsEmpty() ? TEXT("cast_pc") : Ref + TEXT("_cast"), CastNode, GraphName);
			if (AddMCNode)
			{
				StoreNodeRef(Ref.IsEmpty() ? TEXT("add_mc") : Ref, AddMCNode, GraphName);
				// Return AddMCNode as created - its "then" pin is the exit for further chaining
				CreatedNode = AddMCNode;
			}
			else
			{
				Errors.Add(TEXT("AddMappingContext function not found"));
				if (CastNode) CreatedNode = CastNode;
			}

			Added += 3; // We created multiple nodes
		}
		// ---- Delay (uses CallFunction on KismetSystemLibrary::Delay) ----
		else if (NodeType.Equals(TEXT("Delay"), ESearchCase::IgnoreCase))
		{
			UFunction* DelayFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(TEXT("Delay")));
			if (DelayFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* DelayNode = Creator.CreateNode();
				DelayNode->SetFromFunction(DelayFunc);
				DelayNode->NodePosX = PosX;
				DelayNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = DelayNode;
			}
		}
		// ---- PrintString ----
		else if (NodeType.Equals(TEXT("PrintString"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("Print"), ESearchCase::IgnoreCase))
		{
			UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(TEXT("PrintString")));
			if (PrintFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* PrintNode = Creator.CreateNode();
				PrintNode->SetFromFunction(PrintFunc);
				PrintNode->NodePosX = PosX;
				PrintNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = PrintNode;
			}
		}
		// ---- SetTimer ----
		else if (NodeType.Equals(TEXT("SetTimer"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("SetTimerByFunctionName"), ESearchCase::IgnoreCase))
		{
			UFunction* TimerFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(TEXT("K2_SetTimerDelegate")));
			if (!TimerFunc)
			{
				TimerFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(TEXT("SetTimerByFunctionName")));
			}
			if (TimerFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* TimerNode = Creator.CreateNode();
				TimerNode->SetFromFunction(TimerFunc);
				TimerNode->NodePosX = PosX;
				TimerNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = TimerNode;
			}
		}
		// ---- DestroyActor ----
		else if (NodeType.Equals(TEXT("DestroyActor"), ESearchCase::IgnoreCase))
		{
			UFunction* DestroyFunc = AActor::StaticClass()->FindFunctionByName(FName(TEXT("K2_DestroyActor")));
			if (DestroyFunc)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* DestroyNode = Creator.CreateNode();
				DestroyNode->SetFromFunction(DestroyFunc);
				DestroyNode->NodePosX = PosX;
				DestroyNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = DestroyNode;
			}
		}
		// ---- SetActorLocation ----
		else if (NodeType.Equals(TEXT("SetActorLocation"), ESearchCase::IgnoreCase))
		{
			UFunction* Func = AActor::StaticClass()->FindFunctionByName(FName(TEXT("K2_SetActorLocation")));
			if (Func)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* FuncNode = Creator.CreateNode();
				FuncNode->SetFromFunction(Func);
				FuncNode->NodePosX = PosX;
				FuncNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = FuncNode;
			}
		}
		// ---- GetActorLocation ----
		else if (NodeType.Equals(TEXT("GetActorLocation"), ESearchCase::IgnoreCase))
		{
			UFunction* Func = AActor::StaticClass()->FindFunctionByName(FName(TEXT("K2_GetActorLocation")));
			if (Func)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* FuncNode = Creator.CreateNode();
				FuncNode->SetFromFunction(Func);
				FuncNode->NodePosX = PosX;
				FuncNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = FuncNode;
			}
		}
		// ---- EnhancedInputAction (UE5 Enhanced Input) ----
		else if (NodeType.Equals(TEXT("EnhancedInputAction"), ESearchCase::IgnoreCase))
		{
			FString ActionPath = Obj->GetStringField(TEXT("action"));
			if (ActionPath.IsEmpty()) ActionPath = Obj->GetStringField(TEXT("name"));

			// Find the InputAction asset
			UInputAction* FoundAction = nullptr;

			// Try direct load
			if (!ActionPath.IsEmpty())
			{
				FoundAction = LoadObject<UInputAction>(nullptr, *ActionPath);

				// Try with /Game/ prefix
				if (!FoundAction && !ActionPath.StartsWith(TEXT("/")))
				{
					FoundAction = LoadObject<UInputAction>(nullptr, *(TEXT("/Game/") + ActionPath));
					if (!FoundAction) FoundAction = LoadObject<UInputAction>(nullptr, *(TEXT("/Game/Input/") + ActionPath));
				}

				// Search asset registry by name
				if (!FoundAction)
				{
					FAssetRegistryModule& ARM2 = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					IAssetRegistry& AR2 = ARM2.Get();
					AR2.ScanPathsSynchronous({TEXT("/Game")}, true);

					FARFilter IAFilter;
					IAFilter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
					IAFilter.bRecursivePaths = true;

					TArray<FAssetData> IAAssets;
					AR2.GetAssets(IAFilter, IAAssets);

					for (const FAssetData& Asset : IAAssets)
					{
						if (Asset.AssetName.ToString().Equals(ActionPath, ESearchCase::IgnoreCase) ||
							Asset.AssetName.ToString().Contains(ActionPath, ESearchCase::IgnoreCase))
						{
							FoundAction = Cast<UInputAction>(Asset.GetAsset());
							if (FoundAction) break;
						}
					}
				}
			}

			FGraphNodeCreator<UK2Node_EnhancedInputAction> Creator(*Graph);
			UK2Node_EnhancedInputAction* EIANode = Creator.CreateNode();

			if (FoundAction)
			{
				// Set InputAction property via reflection
				FObjectProperty* ActionProp = CastField<FObjectProperty>(
					UK2Node_EnhancedInputAction::StaticClass()->FindPropertyByName(FName(TEXT("InputAction"))));
				if (ActionProp)
				{
					ActionProp->SetObjectPropertyValue(
						ActionProp->ContainerPtrToValuePtr<void>(EIANode), FoundAction);
				}
				UE_LOG(LogNwiroBP, Log, TEXT("EnhancedInputAction: Set action to %s"), *FoundAction->GetName());
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));
			}

			EIANode->NodePosX = PosX;
			EIANode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = EIANode;
		}
		// ---- SwitchOnInt ----
		else if (NodeType.Equals(TEXT("SwitchOnInt"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("SwitchInteger"), ESearchCase::IgnoreCase))
		{
			FGraphNodeCreator<UK2Node_SwitchInteger> Creator(*Graph);
			UK2Node_SwitchInteger* SwitchNode = Creator.CreateNode();
			SwitchNode->NodePosX = PosX;
			SwitchNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = SwitchNode;
		}
		// ---- SwitchOnString ----
		else if (NodeType.Equals(TEXT("SwitchOnString"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("SwitchString"), ESearchCase::IgnoreCase))
		{
			FGraphNodeCreator<UK2Node_SwitchString> Creator(*Graph);
			UK2Node_SwitchString* SwitchNode = Creator.CreateNode();
			SwitchNode->NodePosX = PosX;
			SwitchNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = SwitchNode;
		}
		// ---- MakeArray ----
		else if (NodeType.Equals(TEXT("MakeArray"), ESearchCase::IgnoreCase))
		{
			FGraphNodeCreator<UK2Node_MakeArray> Creator(*Graph);
			UK2Node_MakeArray* ArrayNode = Creator.CreateNode();
			ArrayNode->NodePosX = PosX;
			ArrayNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = ArrayNode;
		}
		// ---- Select ----
		else if (NodeType.Equals(TEXT("Select"), ESearchCase::IgnoreCase))
		{
			FGraphNodeCreator<UK2Node_Select> Creator(*Graph);
			UK2Node_Select* SelectNode = Creator.CreateNode();
			SelectNode->NodePosX = PosX;
			SelectNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = SelectNode;
		}
		// ---- GetComponent: Gets a component variable from self (e.g., CharacterMovement) ----
		else if (NodeType.Equals(TEXT("GetComponent"), ESearchCase::IgnoreCase))
		{
			FString CompName = Obj->GetStringField(TEXT("component"));
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* GetNode = Creator.CreateNode();
			GetNode->VariableReference.SetSelfMember(FName(*CompName));
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = GetNode;
		}
		// ---- SetProperty: Sets a property on a target object (e.g., MaxWalkSpeed on CharacterMovement) ----
		else if (NodeType.Equals(TEXT("SetProperty"), ESearchCase::IgnoreCase))
		{
			FString PropName = GetFieldNormalized(Obj, TEXT("property"));
			FString OwnerClassName = GetFieldNormalized(Obj, TEXT("ownerclass")); // covers ownerClass / owner_class / OwnerClass

			UClass* OwnerClass = !OwnerClassName.IsEmpty() ? FindClassByName(OwnerClassName) : nullptr;

			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* SetNode = Creator.CreateNode();
			if (OwnerClass)
			{
				SetNode->VariableReference.SetExternalMember(FName(*PropName), OwnerClass);
				NodeDetail = FString::Printf(TEXT("SetProperty:'%s' [resolved] owner:'%s'"), *PropName, *OwnerClass->GetName());
			}
			else if (!OwnerClassName.IsEmpty())
			{
				SetNode->VariableReference.SetSelfMember(FName(*PropName));
				Errors.Add(FString::Printf(TEXT("SetProperty:'%s' [not_resolved] owner:'%s' not found — node created as self-member; fix ownerClass field"), *PropName, *OwnerClassName));
			}
			else
			{
				SetNode->VariableReference.SetSelfMember(FName(*PropName));
				NodeDetail = FString::Printf(TEXT("SetProperty:'%s' [resolved] self"), *PropName);
			}
			SetNode->NodePosX = PosX;
			SetNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = SetNode;
		}
		// ---- GetProperty: Gets a property from a target object ----
		else if (NodeType.Equals(TEXT("GetProperty"), ESearchCase::IgnoreCase))
		{
			FString PropName = GetFieldNormalized(Obj, TEXT("property"));
			FString OwnerClassName = GetFieldNormalized(Obj, TEXT("ownerclass")); // covers ownerClass / owner_class / OwnerClass

			UClass* OwnerClass = !OwnerClassName.IsEmpty() ? FindClassByName(OwnerClassName) : nullptr;

			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* GetNode = Creator.CreateNode();
			if (OwnerClass)
			{
				GetNode->VariableReference.SetExternalMember(FName(*PropName), OwnerClass);
				NodeDetail = FString::Printf(TEXT("GetProperty:'%s' [resolved] owner:'%s'"), *PropName, *OwnerClass->GetName());
			}
			else if (!OwnerClassName.IsEmpty())
			{
				GetNode->VariableReference.SetSelfMember(FName(*PropName));
				Errors.Add(FString::Printf(TEXT("GetProperty:'%s' [not_resolved] owner:'%s' not found — node created as self-member; fix ownerClass field"), *PropName, *OwnerClassName));
			}
			else
			{
				GetNode->VariableReference.SetSelfMember(FName(*PropName));
				NodeDetail = FString::Printf(TEXT("GetProperty:'%s' [resolved] self"), *PropName);
			}
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = GetNode;
		}
		// ---- Sequence ----
		else if (NodeType.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("ExecutionSequence"), ESearchCase::IgnoreCase))
		{
			UK2Node_CallFunction* SeqNode = NewObject<UK2Node_CallFunction>(Graph);
			UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeLiteralInt"));
			// Use ExecutionSequence macro instead
			FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*Graph);
			UK2Node_ExecutionSequence* Node = Creator.CreateNode();
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;
			Creator.Finalize();
			CreatedNode = Node;
		}
		// ---- ForEachLoop / WhileLoop (Macros) ----
		else if (NodeType.Equals(TEXT("ForEachLoop"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("ForLoop"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("WhileLoop"), ESearchCase::IgnoreCase))
		{
			FString MacroName;
			if (NodeType.Contains(TEXT("ForEach"))) MacroName = TEXT("ForEachLoop");
			else if (NodeType.Contains(TEXT("ForLoop")) || NodeType.Contains(TEXT("For"))) MacroName = TEXT("ForLoop");
			else MacroName = TEXT("WhileLoop");

			UEdGraph* MacroGraph = nullptr;
			TArray<UBlueprint*> MacroLibs;
			MacroLibs.Add(LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")));
			for (UBlueprint* Lib : MacroLibs)
			{
				if (!Lib) continue;
				for (UEdGraph* G : Lib->MacroGraphs)
				{
					if (G && G->GetFName().ToString().Equals(MacroName, ESearchCase::IgnoreCase))
					{
						MacroGraph = G;
						break;
					}
				}
			}
			if (MacroGraph)
			{
				FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
				UK2Node_MacroInstance* MacroNode = Creator.CreateNode();
				MacroNode->SetMacroGraph(MacroGraph);
				MacroNode->NodePosX = PosX;
				MacroNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = MacroNode;
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Macro not found: %s"), *MacroName));
			}
		}
		// ---- Gate / DoOnce / FlipFlop / DoN / IsValid (Macros) ----
		else if (NodeType.Equals(TEXT("Gate"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("DoOnce"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("FlipFlop"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("DoN"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("IsValid"), ESearchCase::IgnoreCase))
		{
			UEdGraph* MacroGraph = nullptr;
			UBlueprint* StdMacros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
			if (StdMacros)
			{
				for (UEdGraph* G : StdMacros->MacroGraphs)
				{
					if (G && G->GetFName().ToString().Equals(NodeType, ESearchCase::IgnoreCase))
					{
						MacroGraph = G;
						break;
					}
				}
			}
			if (MacroGraph)
			{
				FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
				UK2Node_MacroInstance* MacroNode = Creator.CreateNode();
				MacroNode->SetMacroGraph(MacroGraph);
				MacroNode->NodePosX = PosX;
				MacroNode->NodePosY = PosY;
				Creator.Finalize();
				CreatedNode = MacroNode;
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Standard macro not found: %s"), *NodeType));
			}
		}
		// ---- Math shorthand nodes (Add, Subtract, Multiply, Divide, Clamp, Lerp, Abs, etc.) ----
		else if (NodeType.Equals(TEXT("Add"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Subtract"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Multiply"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Divide"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Clamp"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Lerp"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Abs"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Min"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Max"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Power"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("Sqrt"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("RandomFloat"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("RandomInteger"), ESearchCase::IgnoreCase))
		{
			// Map shorthand to actual function names
			FString FuncName = NodeType;
			if (NodeType.Equals(TEXT("Add"), ESearchCase::IgnoreCase)) FuncName = TEXT("Add_FloatFloat");
			else if (NodeType.Equals(TEXT("Subtract"), ESearchCase::IgnoreCase)) FuncName = TEXT("Subtract_FloatFloat");
			else if (NodeType.Equals(TEXT("Multiply"), ESearchCase::IgnoreCase)) FuncName = TEXT("Multiply_FloatFloat");
			else if (NodeType.Equals(TEXT("Divide"), ESearchCase::IgnoreCase)) FuncName = TEXT("Divide_FloatFloat");
			else if (NodeType.Equals(TEXT("Lerp"), ESearchCase::IgnoreCase)) FuncName = TEXT("Lerp");
			else if (NodeType.Equals(TEXT("Clamp"), ESearchCase::IgnoreCase)) FuncName = TEXT("FClamp");
			else if (NodeType.Equals(TEXT("RandomFloat"), ESearchCase::IgnoreCase)) FuncName = TEXT("RandomFloatInRange");
			else if (NodeType.Equals(TEXT("RandomInteger"), ESearchCase::IgnoreCase)) FuncName = TEXT("RandomIntegerInRange");

			UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(*FuncName));
			if (!Func) Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(*(TEXT("K") + FuncName)));
			if (Func)
			{
				UK2Node_CallFunction* FuncNode = NewObject<UK2Node_CallFunction>(Graph);
				FuncNode->SetFromFunction(Func);
				FuncNode->NodePosX = PosX;
				FuncNode->NodePosY = PosY;
				Graph->AddNode(FuncNode, false, false);
				FuncNode->AllocateDefaultPins();
				CreatedNode = FuncNode;
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Math function not found: %s -> %s"), *NodeType, *FuncName));
			}
		}
		// ---- Common shorthand functions ----
		else if (NodeType.Equals(TEXT("GetPlayerController"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetPlayerPawn"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetPlayerCharacter"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetPlayerCameraManager"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetGameMode"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetGameInstance"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetGameState"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("PlaySound2D"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("PlaySoundAtLocation"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SpawnEmitterAtLocation"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SpawnEmitterAttached"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("LineTraceByChannel"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SphereTraceByChannel"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("BoxTraceByChannel"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("CreateWidget"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("AddToViewport"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("RemoveFromParent"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetInputMode"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("FormatText"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("AppendString"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetWorldDeltaSeconds"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetActorRotation"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetActorRotation"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetActorScale3D"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetActorScale3D"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetActorTransform"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("GetActorTransform"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("AddActorWorldOffset"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("AddActorWorldRotation"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetActorHiddenInGame"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("IsOverlappingActor"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetVisibility"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetCollisionEnabled"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("PlayAnimMontage"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("StopAnimMontage"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("SetTimerByEvent"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("ClearTimer"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("OpenLevel"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("QuitGame"), ESearchCase::IgnoreCase) ||
				 NodeType.Equals(TEXT("ApplyDamage"), ESearchCase::IgnoreCase))
		{
			// Remove spaces from NodeType for function lookup
			FString FuncName = NodeType.Replace(TEXT(" "), TEXT(""));
			UFunction* Func = nullptr;

			TArray<UClass*> SearchLibs = {
				UGameplayStatics::StaticClass(),
				UKismetSystemLibrary::StaticClass(),
				UKismetMathLibrary::StaticClass(),
				UKismetStringLibrary::StaticClass(),
			};
			if (BP->GeneratedClass)
			{
				for (UClass* C = BP->GeneratedClass; C; C = C->GetSuperClass())
					SearchLibs.Add(C);
			}

			for (UClass* Lib : SearchLibs)
			{
				for (TFieldIterator<UFunction> It(Lib); It; ++It)
				{
					FString Name = It->GetName();
					if (Name.Equals(FuncName, ESearchCase::IgnoreCase) || Name.Replace(TEXT("_"), TEXT("")).Equals(FuncName, ESearchCase::IgnoreCase))
					{
						Func = *It;
						break;
					}
				}
				if (Func) break;
			}

			if (Func)
			{
				UK2Node_CallFunction* FuncNode = NewObject<UK2Node_CallFunction>(Graph);
				FuncNode->SetFromFunction(Func);
				FuncNode->NodePosX = PosX;
				FuncNode->NodePosY = PosY;
				Graph->AddNode(FuncNode, false, false);
				FuncNode->AllocateDefaultPins();
				CreatedNode = FuncNode;
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Function not found: %s"), *NodeType));
			}
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("Unknown node type: %s"), *NodeType));
		}

		if (CreatedNode)
		{
			// Auto-set pin defaults from extra JSON fields (key, axisName, value, etc.)
			static const TSet<FString> NodeReservedKeys = { TEXT("ref"), TEXT("id"), TEXT("type"), TEXT("class"),
				TEXT("function"), TEXT("event"), TEXT("target"), TEXT("action"), TEXT("variable"),
				TEXT("x"), TEXT("y"), TEXT("posX"), TEXT("posY"), TEXT("params") };

			const UEdGraphSchema_K2* AutoSchema = GetDefault<UEdGraphSchema_K2>();
			for (const auto& Pair : Obj->Values)
			{
				if (NodeReservedKeys.Contains(Pair.Key)) continue;

				FString PinVal;
				if (Pair.Value->Type == EJson::String) PinVal = Pair.Value->AsString();
				else if (Pair.Value->Type == EJson::Number) PinVal = FString::SanitizeFloat(Pair.Value->AsNumber());
				else if (Pair.Value->Type == EJson::Boolean) PinVal = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
				else continue;

				// Find pin by name (case-insensitive)
				for (UEdGraphPin* Pin : CreatedNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input &&
						Pin->PinName.ToString().Equals(Pair.Key, ESearchCase::IgnoreCase))
					{
						AutoSchema->TrySetDefaultValue(*Pin, PinVal);
						break;
					}
				}
			}

			StoreNodeRef(Ref.IsEmpty() ? FString::Printf(TEXT("node_%d"), Added) : Ref, CreatedNode, GraphName);
			// Also register a secondary ref keyed on the node's editable title
			// so the LLM can connect later via the friendly name it sees in
			// read_blueprint output. If a node with that title already exists
			// in the ref map, append an index suffix so each node stays
			// uniquely addressable (e.g. "Add Actor Local Rotation",
			// "Add Actor Local Rotation 2").
			if (CreatedNode)
			{
				const FString Title = CreatedNode->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
				if (!Title.IsEmpty())
				{
					FString Key = Title;
					int32 Suffix = 2;
					while (NodeRefs.Contains(Key))
					{
						Key = FString::Printf(TEXT("%s %d"), *Title, Suffix++);
					}
					StoreNodeRef(Key, CreatedNode, GraphName);
				}
			}
			Added++;
			if (NodeDetail.IsEmpty()) NodeDetail = CreatedNode->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
			NodeDetail += FString::Printf(TEXT(" guid:%s"), *CreatedNode->NodeGuid.ToString());
			NodeDetails.Add(NodeDetail);
		}

		NodeX += 300;
		if (Added % 5 == 0)
		{
			NodeX = 300;
			NodeY += 300;
		}
	}

	FString Msg = FString::Printf(TEXT("Added %d node(s)"), Added);
	if (NodeDetails.Num() > 0) Msg += TEXT(". Details: ") + FString::Join(NodeDetails, TEXT("; "));
	if (Skipped > 0) Msg += FString::Printf(TEXT(", skipped %d duplicate(s)"), Skipped);
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));

	return Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// CONNECT PINS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoConnectPins(UBlueprint* BP, const FString& GraphName, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph) return FNwiroIKBPResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	int32 Connected = 0;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		// Format: "from"/"source": "nodeRef.PinName", "to"/"target": "nodeRef.PinName"
		FString FromStr = Obj->HasField(TEXT("from")) ? Obj->GetStringField(TEXT("from"))
			: Obj->HasField(TEXT("source")) ? Obj->GetStringField(TEXT("source")) : TEXT("");
		FString ToStr = Obj->HasField(TEXT("to")) ? Obj->GetStringField(TEXT("to"))
			: Obj->HasField(TEXT("target")) ? Obj->GetStringField(TEXT("target")) : TEXT("");

		if (FromStr.IsEmpty() || ToStr.IsEmpty())
		{
			Errors.Add(TEXT("Connection needs both 'from' and 'to'"));
			continue;
		}

		// Build a snapshot of every available "ref.pin" combo in this graph,
		// so we can hand it back in error messages and let the LLM self-correct
		// instead of nuking the graph and starting over.
		auto BuildAvailableRefs = [&]() -> FString
		{
			TArray<FString> Lines;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				const FString Title = Node->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
				TArray<FString> InPins, OutPins;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin) continue;
					(Pin->Direction == EGPD_Input ? InPins : OutPins).Add(Pin->PinName.ToString());
				}
				Lines.Add(FString::Printf(TEXT("'%s' inputs=[%s] outputs=[%s]"),
					*Title,
					*FString::Join(InPins, TEXT(",")),
					*FString::Join(OutPins, TEXT(","))));
			}
			return FString::Join(Lines, TEXT(" | "));
		};

		// Parse "ref.pinName" or "ref:pinName" — accept either separator. The
		// LLM keeps mixing them up. Splits at the LAST dot/colon so node refs
		// containing periods (e.g. asset paths) still work.
		enum class EParseResult : uint8 { Ok, NoDot, NodeNotFound, PinNotFound };
		auto ParsePinRef = [&](const FString& Str, UEdGraphNode*& OutNode, UEdGraphPin*& OutPin) -> EParseResult
		{
			int32 SepIdx = INDEX_NONE;
			Str.FindLastChar('.', SepIdx);
			int32 ColonIdx = INDEX_NONE;
			Str.FindLastChar(':', ColonIdx);
			if (ColonIdx > SepIdx) SepIdx = ColonIdx;
			if (SepIdx == INDEX_NONE) return EParseResult::NoDot;

			const FString NodeRef = Str.Left(SepIdx);
			const FString PinName = Str.Mid(SepIdx + 1);

			OutNode = FindNodeByRef(Graph, NodeRef);
			if (!OutNode) return EParseResult::NodeNotFound;

			OutPin = FindPin(OutNode, PinName);
			return OutPin ? EParseResult::Ok : EParseResult::PinNotFound;
		};

		UEdGraphNode* FromNode = nullptr;
		UEdGraphPin* FromPin = nullptr;
		UEdGraphNode* ToNode = nullptr;
		UEdGraphPin* ToPin = nullptr;

		auto ExplainFailure = [&](const FString& Str, EParseResult R, UEdGraphNode* Node) -> FString
		{
			switch (R)
			{
				case EParseResult::NoDot:
					return FString::Printf(TEXT("'%s' missing dot — expected format 'NodeRef.PinName'"), *Str);
				case EParseResult::NodeNotFound:
					return FString::Printf(TEXT("Node not found in '%s'. Available nodes/pins: %s"),
						*Str, *BuildAvailableRefs());
				case EParseResult::PinNotFound:
				{
					TArray<FString> InPins, OutPins;
					if (Node) for (UEdGraphPin* Pin : Node->Pins)
					{
						if (Pin) (Pin->Direction == EGPD_Input ? InPins : OutPins).Add(Pin->PinName.ToString());
					}
					return FString::Printf(TEXT("Pin not found in '%s'. The node exists but its pins are inputs=[%s] outputs=[%s]"),
						*Str,
						*FString::Join(InPins, TEXT(",")),
						*FString::Join(OutPins, TEXT(",")));
				}
				default:
					return FString::Printf(TEXT("Unknown parse error for '%s'"), *Str);
			}
		};

		const EParseResult FromR = ParsePinRef(FromStr, FromNode, FromPin);
		if (FromR != EParseResult::Ok)
		{
			Errors.Add(ExplainFailure(FromStr, FromR, FromNode));
			continue;
		}

		const EParseResult ToR = ParsePinRef(ToStr, ToNode, ToPin);
		if (ToR != EParseResult::Ok)
		{
			Errors.Add(ExplainFailure(ToStr, ToR, ToNode));
			continue;
		}

		// Pre-check: reject unrelated PC_Object connections that TryCreateConnection
		// silently accepts but the compiler then rejects (e.g. Actor → StaticMeshComponent).
		{
			UClass* FromClass = Cast<UClass>(FromPin->PinType.PinSubCategoryObject.Get());
			UClass* ToClass   = Cast<UClass>(ToPin->PinType.PinSubCategoryObject.Get());
			const bool bFromIsObj = FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object;
			const bool bToIsObj   = ToPin->PinType.PinCategory   == UEdGraphSchema_K2::PC_Object;
			if (bFromIsObj && bToIsObj && FromClass && ToClass &&
				!FromClass->IsChildOf(ToClass) && !ToClass->IsChildOf(FromClass))
			{
				Errors.Add(FString::Printf(
					TEXT("Type mismatch: cannot connect %s (%s) to %s (%s) — unrelated types. Did you mean to call Get<ComponentName> first?"),
					*FromStr, *FromClass->GetName(), *ToStr, *ToClass->GetName()));
				continue;
			}
		}

		// Try to connect
		bool bConnected = Schema->TryCreateConnection(FromPin, ToPin);

		if (!bConnected)
		{
			// Try swapping direction (schema may auto-resolve)
			bConnected = Schema->TryCreateConnection(ToPin, FromPin);
		}

		if (bConnected)
		{
			Connected++;
		}
		else
		{
			FString Reason;
			if (FromPin->PinType.PinCategory == ToPin->PinType.PinCategory)
			{
				UClass* FC = Cast<UClass>(FromPin->PinType.PinSubCategoryObject.Get());
				UClass* TC = Cast<UClass>(ToPin->PinType.PinSubCategoryObject.Get());
				if (!FC && TC)
					Reason = FString::Printf(TEXT(" (type mismatch: source is untyped UObject but destination requires %s — did you forget 'subtype'?)"), *TC->GetName());
				else if (FC && TC && !FC->IsChildOf(TC) && !TC->IsChildOf(FC))
					Reason = FString::Printf(TEXT(" (type mismatch: %s is not a %s)"), *FC->GetName(), *TC->GetName());
			}
			Errors.Add(FString::Printf(TEXT("Failed to connect: %s -> %s%s"), *FromStr, *ToStr, *Reason));
		}
	}

	FString Msg = FString::Printf(TEXT("Connected %d pin pair(s)"), Connected);
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));

	return Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// SET PIN DEFAULTS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoSetPinDefaults(UBlueprint* BP, const FString& GraphName, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph) return FNwiroIKBPResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	int32 Set = 0;
	TArray<FString> Errors;
	TArray<FString> Details;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		// Accept either "node" or "ref" for consistency with add_nodes/connect_pins
		FString NodeRef = Obj->HasField(TEXT("ref")) ? Obj->GetStringField(TEXT("ref")) : Obj->GetStringField(TEXT("node"));
		FString PinName = Obj->GetStringField(TEXT("pin"));
		FString Value = Obj->GetStringField(TEXT("value"));

		UEdGraphNode* Node = FindNodeByRef(Graph, NodeRef);
		if (!Node) { Errors.Add(FString::Printf(TEXT("Node not found: %s"), *NodeRef)); continue; }

		UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
		if (!Pin) { Errors.Add(FString::Printf(TEXT("'%s.%s' [not_resolved] pin not found — check pin name"), *NodeRef, *PinName)); continue; }

		// For Class pins, resolve the class by name and set as DefaultObject
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
		{
			UClass* ResolvedClass = FindFirstObject<UClass>(*Value);
			if (!ResolvedClass)
				ResolvedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *Value));
			if (!ResolvedClass)
				ResolvedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *Value);
			if (ResolvedClass)
				Schema->TrySetDefaultObject(*Pin, ResolvedClass);
			else
				Schema->TrySetDefaultValue(*Pin, Value);
		}
		// For Object pins, try loading the asset and using TrySetDefaultObject
		else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface ||
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
		{
			UObject* Asset = LoadObject<UObject>(nullptr, *Value);
			if (!Asset)
			{
				// Try searching asset registry
				FAssetRegistryModule& PinARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				PinARM.Get().ScanPathsSynchronous({TEXT("/Game")}, true);
				TArray<FAssetData> PinAssets;
				PinARM.Get().GetAssetsByPackageName(FName(*Value.Left(Value.Find(TEXT(".")))), PinAssets);
				if (PinAssets.Num() > 0) Asset = PinAssets[0].GetAsset();
			}
			if (Asset)
			{
				Schema->TrySetDefaultObject(*Pin, Asset);
			}
			else
			{
				Schema->TrySetDefaultValue(*Pin, Value);
			}
		}
		else
		{
			Schema->TrySetDefaultValue(*Pin, Value);
		}
		Set++;
		Details.Add(FString::Printf(TEXT("'%s.%s'=%s [resolved]"), *NodeRef, *PinName, *Value));
	}

	FString Msg = FString::Printf(TEXT("Set %d pin default(s)"), Set);
	if (Details.Num() > 0) Msg += TEXT(": ") + FString::Join(Details, TEXT(", "));
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));

	return Set > 0 || Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// CREATE BLUEPRINT
// ============================================================

FString FNwiroIKBlueprintTools::CreateBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Invalid JSON\"}");
	}

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	FString ParentClassName = Cmd->GetStringField(TEXT("parentClass"));

	if (Name.IsEmpty())
	{
		return TEXT("{\"success\": false, \"error\": \"Missing 'name' field\"}");
	}

	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	// Find parent class
	UClass* ParentClass = AActor::StaticClass(); // default
	if (!ParentClassName.IsEmpty())
	{
		UClass* Found = FindClassByName(ParentClassName);
		if (Found) ParentClass = Found;
	}

	FString FullPath = Path / Name;

	// Check if blueprint already exists
	UBlueprint* ExistingBP = LoadObject<UBlueprint>(nullptr, *FullPath);
	if (ExistingBP)
	{
		return FString::Printf(TEXT("{\"success\": true, \"name\": \"%s\", \"path\": \"%s\", \"message\": \"Blueprint already exists\"}"), *Name, *ExistingBP->GetPathName());
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateBlueprint", "AI: Create Blueprint"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\": false, \"error\": \"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UBlueprint* NewBP = nullptr;

	{
		// Prevent GC during entire create+compile cycle
		FGCScopeGuard GCScopeGuard;

		Package->AddToRoot();

		NewBP = FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());

		if (!NewBP)
		{
			Package->RemoveFromRoot();
			return TEXT("{\"success\": false, \"error\": \"Failed to create blueprint\"}");
		}

		NewBP->AddToRoot();
		FAssetRegistryModule::AssetCreated(NewBP);
		NewBP->MarkPackageDirty();

		FKismetEditorUtilities::CompileBlueprint(NewBP);

		NewBP->RemoveFromRoot();
		Package->RemoveFromRoot();
	}

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), NewBP->GetPathName());
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetName());

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);

	UE_LOG(LogNwiroBP, Log, TEXT("Created blueprint: %s (parent: %s)"), *Name, *ParentClass->GetName());
	return Out;
}

// ============================================================
// CLASS RESOLUTION (static method)
// ============================================================

UClass* FNwiroIKBlueprintTools::FindClassByName(const FString& Name)
{
	if (Name.IsEmpty()) return nullptr;

	UClass* C = FindFirstObject<UClass>(*Name);
	if (C) return C;

	C = FindFirstObject<UClass>(*(TEXT("U") + Name));
	if (C) return C;
	C = FindFirstObject<UClass>(*(TEXT("A") + Name));
	if (C) return C;

	// Common aliases
	static TMap<FString, FString> Aliases = {
		{ TEXT("character"), TEXT("ACharacter") },
		{ TEXT("pawn"), TEXT("APawn") },
		{ TEXT("actor"), TEXT("AActor") },
		{ TEXT("playercontroller"), TEXT("APlayerController") },
		{ TEXT("gamemode"), TEXT("AGameModeBase") },
		{ TEXT("gamestate"), TEXT("AGameStateBase") },
		{ TEXT("playerstate"), TEXT("APlayerState") },
		{ TEXT("hud"), TEXT("AHUD") },
		{ TEXT("charactermovementcomponent"), TEXT("UCharacterMovementComponent") },
		{ TEXT("charactermovement"), TEXT("UCharacterMovementComponent") },
		{ TEXT("movementcomponent"), TEXT("UMovementComponent") },
		{ TEXT("scenecomponent"), TEXT("USceneComponent") },
		{ TEXT("actorcomponent"), TEXT("UActorComponent") },
		{ TEXT("staticmeshcomponent"), TEXT("UStaticMeshComponent") },
		{ TEXT("skeletalmeshcomponent"), TEXT("USkeletalMeshComponent") },
		{ TEXT("capsulecomponent"), TEXT("UCapsuleComponent") },
		{ TEXT("springarmcomponent"), TEXT("USpringArmComponent") },
		{ TEXT("cameracomponent"), TEXT("UCameraComponent") },
		{ TEXT("widgetcomponent"), TEXT("UWidgetComponent") },
		{ TEXT("audiocomponent"), TEXT("UAudioComponent") },
		{ TEXT("pointlightcomponent"), TEXT("UPointLightComponent") },
		{ TEXT("spotlightcomponent"), TEXT("USpotLightComponent") },
		{ TEXT("particlesystemcomponent"), TEXT("UParticleSystemComponent") },
		{ TEXT("niagaracomponent"), TEXT("UNiagaraComponent") },
		{ TEXT("boxcollision"), TEXT("UBoxComponent") },
		{ TEXT("spherecollision"), TEXT("USphereComponent") },
		{ TEXT("arrowcomponent"), TEXT("UArrowComponent") },
		{ TEXT("enhancedinputlocalplayersubsystem"), TEXT("UEnhancedInputLocalPlayerSubsystem") },
		{ TEXT("enhancedinputsubsystem"), TEXT("UEnhancedInputLocalPlayerSubsystem") },
		{ TEXT("inputsubsystem"), TEXT("UEnhancedInputLocalPlayerSubsystem") },
	};

	if (const FString* Alias = Aliases.Find(Name.ToLower()))
	{
		C = FindFirstObject<UClass>(**Alias);
		if (C) return C;
	}

	// Try with Component suffix
	C = FindFirstObject<UClass>(*(Name + TEXT("Component")));
	if (C) return C;
	C = FindFirstObject<UClass>(*(TEXT("U") + Name + TEXT("Component")));
	if (C) return C;

	// Force-load engine class if not yet in memory
	C = StaticLoadClass(UObject::StaticClass(), nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *Name));
	if (C) return C;
	C = StaticLoadClass(UObject::StaticClass(), nullptr, *Name);
	if (C) return C;

	return nullptr;
}

// ============================================================
// RENAME VARIABLES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoRenameVariables(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Renamed = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString OldName = Obj->GetStringField(TEXT("old"));
		FString NewName = Obj->GetStringField(TEXT("new"));
		if (OldName.IsEmpty() || NewName.IsEmpty()) continue;

		FBlueprintEditorUtils::RenameMemberVariable(BP, FName(*OldName), FName(*NewName));
		Renamed++;
	}
	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Renamed %d variable(s)"), Renamed));
}

// ============================================================
// SET COMPONENT PROPERTIES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoSetComponentProperties(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	if (!BP->SimpleConstructionScript) return FNwiroIKBPResult::Fail(TEXT("No SCS"));

	int32 Set = 0;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString CompName = Obj->GetStringField(TEXT("component"));
		FString PropName = Obj->GetStringField(TEXT("property"));
		FString Value = Obj->GetStringField(TEXT("value"));

		if (CompName.IsEmpty() || PropName.IsEmpty()) continue;

		// Find the component template
		UActorComponent* CompTemplate = nullptr;
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(CompName, ESearchCase::IgnoreCase))
			{
				CompTemplate = Node->ComponentTemplate;
				break;
			}
		}

		if (!CompTemplate)
		{
			Errors.Add(FString::Printf(TEXT("Component not found: %s"), *CompName));
			continue;
		}

		if (ApplyComponentProperty(CompTemplate, PropName, Value, CompName, &Errors, nullptr))
			Set++;
	}

	FString Msg = FString::Printf(TEXT("Set %d component property(ies)"), Set);
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));
	return Set > 0 || Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// REMOVE FUNCTIONS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoRemoveFunctions(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		FString FuncName;
		if (Item->Type == EJson::String) FuncName = Item->AsString();
		else if (Item->Type == EJson::Object && Item->AsObject().IsValid()) FuncName = Item->AsObject()->GetStringField(TEXT("name"));

		if (!FuncName.IsEmpty())
		{
			FBlueprintEditorUtils::RemoveGraph(BP, FindGraph(BP, FuncName));
			Removed++;
		}
	}
	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Removed %d function(s)"), Removed));
}

// ============================================================
// ADD EVENT DISPATCHERS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddEventDispatchers(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Added = 0;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString DispName = Obj->GetStringField(TEXT("name"));
		if (DispName.IsEmpty()) continue;

		FName DispFName(*DispName);

		// Check if already exists
		bool bExists = false;
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			if (Var.VarName == DispFName)
			{
				bExists = true;
				break;
			}
		}
		if (bExists) { Errors.Add(FString::Printf(TEXT("'%s' already exists"), *DispName)); continue; }

		// Create the event dispatcher
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
		FBlueprintEditorUtils::AddMemberVariable(BP, DispFName, PinType);

		// Add parameters if specified
		const TArray<TSharedPtr<FJsonValue>>* Params;
		if (Obj->TryGetArrayField(TEXT("params"), Params))
		{
			// Params would need to be added to the delegate signature
			// This is complex - for now just create the dispatcher
		}

		Added++;
		UE_LOG(LogNwiroBP, Log, TEXT("Added event dispatcher: %s"), *DispName);
	}

	FString Msg = FString::Printf(TEXT("Added %d event dispatcher(s)"), Added);
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));
	return Added > 0 || Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

// ============================================================
// ADD/REMOVE INTERFACES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoAddInterfaces(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Added = 0;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		FString IfaceName;
		if (Item->Type == EJson::String) IfaceName = Item->AsString();
		else if (Item->Type == EJson::Object && Item->AsObject().IsValid()) IfaceName = Item->AsObject()->GetStringField(TEXT("name"));
		if (IfaceName.IsEmpty()) continue;

		UClass* IfaceClass = FindClassByName(IfaceName);
		if (!IfaceClass || !IfaceClass->IsChildOf(UInterface::StaticClass()))
		{
			// Try with I prefix (interface convention)
			IfaceClass = FindFirstObject<UClass>(*(TEXT("U") + IfaceName));
			if (!IfaceClass)
			{
				Errors.Add(FString::Printf(TEXT("Interface not found: %s"), *IfaceName));
				continue;
			}
		}

		FBlueprintEditorUtils::ImplementNewInterface(BP, FTopLevelAssetPath(IfaceClass->GetPathName()));
		Added++;
	}

	FString Msg = FString::Printf(TEXT("Added %d interface(s)"), Added);
	if (Errors.Num() > 0) Msg += TEXT(". Errors: ") + FString::Join(Errors, TEXT("; "));
	return Added > 0 || Errors.Num() == 0 ? FNwiroIKBPResult::Ok(Msg) : FNwiroIKBPResult::Fail(Msg);
}

FNwiroIKBPResult FNwiroIKBlueprintTools::DoRemoveInterfaces(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		FString IfaceName;
		if (Item->Type == EJson::String) IfaceName = Item->AsString();
		else if (Item->Type == EJson::Object && Item->AsObject().IsValid()) IfaceName = Item->AsObject()->GetStringField(TEXT("name"));
		if (IfaceName.IsEmpty()) continue;

		UClass* IfaceClass = FindClassByName(IfaceName);
		if (IfaceClass)
		{
			FBlueprintEditorUtils::RemoveInterface(BP, FTopLevelAssetPath(IfaceClass->GetPathName()));
			Removed++;
		}
	}
	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Removed %d interface(s)"), Removed));
}

// ============================================================
// REPARENT
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoReparent(UBlueprint* BP, const FString& NewParentClass)
{
	UClass* NewParent = FindClassByName(NewParentClass);
	if (!NewParent)
	{
		return FNwiroIKBPResult::Fail(FString::Printf(TEXT("Parent class not found: %s"), *NewParentClass));
	}

	BP->ParentClass = NewParent;
	FBlueprintEditorUtils::RefreshAllNodes(BP);
	UE_LOG(LogNwiroBP, Log, TEXT("Reparented to: %s"), *NewParent->GetName());
	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Reparented to %s [resolved] path:%s"), *NewParent->GetName(), *NewParent->GetPathName()));
}

// ============================================================
// REMOVE NODES
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoRemoveNodes(UBlueprint* BP, const FString& GraphName, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph) return FNwiroIKBPResult::Fail(TEXT("Graph not found"));

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		FString Ref;
		if (Item->Type == EJson::String) Ref = Item->AsString();
		else if (Item->Type == EJson::Object) Ref = Item->AsObject()->GetStringField(TEXT("ref"));
		if (Ref.IsEmpty()) continue;

		UEdGraphNode* Node = FindNodeByRef(Graph, Ref);
		if (Node)
		{
			FBlueprintEditorUtils::RemoveNode(BP, Node);
			Removed++;
		}
	}
	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Removed %d node(s)"), Removed));
}

// ============================================================
// BREAK CONNECTIONS
// ============================================================

FNwiroIKBPResult FNwiroIKBlueprintTools::DoBreakConnections(UBlueprint* BP, const FString& GraphName, const TArray<TSharedPtr<FJsonValue>>& Items)
{
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph) return FNwiroIKBPResult::Fail(TEXT("Graph not found"));

	int32 Broken = 0;
	for (const TSharedPtr<FJsonValue>& Item : Items)
	{
		const TSharedPtr<FJsonObject>& Obj = Item->AsObject();
		if (!Obj.IsValid()) continue;

		FString NodeRef = Obj->HasField(TEXT("ref")) ? Obj->GetStringField(TEXT("ref")) : Obj->GetStringField(TEXT("node"));
		FString PinName = Obj->GetStringField(TEXT("pin"));

		UEdGraphNode* Node = FindNodeByRef(Graph, NodeRef);
		if (!Node) continue;

        if (PinName.IsEmpty())
		{
			// Break all connections on this node
			Node->BreakAllNodeLinks();
			Broken++;
		}
		else
		{
			UEdGraphPin* Pin = FindPin(Node, PinName);
			if (Pin)
			{
				Pin->BreakAllPinLinks();
				Broken++;
			}
		}
	}
	return FNwiroIKBPResult::Ok(FString::Printf(TEXT("Broke %d connection(s)"), Broken));
}

// ============================================================
// SERIALIZATION HELPERS
// ============================================================

TSharedPtr<FJsonObject> FNwiroIKBlueprintTools::SerializeVariable(UBlueprint* BP, const FName& VarName)
{
	int32 Idx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarName);
	if (Idx == INDEX_NONE) return nullptr;

	const FBPVariableDescription& Var = BP->NewVariables[Idx];

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("name"), VarName.ToString());
	Obj->SetStringField(TEXT("type"), PinTypeToString(Var.VarType));
	Obj->SetStringField(TEXT("defaultValue"), Var.DefaultValue);
	Obj->SetStringField(TEXT("category"), Var.Category.ToString());

	// Flags
	Obj->SetBoolField(TEXT("replicated"), (Var.PropertyFlags & CPF_Net) != 0);
	Obj->SetBoolField(TEXT("saveGame"), (Var.PropertyFlags & CPF_SaveGame) != 0);

	return Obj;
}

TSharedPtr<FJsonObject> FNwiroIKBlueprintTools::SerializeComponent(const UActorComponent* Comp, const FName& VarName)
{
	if (!Comp) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("name"), VarName.ToString());
	Obj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

	if (const USceneComponent* Scene = Cast<USceneComponent>(Comp))
	{
		TSharedPtr<FJsonObject> Loc = MakeShareable(new FJsonObject());
		Loc->SetNumberField(TEXT("x"), Scene->GetRelativeLocation().X);
		Loc->SetNumberField(TEXT("y"), Scene->GetRelativeLocation().Y);
		Loc->SetNumberField(TEXT("z"), Scene->GetRelativeLocation().Z);
		Obj->SetObjectField(TEXT("location"), Loc);

		FVector Scale = Scene->GetRelativeScale3D();
		if (Scale != FVector::OneVector)
		{
			Obj->SetStringField(TEXT("scale"), FString::Printf(TEXT("(%.2f,%.2f,%.2f)"), Scale.X, Scale.Y, Scale.Z));
		}

		Obj->SetStringField(TEXT("mobility"), Scene->Mobility == EComponentMobility::Static ? TEXT("Static") :
			Scene->Mobility == EComponentMobility::Stationary ? TEXT("Stationary") : TEXT("Movable"));
	}

	// StaticMeshComponent details
	if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
	{
		if (SMC->GetStaticMesh())
			Obj->SetStringField(TEXT("staticMesh"), SMC->GetStaticMesh()->GetPathName());
		else
			Obj->SetStringField(TEXT("staticMesh"), TEXT("None"));
	}

	// Physics info
	if (const UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Comp))
	{
		Obj->SetBoolField(TEXT("simulatePhysics"), Prim->BodyInstance.bSimulatePhysics);
		Obj->SetBoolField(TEXT("generateOverlapEvents"), Prim->GetGenerateOverlapEvents());
	}

	return Obj;
}

TSharedPtr<FJsonObject> FNwiroIKBlueprintTools::SerializeGraph(UEdGraph* Graph, bool bIncludeNodes)
{
	if (!Graph) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("name"), Graph->GetName());
	Obj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

	if (bIncludeNodes)
	{
		TArray<TSharedPtr<FJsonValue>> NodeArr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			TSharedPtr<FJsonObject> NObj = SerializeNode(Node);
			if (NObj.IsValid())
			{
				NodeArr.Add(MakeShareable(new FJsonValueObject(NObj.ToSharedRef())));
			}
		}
		Obj->SetArrayField(TEXT("nodes"), NodeArr);
	}

	return Obj;
}

TSharedPtr<FJsonObject> FNwiroIKBlueprintTools::SerializeNode(UEdGraphNode* Node)
{
	if (!Node) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	Obj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
	// `ref` is what connect_pins expects in the "from"/"to" strings before the
	// dot. Look it up in NodeRefs (the auto-stored secondary key, possibly
	// suffixed for uniqueness) so each node in the JSON output gets a unique
	// handle even when multiple nodes share the same title.
	{
		FString RefValue;
		for (const auto& Pair : NodeRefs)
		{
			if (Pair.Value.NodeGuid == Node->NodeGuid)
			{
				// Prefer keys that look like the title (skip generic "node_N").
				if (RefValue.IsEmpty() || (RefValue.StartsWith(TEXT("node_")) && !Pair.Key.StartsWith(TEXT("node_"))))
				{
					RefValue = Pair.Key;
				}
			}
		}
		if (RefValue.IsEmpty())
		{
			RefValue = Node->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
		}
		Obj->SetStringField(TEXT("ref"), RefValue);
	}
	Obj->SetNumberField(TEXT("x"), Node->NodePosX);
	Obj->SetNumberField(TEXT("y"), Node->NodePosY);

	// Serialize pins
	TArray<TSharedPtr<FJsonValue>> PinArr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;

		TSharedPtr<FJsonObject> PObj = MakeShareable(new FJsonObject());
		PObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
		PObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
		PObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);

		// Show what this pin is connected to
		if (Pin->LinkedTo.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> LinkedArr;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
				{
					FString ConnStr = FString::Printf(TEXT("%s.%s"),
						*Linked->GetOwningNode()->NodeGuid.ToString(),
						*Linked->PinName.ToString());
					LinkedArr.Add(MakeShareable(new FJsonValueString(ConnStr)));
				}
			}
			PObj->SetArrayField(TEXT("connectedTo"), LinkedArr);
		}

		// Show DefaultObject for object reference pins
		if (Pin->DefaultObject)
		{
			PObj->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
		}

		if (!Pin->PinFriendlyName.IsEmpty())
		{
			PObj->SetStringField(TEXT("friendlyName"), Pin->PinFriendlyName.ToString());
		}

		PinArr.Add(MakeShareable(new FJsonValueObject(PObj.ToSharedRef())));
	}
	Obj->SetArrayField(TEXT("pins"), PinArr);

	return Obj;
}

FString FNwiroIKBlueprintTools::PinTypeToString(const FEdGraphPinType& PinType)
{
	FString Result;

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean) Result = TEXT("Boolean");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte) Result = TEXT("Byte");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int) Result = TEXT("Int");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64) Result = TEXT("Int64");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real) Result = PinType.PinSubCategory.ToString();
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String) Result = TEXT("String");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name) Result = TEXT("Name");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text) Result = TEXT("Text");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) Result = TEXT("Exec");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		Result = TEXT("Object");
		if (PinType.PinSubCategoryObject.IsValid())
		{
			Result += TEXT(":") + PinType.PinSubCategoryObject->GetName();
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		Result = TEXT("Struct");
		if (PinType.PinSubCategoryObject.IsValid())
		{
			Result = PinType.PinSubCategoryObject->GetName();
		}
	}
	else
	{
		Result = PinType.PinCategory.ToString();
	}

	// Container
	if (PinType.ContainerType == EPinContainerType::Array)
	{
		Result = TEXT("Array<") + Result + TEXT(">");
	}
	else if (PinType.ContainerType == EPinContainerType::Set)
	{
		Result = TEXT("Set<") + Result + TEXT(">");
	}
	else if (PinType.ContainerType == EPinContainerType::Map)
	{
		Result = TEXT("Map<") + Result + TEXT(">");
	}

	return Result;
}

// ============================================================
// DELETE BLUEPRINT
// ============================================================

// ============================================================
// CLEAR GRAPH — remove all nodes from EventGraph
// ============================================================

FString FNwiroIKBlueprintTools::ClearGraph(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	// Hard guard: clear_graph is destructive. The LLM kept calling it as a
	// "reset" reflex whenever connect_pins failed, wiping out work in progress.
	// Refuse the call unless it carries an explicit confirm flag — the system
	// prompt instructs the model to set this only when the USER literally
	// asked to start the graph over.
	FString Confirm;
	Cmd->TryGetStringField(TEXT("confirm"), Confirm);
	if (!Confirm.Equals(TEXT("user_requested"), ESearchCase::IgnoreCase))
	{
		return TEXT("{\"success\":false,\"error\":\"clear_graph refused: this is destructive. Read the connect_pins / edit_blueprint error message — it lists the available node refs and pin names so you can self-correct without wiping the graph. Only call clear_graph again with arguments.confirm=\\\"user_requested\\\" if the user literally said to start over.\"}");
	}

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	FString GraphName = Cmd->HasField(TEXT("graph")) ? Cmd->GetStringField(TEXT("graph")) : TEXT("EventGraph");

	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph) return TEXT("{\"success\":false,\"error\":\"Graph not found\"}");

	int32 Removed = 0;
	TArray<UEdGraphNode*> NodesToRemove;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (!Node->CanUserDeleteNode()) continue;
		if (Node->IsA<UK2Node_FunctionEntry>()) continue;
		if (Node->IsA<UK2Node_FunctionResult>()) continue;
		if (Node->IsA<UK2Node_Event>()) continue;
		if (Node->IsA<UK2Node_Tunnel>()) continue;
		NodesToRemove.Add(Node);
	}

	for (UEdGraphNode* Node : NodesToRemove)
	{
		FBlueprintEditorUtils::RemoveNode(BP, Node);
		Removed++;
	}

	// Clear node refs
	NodeRefs.Empty();

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);

	return FString::Printf(TEXT("{\"success\":true,\"removed\":%d,\"message\":\"Graph cleared and compiled\"}"), Removed);
}

FString FNwiroIKBlueprintTools::DeleteBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString AssetPath = Cmd->GetStringField(TEXT("assetPath"));
	if (AssetPath.IsEmpty()) AssetPath = Cmd->GetStringField(TEXT("path"));
	if (AssetPath.IsEmpty()) AssetPath = Cmd->GetStringField(TEXT("name"));

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *AssetPath);

	FString FullPath = BP->GetPathName();
	FString PackagePath = BP->GetPackage()->GetName();

	if (UEditorAssetLibrary::DeleteAsset(PackagePath))
		return FString::Printf(TEXT("{\"success\":true,\"deleted\":\"%s\"}"), *FullPath);

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to delete: %s\"}"), *FullPath);
}

// ============================================================
// DUPLICATE BLUEPRINT
// ============================================================

FString FNwiroIKBlueprintTools::DuplicateBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString SourcePath = Cmd->GetStringField(TEXT("source"));
	if (SourcePath.IsEmpty()) SourcePath = Cmd->GetStringField(TEXT("assetPath"));
	FString NewName = Cmd->GetStringField(TEXT("newName"));
	FString DestPath = Cmd->GetStringField(TEXT("destinationPath"));

	UBlueprint* SourceBP = LoadBP(SourcePath);
	if (!SourceBP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Source blueprint not found: %s\"}"), *SourcePath);

	if (DestPath.IsEmpty()) DestPath = FPaths::GetPath(SourceBP->GetPackage()->GetName());
	if (NewName.IsEmpty()) NewName = SourceBP->GetName() + TEXT("_Copy");

	FString FullDestPath = DestPath / NewName;
	FString SourcePkg = SourceBP->GetPackage()->GetName();

	if (UEditorAssetLibrary::DuplicateAsset(SourcePkg, FullDestPath))
		return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\"}"), *NewName, *FullDestPath);

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to duplicate to: %s\"}"), *FullDestPath);
}

// ============================================================
// RENAME BLUEPRINT
// ============================================================

FString FNwiroIKBlueprintTools::RenameBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString AssetPath = Cmd->GetStringField(TEXT("assetPath"));
	if (AssetPath.IsEmpty()) AssetPath = Cmd->GetStringField(TEXT("source"));
	FString NewName = Cmd->GetStringField(TEXT("newName"));

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *AssetPath);

	if (NewName.IsEmpty())
		return TEXT("{\"success\":false,\"error\":\"newName is required\"}");

	FString SourcePkg = BP->GetPackage()->GetName();
	FString DestPkg = FPaths::GetPath(SourcePkg) / NewName;

	if (UEditorAssetLibrary::RenameAsset(SourcePkg, DestPkg))
		return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\"}"), *NewName, *DestPkg);

	return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to rename to: %s\"}"), *NewName);
}

// ============================================================
// DELETE NODE (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::DeleteNode(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	FString GraphName = Cmd->GetStringField(TEXT("graph"));
	if (GraphName.IsEmpty()) GraphName = TEXT("EventGraph");

	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	// Build array from "refs" array or single "ref"
	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* RefsArr;
	if (Cmd->TryGetArrayField(TEXT("refs"), RefsArr))
	{
		for (const auto& V : *RefsArr)
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("ref"), V->AsString());
			Items.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
	}
	else if (Cmd->HasField(TEXT("ref")))
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("ref"), Cmd->GetStringField(TEXT("ref")));
		Items.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "DeleteNode", "AI: Delete Node"), BP);
	FNwiroIKBPResult R = DoRemoveNodes(BP, GraphName, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// CREATE FUNCTION GRAPH (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::CreateFunctionGraph(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	// Build items array for DoAddFunctions
	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* FuncsArr;
	if (Cmd->TryGetArrayField(TEXT("functions"), FuncsArr))
	{
		Items = *FuncsArr;
	}
	else
	{
		// Single function from top-level fields
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("name"), Cmd->GetStringField(TEXT("name")));
		if (Cmd->HasField(TEXT("inputs"))) Obj->SetField(TEXT("inputs"), Cmd->TryGetField(TEXT("inputs")));
		if (Cmd->HasField(TEXT("outputs"))) Obj->SetField(TEXT("outputs"), Cmd->TryGetField(TEXT("outputs")));
		if (Cmd->HasField(TEXT("pure"))) Obj->SetBoolField(TEXT("pure"), Cmd->GetBoolField(TEXT("pure")));
		Items.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateFunctionGraph", "AI: Create Function Graph"), BP);
	FNwiroIKBPResult R = DoAddFunctions(BP, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// ADD INTERFACE (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::AddInterface(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Cmd->TryGetArrayField(TEXT("interfaces"), Arr))
	{
		Items = *Arr;
	}
	else if (Cmd->HasField(TEXT("interface")))
	{
		Items.Add(MakeShareable(new FJsonValueString(Cmd->GetStringField(TEXT("interface")))));
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "AddInterface", "AI: Add Interface"), BP);
	FNwiroIKBPResult R = DoAddInterfaces(BP, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// REMOVE INTERFACE (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::RemoveInterface(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Cmd->TryGetArrayField(TEXT("interfaces"), Arr))
	{
		Items = *Arr;
	}
	else if (Cmd->HasField(TEXT("interface")))
	{
		Items.Add(MakeShareable(new FJsonValueString(Cmd->GetStringField(TEXT("interface")))));
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "RemoveInterface", "AI: Remove Interface"), BP);
	FNwiroIKBPResult R = DoRemoveInterfaces(BP, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// CREATE EVENT DISPATCHER (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::CreateEventDispatcher(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Cmd->TryGetArrayField(TEXT("dispatchers"), Arr))
	{
		Items = *Arr;
	}
	else
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("name"), Cmd->GetStringField(TEXT("name")));
		if (Cmd->HasField(TEXT("params"))) Obj->SetField(TEXT("params"), Cmd->TryGetField(TEXT("params")));
		Items.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateEventDispatcher", "AI: Create Event Dispatcher"), BP);
	FNwiroIKBPResult R = DoAddEventDispatchers(BP, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// REPARENT BLUEPRINT (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::ReparentBlueprint(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	FString NewParent = Cmd->GetStringField(TEXT("parentClass"));
	if (NewParent.IsEmpty()) NewParent = Cmd->GetStringField(TEXT("parent"));

	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "ReparentBlueprint", "AI: Reparent Blueprint"), BP);
	FNwiroIKBPResult R = DoReparent(BP, NewParent);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// REMOVE COMPONENT (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::RemoveComponent(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Cmd->TryGetArrayField(TEXT("components"), Arr))
	{
		Items = *Arr;
	}
	else if (Cmd->HasField(TEXT("name")))
	{
		Items.Add(MakeShareable(new FJsonValueString(Cmd->GetStringField(TEXT("name")))));
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "RemoveComponent", "AI: Remove Component"), BP);
	FNwiroIKBPResult R = DoRemoveComponents(BP, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

// ============================================================
// EDIT COMPONENT (standalone)
// ============================================================

FString FNwiroIKBlueprintTools::EditComponent(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString BPName = Cmd->GetStringField(TEXT("blueprint"));
	UBlueprint* BP = LoadBP(BPName);
	if (!BP) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Blueprint not found: %s\"}"), *BPName);

	// Convert {name, properties:{K:V}} entries into flat {component, property, value} triplets
	// that DoSetComponentProperties expects.
	auto ExpandToTriplets = [](const FString& CompName, const TSharedPtr<FJsonObject>& PropsObj, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		for (const auto& Pair : PropsObj->Values)
		{
			FString ValStr;
			if (Pair.Value->Type == EJson::String)
				ValStr = Pair.Value->AsString();
			else if (Pair.Value->Type == EJson::Boolean)
				ValStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
			else if (Pair.Value->Type == EJson::Number)
				ValStr = FString::SanitizeFloat(Pair.Value->AsNumber());
			else
				continue;

			TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject());
			Item->SetStringField(TEXT("component"), CompName);
			Item->SetStringField(TEXT("property"), Pair.Key);
			Item->SetStringField(TEXT("value"), ValStr);
			Out.Add(MakeShareable(new FJsonValueObject(Item)));
		}
	};

	TArray<TSharedPtr<FJsonValue>> Items;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Cmd->TryGetArrayField(TEXT("components"), Arr))
	{
		for (const auto& CV : *Arr)
		{
			const TSharedPtr<FJsonObject>& CO = CV->AsObject();
			if (!CO.IsValid()) continue;
			FString CompName = CO->GetStringField(TEXT("name"));
			const TSharedPtr<FJsonObject>* PropsObj;
			if (CO->TryGetObjectField(TEXT("properties"), PropsObj))
				ExpandToTriplets(CompName, *PropsObj, Items);
		}
	}
	else
	{
		// Single component from top-level: {name, properties}
		FString CompName = Cmd->GetStringField(TEXT("name"));
		const TSharedPtr<FJsonObject>* PropsObj;
		if (Cmd->TryGetObjectField(TEXT("properties"), PropsObj))
			ExpandToTriplets(CompName, *PropsObj, Items);
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "EditComponent", "AI: Edit Component"), BP);
	FNwiroIKBPResult R = DoSetComponentProperties(BP, Items);

	if (R.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
	}

	return FString::Printf(TEXT("{\"success\":%s,\"message\":\"%s\"}"), R.bSuccess ? TEXT("true") : TEXT("false"), *R.Message);
}

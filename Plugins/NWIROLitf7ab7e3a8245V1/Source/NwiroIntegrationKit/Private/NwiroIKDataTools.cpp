// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKDataTools.h"
#include "NwiroIKTransactionHelper.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/DataTableFactory.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroData, Log, All);

// ============================================================
// CREATE DATA TABLE
// ============================================================

FString FNwiroIKDataTools::CreateDataTable(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	FString RowStructName = Cmd->GetStringField(TEXT("rowStruct"));

	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/Data");

	// Find the row struct
	UScriptStruct* RowStruct = nullptr;
	if (!RowStructName.IsEmpty())
	{
		RowStruct = FindObject<UScriptStruct>(nullptr, *RowStructName);
		if (!RowStruct)
		{
			// Try common paths
			RowStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *RowStructName));
		}
		if (!RowStruct)
		{
			// Search user defined structs
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			FARFilter Filter;
			Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(TEXT("/Game"));
			TArray<FAssetData> Assets;
			ARM.Get().GetAssets(Filter, Assets);
			for (const FAssetData& A : Assets)
			{
				if (A.AssetName.ToString().Equals(RowStructName, ESearchCase::IgnoreCase))
				{
					RowStruct = Cast<UScriptStruct>(A.GetAsset());
					break;
				}
			}
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	if (RowStruct)
	{
		Factory->Struct = RowStruct;
	}

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateDataTable", "AI: Create DataTable"));

	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UDataTable::StaticClass(), Factory);
	UDataTable* DT = Cast<UDataTable>(NewAsset);

	if (!DT)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create DataTable\"}");
	}

	Tx.AlsoModify(DT);
	DT->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"rowStruct\":\"%s\"}"),
		*Name, *DT->GetPathName(), RowStruct ? *RowStruct->GetName() : TEXT("None"));
}

// ============================================================
// ADD DATA TABLE ROW
// ============================================================

FString FNwiroIKDataTools::AddDataTableRow(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString TablePath = Cmd->GetStringField(TEXT("table"));
	FString RowName = Cmd->GetStringField(TEXT("rowName"));

	UObject* Asset = UEditorAssetLibrary::LoadAsset(TablePath);
	UDataTable* DT = Cast<UDataTable>(Asset);
	if (!DT)
	{
		// Search by name
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FARFilter Filter;
		Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(TEXT("/Game"));
		TArray<FAssetData> Assets;
		ARM.Get().GetAssets(Filter, Assets);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetName.ToString().Contains(TablePath, ESearchCase::IgnoreCase))
			{
				DT = Cast<UDataTable>(A.GetAsset());
				break;
			}
		}
	}

	if (!DT) return FString::Printf(TEXT("{\"success\":false,\"error\":\"DataTable not found: %s\"}"), *TablePath);
	if (RowName.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'rowName'\"}");

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "AddDataTableRow", "AI: Add DataTable Row"), DT);

	// Add row via JSON
	const TSharedPtr<FJsonObject>* ValuesObj;
	if (Cmd->TryGetObjectField(TEXT("values"), ValuesObj))
	{
		FString RowJson;
		TSharedRef<TJsonWriter<>> JW = TJsonWriterFactory<>::Create(&RowJson);
		FJsonSerializer::Serialize((*ValuesObj).ToSharedRef(), JW);

		FString JsonString = FString::Printf(TEXT("[{\"Name\":\"%s\",%s}]"),
			*RowName, *RowJson.Mid(1, RowJson.Len() - 2)); // Strip outer {}

		TArray<FString> Problems;
		FString Unused;
		DT->CreateTableFromJSONString(JsonString);
	}

	DT->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"table\":\"%s\",\"row\":\"%s\"}"), *DT->GetName(), *RowName);
}

// ============================================================
// READ DATA TABLE
// ============================================================

FString FNwiroIKDataTools::ReadDataTable(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString TablePath = Cmd->GetStringField(TEXT("table"));
	if (TablePath.IsEmpty()) TablePath = Cmd->GetStringField(TEXT("path"));

	UObject* Asset = UEditorAssetLibrary::LoadAsset(TablePath);
	UDataTable* DT = Cast<UDataTable>(Asset);
	if (!DT)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"DataTable not found: %s\"}"), *TablePath);

	FString JsonOut = DT->GetTableAsJSON();

	return FString::Printf(TEXT("{\"success\":true,\"table\":\"%s\",\"rowStruct\":\"%s\",\"rowCount\":%d,\"rows\":%s}"),
		*DT->GetName(),
		DT->RowStruct ? *DT->RowStruct->GetName() : TEXT("None"),
		DT->GetRowMap().Num(),
		*JsonOut);
}

// ============================================================
// IMPORT DATA TABLE JSON
// ============================================================

FString FNwiroIKDataTools::ImportDataTableJson(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString TablePath = Cmd->GetStringField(TEXT("table"));
	FString JsonData = Cmd->GetStringField(TEXT("json"));

	UObject* Asset = UEditorAssetLibrary::LoadAsset(TablePath);
	UDataTable* DT = Cast<UDataTable>(Asset);
	if (!DT)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"DataTable not found: %s\"}"), *TablePath);

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "ImportDataTableJson", "AI: Import DataTable JSON"), DT);

	TArray<FString> Problems = DT->CreateTableFromJSONString(JsonData);

	DT->MarkPackageDirty();

	if (Problems.Num() > 0)
	{
		return FString::Printf(TEXT("{\"success\":true,\"warnings\":\"%s\",\"rowCount\":%d}"),
			*FString::Join(Problems, TEXT("; ")), DT->GetRowMap().Num());
	}

	return FString::Printf(TEXT("{\"success\":true,\"rowCount\":%d}"), DT->GetRowMap().Num());
}

// ============================================================
// CREATE STRUCT
// ============================================================

FString FNwiroIKDataTools::CreateStruct(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/Data");

	FString FullPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateStruct", "AI: Create Struct"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*Name), RF_Public | RF_Standalone);
	if (!NewStruct)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create struct\"}");
	}
	Tx.AlsoModify(NewStruct);

	// Add fields
	const TArray<TSharedPtr<FJsonValue>>* Fields;
	int32 FieldCount = 0;
	if (Cmd->TryGetArrayField(TEXT("fields"), Fields))
	{
		for (const TSharedPtr<FJsonValue>& FieldVal : *Fields)
		{
			const TSharedPtr<FJsonObject>& FieldObj = FieldVal->AsObject();
			if (!FieldObj.IsValid()) continue;

			FString FieldName = FieldObj->GetStringField(TEXT("name"));
			FString FieldType = FieldObj->GetStringField(TEXT("type"));

			if (FieldName.IsEmpty() || FieldType.IsEmpty()) continue;

			// Map type string to FEdGraphPinType
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; // default

			FString TypeLower = FieldType.ToLower();
			if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
				PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			else if (TypeLower == TEXT("int") || TypeLower == TEXT("integer"))
				PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			else if (TypeLower == TEXT("float"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				PinType.PinSubCategory = TEXT("float");
			}
			else if (TypeLower == TEXT("double") || TypeLower == TEXT("real"))
			{
				// "real" is an alias defaulting to highest precision (double).
				PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				PinType.PinSubCategory = TEXT("double");
			}
			else if (TypeLower == TEXT("string"))
				PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			else if (TypeLower == TEXT("name"))
				PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			else if (TypeLower == TEXT("text"))
				PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			else if (TypeLower == TEXT("vector"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			}
			else if (TypeLower == TEXT("rotator"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			}
			else if (TypeLower == TEXT("transform"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			}
			else if (TypeLower == TEXT("color") || TypeLower == TEXT("linearcolor"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
			}

			FStructureEditorUtils::AddVariable(NewStruct, PinType);
			// Rename the variable
			TArray<FStructVariableDescription>& Vars = FStructureEditorUtils::GetVarDesc(NewStruct);
			if (Vars.Num() > 0)
			{
				FStructureEditorUtils::RenameVariable(NewStruct, Vars.Last().VarGuid, FieldName);
			}

			FieldCount++;
		}
	}

	FAssetRegistryModule::AssetCreated(NewStruct);
	NewStruct->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"fieldCount\":%d}"),
		*Name, *NewStruct->GetPathName(), FieldCount);
}

// ============================================================
// CREATE ENUM
// ============================================================

FString FNwiroIKDataTools::CreateEnum(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(Reader, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Name = Cmd->GetStringField(TEXT("name"));
	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Name.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'name'\"}");
	if (Path.IsEmpty()) Path = TEXT("/Game/Data");

	FString FullPath = Path / Name;

	FNwiroIKTransactionHelper Tx(NSLOCTEXT("Nwiro", "CreateEnum", "AI: Create Enum"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create package\"}");
	}
	Tx.AlsoModify(Package);

	UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, FName(*Name), RF_Public | RF_Standalone));
	if (!NewEnum)
	{
		Tx.Cancel();
		return TEXT("{\"success\":false,\"error\":\"Failed to create enum\"}");
	}
	Tx.AlsoModify(NewEnum);

	// Add values
	const TArray<TSharedPtr<FJsonValue>>* Values;
	int32 ValueCount = 0;
	if (Cmd->TryGetArrayField(TEXT("values"), Values))
	{
		for (const TSharedPtr<FJsonValue>& Val : *Values)
		{
			FString ValueName = Val->AsString();
			if (ValueName.IsEmpty()) continue;

			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);
			// Rename to the desired name
			int64 EnumVal = NewEnum->GetMaxEnumValue() - 2; // -1 for MAX, -1 for 0-indexed
			FText DisplayName = FText::FromString(ValueName);
			FEnumEditorUtils::SetEnumeratorDisplayName(NewEnum, EnumVal, DisplayName);

			ValueCount++;
		}
	}

	FAssetRegistryModule::AssetCreated(NewEnum);
	NewEnum->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"path\":\"%s\",\"valueCount\":%d}"),
		*Name, *NewEnum->GetPathName(), ValueCount);
}

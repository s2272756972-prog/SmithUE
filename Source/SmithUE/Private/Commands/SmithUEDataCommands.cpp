// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEDataCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "EdGraphSchema_K2.h"
#include "Factories/DataTableFactory.h"
#include "IAssetTools.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Misc/PackageName.h"
#include "UObject/StructOnScope.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace SmithUEData
{
    FString NormalizeAssetPath(const FString& Path, const FString& Name)
    {
        FString CleanPath = Path;
        CleanPath.RemoveFromEnd(TEXT("/"));
        if (Name.IsEmpty() || CleanPath.EndsWith(TEXT("/") + Name))
        {
            return CleanPath;
        }
        return CleanPath / Name;
    }

    UScriptStruct* FindRowStruct(const FString& StructPathOrName)
    {
        if (StructPathOrName.IsEmpty())
        {
            return nullptr;
        }

        if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPathOrName))
        {
            return Struct;
        }

        if (UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *StructPathOrName))
        {
            return Struct;
        }

        const FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *StructPathOrName);
        if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *ScriptPath))
        {
            return Struct;
        }

        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        FARFilter Filter;
        Filter.PackagePaths.Add(FName(TEXT("/Game")));
        Filter.bRecursivePaths = true;
        Filter.ClassNames.Add(UUserDefinedStruct::StaticClass()->GetFName());

        TArray<FAssetData> Assets;
        AssetRegistryModule.Get().GetAssets(Filter, Assets);
        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetName.ToString().Equals(StructPathOrName, ESearchCase::IgnoreCase))
            {
                return Cast<UScriptStruct>(Asset.GetAsset());
            }
        }

        return nullptr;
    }

    UDataTable* LoadDataTable(const FString& TablePath, FString& OutError)
    {
        UObject* Asset = UEditorAssetLibrary::LoadAsset(TablePath);
        UDataTable* DataTable = Cast<UDataTable>(Asset);
        if (!DataTable)
        {
            OutError = FString::Printf(TEXT("DataTable not found: %s"), *TablePath);
        }
        return DataTable;
    }

    TSharedPtr<FJsonObject> RowToJsonObject(UDataTable* DataTable, const FName& RowName, const uint8* RowData)
    {
        TSharedPtr<FJsonObject> RowObject = MakeShared<FJsonObject>();
        RowObject->SetStringField(TEXT("Name"), RowName.ToString());

        if (DataTable && DataTable->RowStruct && RowData)
        {
            TSharedRef<FJsonObject> ValueObject = MakeShared<FJsonObject>();
            if (FJsonObjectConverter::UStructToJsonObject(DataTable->RowStruct, RowData, ValueObject, 0, 0))
            {
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : ValueObject->Values)
                {
                    RowObject->SetField(Field.Key, Field.Value);
                }
            }
        }

        return RowObject;
    }

    bool JsonFieldToPinType(const FString& FieldType, FEdGraphPinType& OutPinType)
    {
        const FString TypeLower = FieldType.ToLower();
        OutPinType = FEdGraphPinType();
        OutPinType.ContainerType = EPinContainerType::None;
        OutPinType.bIsReference = false;
        OutPinType.bIsConst = false;
        OutPinType.bIsWeakPointer = false;
        OutPinType.bIsUObjectWrapper = false;

        if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        }
        else if (TypeLower == TEXT("int") || TypeLower == TEXT("int32") || TypeLower == TEXT("integer"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
        }
        else if (TypeLower == TEXT("float") || TypeLower == TEXT("double") || TypeLower == TEXT("real"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
            OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
        }
        else if (TypeLower == TEXT("fstring") || TypeLower == TEXT("string"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
        }
        else if (TypeLower == TEXT("fname") || TypeLower == TEXT("name"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
        }
        else if (TypeLower == TEXT("ftext") || TypeLower == TEXT("text"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
        }
        else if (TypeLower == TEXT("fvector") || TypeLower == TEXT("vector"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
        }
        else if (TypeLower == TEXT("frotator") || TypeLower == TEXT("rotator"))
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
        }
        else
        {
            return false;
        }

        return true;
    }
} // namespace SmithUEData

using namespace SmithUEData;

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEDataCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("data_create_table"),
            TEXT("Data"),
            TEXT("Create a DataTable asset using a row struct"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("DataTable asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (e.g. /Game/Data)"), true),
                FSmithUEToolParam(TEXT("row_struct"), TEXT("string"), TEXT("Row UScriptStruct path or name"), true)
            }),
        &HandleDataCreateTable);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("data_add_row"),
            TEXT("Data"),
            TEXT("Add or replace a row in a DataTable from JSON object data"),
            {
                FSmithUEToolParam(TEXT("table_path"), TEXT("string"), TEXT("DataTable asset path"), true),
                FSmithUEToolParam(TEXT("row_name"), TEXT("string"), TEXT("Row name to add or replace"), true),
                FSmithUEToolParam(TEXT("row_data"), TEXT("object"), TEXT("JSON object containing row field values"), true)
            }),
        &HandleDataAddRow);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("data_read_table"),
            TEXT("Data"),
            TEXT("Read all rows or a single row from a DataTable"),
            {
                FSmithUEToolParam(TEXT("table_path"), TEXT("string"), TEXT("DataTable asset path"), true),
                FSmithUEToolParam(TEXT("row_name"), TEXT("string"), TEXT("Optional row name to read"))
            }),
        &HandleDataReadTable);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("data_import_json"),
            TEXT("Data"),
            TEXT("Import DataTable rows from a JSON string"),
            {
                FSmithUEToolParam(TEXT("table_path"), TEXT("string"), TEXT("DataTable asset path"), true),
                FSmithUEToolParam(TEXT("json_string"), TEXT("string"), TEXT("JSON string accepted by UDataTable::CreateTableFromJSONString"), true)
            }),
        &HandleDataImportJson);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("data_create_struct"),
            TEXT("Data"),
            TEXT("Create a UserDefinedStruct asset with typed fields"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Struct asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (e.g. /Game/Data)"), true),
                FSmithUEToolParam(TEXT("fields"), TEXT("array"), TEXT("Array of field objects with name and type"), true)
            }),
        &HandleDataCreateStruct);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("data_create_enum"),
            TEXT("Data"),
            TEXT("Create a UserDefinedEnum asset with entries"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Enum asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (e.g. /Game/Data)"), true),
                FSmithUEToolParam(TEXT("entries"), TEXT("array"), TEXT("Array of enum entry names"), true)
            }),
        &HandleDataCreateEnum);
}

// ---------------------------------------------------------------------------
// Command: data_create_table
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDataCommands::HandleDataCreateTable(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path"), TEXT("row_struct")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString Name;
    FString Path;
    FString RowStructName;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("row_struct"), RowStructName);

    UScriptStruct* RowStruct = FindRowStruct(RowStructName);
    if (!RowStruct)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Row struct not found: %s"), *RowStructName));
    }

    UDataTableFactory* Factory = NewObject<UDataTableFactory>();
    Factory->Struct = RowStruct;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UDataTable::StaticClass(), Factory);
    UDataTable* DataTable = Cast<UDataTable>(NewAsset);
    if (!DataTable)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create DataTable asset"));
    }

    UEditorAssetLibrary::SaveLoadedAsset(DataTable);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), DataTable->GetName());
    Data->SetStringField(TEXT("path"), DataTable->GetPathName());
    Data->SetStringField(TEXT("row_struct"), RowStruct->GetPathName());
    UE_LOG(LogSmithUE, Log, TEXT("data_create_table: created %s"), *DataTable->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: data_add_row
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDataCommands::HandleDataAddRow(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("table_path"), TEXT("row_name"), TEXT("row_data")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString TablePath;
    FString RowNameString;
    Params->TryGetStringField(TEXT("table_path"), TablePath);
    Params->TryGetStringField(TEXT("row_name"), RowNameString);

    const TSharedPtr<FJsonObject>* RowDataObjectPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("row_data"), RowDataObjectPtr) || !RowDataObjectPtr || !RowDataObjectPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("row_data must be a JSON object"));
    }

    UDataTable* DataTable = LoadDataTable(TablePath, Error);
    if (!DataTable)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    if (!DataTable->RowStruct)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("DataTable has no row struct"));
    }

    // Build a JSON string containing just this single row, then import it
    const FName RowName(*RowNameString);
    DataTable->RemoveRow(RowName);

    // Serialize row_data back to JSON string for import
    TSharedRef<FJsonObject> RowJsonObj = MakeShared<FJsonObject>();
    RowJsonObj->SetStringField(TEXT("Name"), RowNameString);
    // Copy all fields from the incoming row_data
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*RowDataObjectPtr)->Values)
    {
        RowJsonObj->SetField(Field.Key, Field.Value);
    }
    
    TArray<TSharedPtr<FJsonValue>> RowArray;
    RowArray.Add(MakeShared<FJsonValueObject>(RowJsonObj));
    
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(RowArray, Writer);
    Writer->Close();
    
    TArray<FString> ImportProblems = DataTable->CreateTableFromJSONString(JsonString);
    
    if (ImportProblems.Num() > 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Row import had issues: %s"), *FString::Join(ImportProblems, TEXT("; "))));
    }
    
    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(DataTable);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("table_path"), DataTable->GetPathName());
    Data->SetStringField(TEXT("row_name"), RowNameString);
    Data->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());
    UE_LOG(LogSmithUE, Log, TEXT("data_add_row: added row %s to %s"), *RowNameString, *DataTable->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: data_read_table
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDataCommands::HandleDataReadTable(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("table_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString TablePath;
    FString RowNameString;
    Params->TryGetStringField(TEXT("table_path"), TablePath);
    Params->TryGetStringField(TEXT("row_name"), RowNameString);

    UDataTable* DataTable = LoadDataTable(TablePath, Error);
    if (!DataTable)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("table_path"), DataTable->GetPathName());
    Data->SetStringField(TEXT("row_struct"), DataTable->RowStruct ? DataTable->RowStruct->GetPathName() : TEXT("None"));
    Data->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());

    if (!RowNameString.IsEmpty())
    {
        const FName RowName(*RowNameString);
        uint8* const* FoundRow = DataTable->GetRowMap().Find(RowName);
        if (!FoundRow)
        {
            return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Row not found: %s"), *RowNameString));
        }
        Data->SetObjectField(TEXT("row"), RowToJsonObject(DataTable, RowName, *FoundRow));
    }
    else
    {
        TArray<TSharedPtr<FJsonValue>> Rows;
        Rows.Reserve(DataTable->GetRowMap().Num());
        for (const TPair<FName, uint8*>& RowPair : DataTable->GetRowMap())
        {
            Rows.Add(MakeShared<FJsonValueObject>(RowToJsonObject(DataTable, RowPair.Key, RowPair.Value)));
        }
        Data->SetArrayField(TEXT("rows"), Rows);
    }

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: data_import_json
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDataCommands::HandleDataImportJson(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("table_path"), TEXT("json_string")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString TablePath;
    FString JsonString;
    Params->TryGetStringField(TEXT("table_path"), TablePath);
    Params->TryGetStringField(TEXT("json_string"), JsonString);

    UDataTable* DataTable = LoadDataTable(TablePath, Error);
    if (!DataTable)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    TArray<FString> Problems = DataTable->CreateTableFromJSONString(JsonString);
    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(DataTable);

    TArray<TSharedPtr<FJsonValue>> Warnings;
    for (const FString& Problem : Problems)
    {
        Warnings.Add(MakeShared<FJsonValueString>(Problem));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("table_path"), DataTable->GetPathName());
    Data->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());
    Data->SetArrayField(TEXT("warnings"), Warnings);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: data_create_struct
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDataCommands::HandleDataCreateStruct(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path"), TEXT("fields")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString Name;
    FString Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
    if (!Params->TryGetArrayField(TEXT("fields"), Fields) || !Fields)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("fields must be an array"));
    }

    const FString PackagePath = NormalizeAssetPath(Path, Name);
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));
    }

    UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*Name), RF_Public | RF_Standalone);
    if (!NewStruct)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UserDefinedStruct"));
    }

    TArray<TSharedPtr<FJsonValue>> CreatedFields;
    for (const TSharedPtr<FJsonValue>& FieldValue : *Fields)
    {
        const TSharedPtr<FJsonObject> FieldObject = FieldValue.IsValid() ? FieldValue->AsObject() : nullptr;
        if (!FieldObject.IsValid())
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("Each fields entry must be an object"));
        }

        FString FieldName;
        FString FieldType;
        FieldObject->TryGetStringField(TEXT("name"), FieldName);
        FieldObject->TryGetStringField(TEXT("type"), FieldType);
        if (FieldName.IsEmpty() || FieldType.IsEmpty())
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("Each field requires non-empty name and type"));
        }

        FEdGraphPinType PinType;
        if (!JsonFieldToPinType(FieldType, PinType))
        {
            return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported field type '%s' for field '%s'"), *FieldType, *FieldName));
        }

        FStructureEditorUtils::AddVariable(NewStruct, PinType);
        TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(NewStruct);
        if (Variables.Num() == 0)
        {
            return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add field: %s"), *FieldName));
        }

        FStructureEditorUtils::RenameVariable(NewStruct, Variables.Last().VarGuid, FieldName);

        TSharedPtr<FJsonObject> CreatedField = MakeShared<FJsonObject>();
        CreatedField->SetStringField(TEXT("name"), FieldName);
        CreatedField->SetStringField(TEXT("type"), FieldType);
        CreatedFields.Add(MakeShared<FJsonValueObject>(CreatedField));
    }

    FStructureEditorUtils::OnStructureChanged(NewStruct);
    FAssetRegistryModule::AssetCreated(NewStruct);
    NewStruct->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(NewStruct);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), NewStruct->GetName());
    Data->SetStringField(TEXT("path"), NewStruct->GetPathName());
    Data->SetNumberField(TEXT("field_count"), CreatedFields.Num());
    Data->SetArrayField(TEXT("fields"), CreatedFields);
    UE_LOG(LogSmithUE, Log, TEXT("data_create_struct: created %s"), *NewStruct->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: data_create_enum
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDataCommands::HandleDataCreateEnum(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name"), TEXT("path"), TEXT("entries")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString Name;
    FString Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!Params->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("entries must be an array"));
    }

    const FString PackagePath = NormalizeAssetPath(Path, Name);
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));
    }

    UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, FName(*Name), RF_Public | RF_Standalone));
    if (!NewEnum)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UserDefinedEnum"));
    }

    TArray<TSharedPtr<FJsonValue>> CreatedEntries;
    for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
    {
        const FString EntryName = EntryValue.IsValid() ? EntryValue->AsString() : FString();
        if (EntryName.IsEmpty())
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("Enum entries must be non-empty strings"));
        }

        FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);
        const int64 EnumValue = NewEnum->GetMaxEnumValue() - 2;
        FEnumEditorUtils::SetEnumeratorDisplayName(NewEnum, EnumValue, FText::FromString(EntryName));
        CreatedEntries.Add(MakeShared<FJsonValueString>(EntryName));
    }

    FAssetRegistryModule::AssetCreated(NewEnum);
    NewEnum->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(NewEnum);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), NewEnum->GetName());
    Data->SetStringField(TEXT("path"), NewEnum->GetPathName());
    Data->SetNumberField(TEXT("entry_count"), CreatedEntries.Num());
    Data->SetArrayField(TEXT("entries"), CreatedEntries);
    UE_LOG(LogSmithUE, Log, TEXT("data_create_enum: created %s"), *NewEnum->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

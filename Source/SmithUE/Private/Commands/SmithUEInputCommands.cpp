// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEInputCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "EnhancedActionKeyMapping.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace SmithUEInput
{
    UInputAction* FindInputActionByName(const FString& NameOrPath)
    {
        // Try direct load first
        UInputAction* Action = LoadObject<UInputAction>(nullptr, *NameOrPath);
        if (Action) return Action;

        // Search by name in AssetRegistry
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        IAssetRegistry& AR = ARM.Get();

        FARFilter Filter;
        Filter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
        Filter.PackagePaths.Add(TEXT("/Game"));
        Filter.bRecursivePaths = true;

        TArray<FAssetData> Assets;
        AR.GetAssets(Filter, Assets);

        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase) ||
                Asset.AssetName.ToString().Contains(NameOrPath, ESearchCase::IgnoreCase))
            {
                return Cast<UInputAction>(Asset.GetAsset());
            }
        }
        return nullptr;
    }

    UInputMappingContext* FindMappingContextByNameOrPath(const FString& NameOrPath)
    {
        UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *NameOrPath);
        if (IMC) return IMC;

        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        IAssetRegistry& AR = ARM.Get();

        FARFilter Filter;
        Filter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
        Filter.PackagePaths.Add(TEXT("/Game"));
        Filter.bRecursivePaths = true;

        TArray<FAssetData> Assets;
        AR.GetAssets(Filter, Assets);

        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase) ||
                Asset.AssetName.ToString().Contains(NameOrPath, ESearchCase::IgnoreCase))
            {
                return Cast<UInputMappingContext>(Asset.GetAsset());
            }
        }
        return nullptr;
    }

    FString ValueTypeToString(EInputActionValueType Type)
    {
        switch (Type)
        {
        case EInputActionValueType::Boolean: return TEXT("Boolean");
        case EInputActionValueType::Axis1D: return TEXT("Axis1D");
        case EInputActionValueType::Axis2D: return TEXT("Axis2D");
        case EInputActionValueType::Axis3D: return TEXT("Axis3D");
        default: return TEXT("Unknown");
        }
    }

    void AddModifiersToMapping(FEnhancedActionKeyMapping& Mapping, const TArray<TSharedPtr<FJsonValue>>& ModArray, UObject* Outer)
    {
        for (const TSharedPtr<FJsonValue>& ModVal : ModArray)
        {
            FString ModName = ModVal->AsString();
            if (ModName.Equals(TEXT("Negate"), ESearchCase::IgnoreCase))
            {
                UInputModifierNegate* Mod = NewObject<UInputModifierNegate>(Outer);
                Mapping.Modifiers.Add(Mod);
            }
            else if (ModName.Equals(TEXT("SwizzleYXZ"), ESearchCase::IgnoreCase))
            {
                UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(Outer);
                Mod->Order = EInputAxisSwizzle::YXZ;
                Mapping.Modifiers.Add(Mod);
            }
            else if (ModName.Equals(TEXT("SwizzleZYX"), ESearchCase::IgnoreCase))
            {
                UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(Outer);
                Mod->Order = EInputAxisSwizzle::ZYX;
                Mapping.Modifiers.Add(Mod);
            }
            else if (ModName.Equals(TEXT("SwizzleXZY"), ESearchCase::IgnoreCase))
            {
                UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(Outer);
                Mod->Order = EInputAxisSwizzle::XZY;
                Mapping.Modifiers.Add(Mod);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEInputCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("input_create_action"),
            TEXT("Input"),
            TEXT("Create an Enhanced Input Action asset (IA_*)"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Action name (IA_ prefix added if missing)"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (default: /Game/Input)"), false),
                FSmithUEToolParam(TEXT("value_type"), TEXT("string"), TEXT("Value type: Boolean, Axis1D, Axis2D, Axis3D (default: Boolean)"), false)
            }),
        &HandleInputCreateAction);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("input_create_mapping_context"),
            TEXT("Input"),
            TEXT("Create an Input Mapping Context asset (IMC_*) with optional key mappings"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Context name (IMC_ prefix added if missing)"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (default: /Game/Input)"), false),
                FSmithUEToolParam(TEXT("mappings"), TEXT("array"), TEXT("Array of {action, key, modifiers[]} objects"), false)
            }),
        &HandleInputCreateMappingContext);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("input_find_actions"),
            TEXT("Input"),
            TEXT("Find InputAction and InputMappingContext assets in the project"),
            {
                FSmithUEToolParam(TEXT("search_term"), TEXT("string"), TEXT("Optional filter by name substring"), false)
            }),
        &HandleInputFindActions);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("input_read_mapping_context"),
            TEXT("Input"),
            TEXT("Read an InputMappingContext to see its action-key mappings and modifiers"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("IMC asset name or path"), true)
            }),
        &HandleInputReadMappingContext);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("input_edit_mapping_context"),
            TEXT("Input"),
            TEXT("Edit an InputMappingContext: add or remove key mappings"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("IMC asset name or full path"), true),
                FSmithUEToolParam(TEXT("add_mappings"), TEXT("array"), TEXT("Array of {action, key, modifiers[]} to add"), false),
                FSmithUEToolParam(TEXT("remove_actions"), TEXT("array"), TEXT("Array of action names to unmap entirely"), false)
            }),
        &HandleInputEditMappingContext);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("input_delete_asset"),
            TEXT("Input"),
            TEXT("Delete an InputAction or InputMappingContext asset by name or path"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name to search and delete"), false),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Full asset path to delete directly"), false)
            }),
        &HandleInputDeleteAsset);
}

// ---------------------------------------------------------------------------
// input_create_action
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInputCommands::HandleInputCreateAction(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

    FString Name = Params->GetStringField(TEXT("name"));
    FString Path = Params->GetStringField(TEXT("path"));
    FString ValueTypeStr = Params->GetStringField(TEXT("value_type"));

    if (Name.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
        return Result;
    }
    if (Path.IsEmpty()) Path = TEXT("/Game/Input");

    // Add IA_ prefix if missing
    if (!Name.StartsWith(TEXT("IA_"))) Name = TEXT("IA_") + Name;

    // Determine value type
    EInputActionValueType ValueType = EInputActionValueType::Boolean;
    if (ValueTypeStr.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
    {
        ValueType = EInputActionValueType::Axis1D;
    }
    else if (ValueTypeStr.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
    {
        ValueType = EInputActionValueType::Axis2D;
    }
    else if (ValueTypeStr.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
    {
        ValueType = EInputActionValueType::Axis3D;
    }

    // Create the asset
    FString FullPath = Path / Name;
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        Result->SetStringField(TEXT("error"), TEXT("Failed to create package"));
        return Result;
    }

    UInputAction* IA = NewObject<UInputAction>(Package, UInputAction::StaticClass(), FName(*Name), RF_Public | RF_Standalone);
    if (!IA)
    {
        Result->SetStringField(TEXT("error"), TEXT("Failed to create InputAction object"));
        return Result;
    }

    IA->ValueType = ValueType;

    FAssetRegistryModule::AssetCreated(IA);
    IA->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(IA);

    Result->SetStringField(TEXT("name"), Name);
    Result->SetStringField(TEXT("path"), IA->GetPathName());
    Result->SetStringField(TEXT("value_type"), SmithUEInput::ValueTypeToString(ValueType));
    return Result;
}

// ---------------------------------------------------------------------------
// input_create_mapping_context
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInputCommands::HandleInputCreateMappingContext(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

    FString Name = Params->GetStringField(TEXT("name"));
    FString Path = Params->GetStringField(TEXT("path"));

    if (Name.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
        return Result;
    }
    if (Path.IsEmpty()) Path = TEXT("/Game/Input");

    // Add IMC_ prefix if missing
    if (!Name.StartsWith(TEXT("IMC_"))) Name = TEXT("IMC_") + Name;

    FString FullPath = Path / Name;
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        Result->SetStringField(TEXT("error"), TEXT("Failed to create package"));
        return Result;
    }

    UInputMappingContext* IMC = NewObject<UInputMappingContext>(Package, UInputMappingContext::StaticClass(), FName(*Name), RF_Public | RF_Standalone);
    if (!IMC)
    {
        Result->SetStringField(TEXT("error"), TEXT("Failed to create InputMappingContext object"));
        return Result;
    }

    // Add mappings if provided
    int32 MappedCount = 0;
    TArray<FString> Errors;
    const TArray<TSharedPtr<FJsonValue>>* Mappings;

    if (Params->TryGetArrayField(TEXT("mappings"), Mappings))
    {
        for (const TSharedPtr<FJsonValue>& MapVal : *Mappings)
        {
            const TSharedPtr<FJsonObject>& MapObj = MapVal->AsObject();
            if (!MapObj.IsValid()) continue;

            FString ActionRef = MapObj->GetStringField(TEXT("action"));
            FString KeyName = MapObj->GetStringField(TEXT("key"));
            if (ActionRef.IsEmpty() || KeyName.IsEmpty()) continue;

            UInputAction* Action = SmithUEInput::FindInputActionByName(ActionRef);
            if (!Action)
            {
                Errors.Add(FString::Printf(TEXT("InputAction not found: %s"), *ActionRef));
                continue;
            }

            FKey Key(*KeyName);
            FEnhancedActionKeyMapping& Mapping = IMC->MapKey(Action, Key);

            // Add modifiers
            const TArray<TSharedPtr<FJsonValue>>* ModArray;
            if (MapObj->TryGetArrayField(TEXT("modifiers"), ModArray))
            {
                SmithUEInput::AddModifiersToMapping(Mapping, *ModArray, IMC);
            }

            MappedCount++;
        }
    }

    FAssetRegistryModule::AssetCreated(IMC);
    IMC->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(IMC);

    Result->SetStringField(TEXT("name"), Name);
    Result->SetStringField(TEXT("path"), IMC->GetPathName());
    Result->SetNumberField(TEXT("mapping_count"), MappedCount);
    if (Errors.Num() > 0)
    {
        Result->SetStringField(TEXT("warnings"), FString::Join(Errors, TEXT("; ")));
    }
    return Result;
}

// ---------------------------------------------------------------------------
// input_find_actions
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInputCommands::HandleInputFindActions(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
    FString SearchTerm = Params->GetStringField(TEXT("search_term"));

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AR = ARM.Get();

    // Find InputActions
    FARFilter IAFilter;
    IAFilter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
    IAFilter.PackagePaths.Add(TEXT("/Game"));
    IAFilter.bRecursivePaths = true;

    TArray<FAssetData> IAAssets;
    AR.GetAssets(IAFilter, IAAssets);

    TArray<TSharedPtr<FJsonValue>> IAResults;
    for (const FAssetData& Asset : IAAssets)
    {
        FString Name = Asset.AssetName.ToString();
        if (!SearchTerm.IsEmpty() && !Name.Contains(SearchTerm, ESearchCase::IgnoreCase)) continue;

        TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
        Obj->SetStringField(TEXT("name"), Name);
        Obj->SetStringField(TEXT("path"), FString::Printf(TEXT("%s.%s"), *Asset.PackageName.ToString(), *Name));
        IAResults.Add(MakeShareable(new FJsonValueObject(Obj)));
    }

    // Find InputMappingContexts
    FARFilter IMCFilter;
    IMCFilter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
    IMCFilter.PackagePaths.Add(TEXT("/Game"));
    IMCFilter.bRecursivePaths = true;

    TArray<FAssetData> IMCAssets;
    AR.GetAssets(IMCFilter, IMCAssets);

    TArray<TSharedPtr<FJsonValue>> IMCResults;
    for (const FAssetData& Asset : IMCAssets)
    {
        FString Name = Asset.AssetName.ToString();
        if (!SearchTerm.IsEmpty() && !Name.Contains(SearchTerm, ESearchCase::IgnoreCase)) continue;

        TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
        Obj->SetStringField(TEXT("name"), Name);
        Obj->SetStringField(TEXT("path"), FString::Printf(TEXT("%s.%s"), *Asset.PackageName.ToString(), *Name));
        IMCResults.Add(MakeShareable(new FJsonValueObject(Obj)));
    }

    Result->SetArrayField(TEXT("input_actions"), IAResults);
    Result->SetArrayField(TEXT("mapping_contexts"), IMCResults);
    Result->SetNumberField(TEXT("action_count"), IAResults.Num());
    Result->SetNumberField(TEXT("context_count"), IMCResults.Num());
    return Result;
}

// ---------------------------------------------------------------------------
// input_read_mapping_context
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInputCommands::HandleInputReadMappingContext(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

    FString Name = Params->GetStringField(TEXT("name"));
    if (Name.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
        return Result;
    }

    UInputMappingContext* IMC = SmithUEInput::FindMappingContextByNameOrPath(Name);
    if (!IMC)
    {
        Result->SetStringField(TEXT("error"), FString::Printf(TEXT("InputMappingContext not found: %s"), *Name));
        return Result;
    }

    Result->SetStringField(TEXT("name"), IMC->GetName());
    Result->SetStringField(TEXT("path"), IMC->GetPathName());

    const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();
    TArray<TSharedPtr<FJsonValue>> MappingArr;

    for (const FEnhancedActionKeyMapping& Mapping : Mappings)
    {
        TSharedPtr<FJsonObject> MapObj = MakeShareable(new FJsonObject());
        MapObj->SetStringField(TEXT("action"), Mapping.Action ? Mapping.Action->GetName() : TEXT("None"));
        MapObj->SetStringField(TEXT("action_path"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT(""));
        MapObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

        // Value type of the action
        if (Mapping.Action)
        {
            MapObj->SetStringField(TEXT("value_type"), SmithUEInput::ValueTypeToString(Mapping.Action->ValueType));
        }

        // Modifiers
        TArray<TSharedPtr<FJsonValue>> ModArr;
        for (const TObjectPtr<UInputModifier>& Mod : Mapping.Modifiers)
        {
            if (Mod)
            {
                ModArr.Add(MakeShareable(new FJsonValueString(Mod->GetClass()->GetName())));
            }
        }
        if (ModArr.Num() > 0)
        {
            MapObj->SetArrayField(TEXT("modifiers"), ModArr);
        }

        // Triggers
        TArray<TSharedPtr<FJsonValue>> TrigArr;
        for (const TObjectPtr<UInputTrigger>& Trig : Mapping.Triggers)
        {
            if (Trig)
            {
                TrigArr.Add(MakeShareable(new FJsonValueString(Trig->GetClass()->GetName())));
            }
        }
        if (TrigArr.Num() > 0)
        {
            MapObj->SetArrayField(TEXT("triggers"), TrigArr);
        }

        MappingArr.Add(MakeShareable(new FJsonValueObject(MapObj)));
    }

    Result->SetArrayField(TEXT("mappings"), MappingArr);
    Result->SetNumberField(TEXT("mapping_count"), Mappings.Num());
    return Result;
}

// ---------------------------------------------------------------------------
// input_edit_mapping_context
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInputCommands::HandleInputEditMappingContext(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

    FString Name = Params->GetStringField(TEXT("name"));
    if (Name.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
        return Result;
    }

    UInputMappingContext* IMC = SmithUEInput::FindMappingContextByNameOrPath(Name);
    if (!IMC)
    {
        Result->SetStringField(TEXT("error"), FString::Printf(TEXT("InputMappingContext not found: %s"), *Name));
        return Result;
    }

    int32 Added = 0;
    int32 Removed = 0;
    TArray<FString> Errors;

    // Remove mappings by action name
    const TArray<TSharedPtr<FJsonValue>>* RemoveArr;
    if (Params->TryGetArrayField(TEXT("remove_actions"), RemoveArr))
    {
        for (const TSharedPtr<FJsonValue>& Val : *RemoveArr)
        {
            FString ActionName = Val->AsString();
            UInputAction* Action = SmithUEInput::FindInputActionByName(ActionName);
            if (Action)
            {
                // Remove all mappings for this action
                TArray<FEnhancedActionKeyMapping> CurrentMappings = IMC->GetMappings();
                for (const FEnhancedActionKeyMapping& Mapping : CurrentMappings)
                {
                    if (Mapping.Action == Action)
                    {
                        IMC->UnmapKey(Action, Mapping.Key);
                    }
                }
                Removed++;
            }
            else
            {
                Errors.Add(FString::Printf(TEXT("Action not found for removal: %s"), *ActionName));
            }
        }
    }

    // Add mappings
    const TArray<TSharedPtr<FJsonValue>>* AddArr;
    if (Params->TryGetArrayField(TEXT("add_mappings"), AddArr))
    {
        for (const TSharedPtr<FJsonValue>& MapVal : *AddArr)
        {
            const TSharedPtr<FJsonObject>& MapObj = MapVal->AsObject();
            if (!MapObj.IsValid()) continue;

            FString ActionRef = MapObj->GetStringField(TEXT("action"));
            FString KeyName = MapObj->GetStringField(TEXT("key"));
            if (ActionRef.IsEmpty() || KeyName.IsEmpty()) continue;

            UInputAction* Action = SmithUEInput::FindInputActionByName(ActionRef);
            if (!Action)
            {
                Errors.Add(FString::Printf(TEXT("Action not found: %s"), *ActionRef));
                continue;
            }

            FKey Key(*KeyName);
            FEnhancedActionKeyMapping& Mapping = IMC->MapKey(Action, Key);

            const TArray<TSharedPtr<FJsonValue>>* ModArray;
            if (MapObj->TryGetArrayField(TEXT("modifiers"), ModArray))
            {
                SmithUEInput::AddModifiersToMapping(Mapping, *ModArray, IMC);
            }

            Added++;
        }
    }

    IMC->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(IMC);

    Result->SetStringField(TEXT("name"), IMC->GetName());
    Result->SetNumberField(TEXT("added"), Added);
    Result->SetNumberField(TEXT("removed"), Removed);
    if (Errors.Num() > 0)
    {
        Result->SetStringField(TEXT("warnings"), FString::Join(Errors, TEXT("; ")));
    }
    return Result;
}

// ---------------------------------------------------------------------------
// input_delete_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInputCommands::HandleInputDeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

    FString Name = Params->GetStringField(TEXT("name"));
    FString Path = Params->GetStringField(TEXT("path"));

    if (Name.IsEmpty() && Path.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), TEXT("Provide either 'name' or 'path'"));
        return Result;
    }

    // Direct path delete
    if (!Path.IsEmpty())
    {
        if (UEditorAssetLibrary::DeleteAsset(Path))
        {
            Result->SetStringField(TEXT("deleted"), Path);
            return Result;
        }
        Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to delete: %s"), *Path));
        return Result;
    }

    // Search by name across both asset types
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AR = ARM.Get();

    for (FTopLevelAssetPath ClassPath : {UInputAction::StaticClass()->GetClassPathName(), UInputMappingContext::StaticClass()->GetClassPathName()})
    {
        FARFilter Filter;
        Filter.ClassPaths.Add(ClassPath);
        Filter.PackagePaths.Add(TEXT("/Game"));
        Filter.bRecursivePaths = true;

        TArray<FAssetData> Assets;
        AR.GetAssets(Filter, Assets);

        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase) ||
                Asset.AssetName.ToString().Contains(Name, ESearchCase::IgnoreCase))
            {
                FString PkgPath = Asset.PackageName.ToString();
                if (UEditorAssetLibrary::DeleteAsset(PkgPath))
                {
                    Result->SetStringField(TEXT("deleted"), PkgPath);
                    Result->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
                    return Result;
                }
                Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to delete: %s"), *PkgPath));
                return Result;
            }
        }
    }

    Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *Name));
    return Result;
}

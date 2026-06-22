// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEAssetAuditCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Utils/SmithUEAssetPropertyUtils.h"
#include "SmithUEModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEAssetAuditCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // get_asset_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_asset_property"),
            TEXT("Asset"),
            TEXT("Read a property value from a loaded asset by dotted path (e.g. LightmapCoordinateIndex, StaticMaterials[0]). Asset must be loaded (is_loaded=true)."),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Asset content path, e.g. /Game/Meshes/SM_Crate"), true),
                FSmithUEToolParam(TEXT("property"),   TEXT("string"), TEXT("Dotted property path, e.g. LightmapCoordinateIndex"), true)
            }),
        &FSmithUEAssetAuditCommands::HandleGetAssetProperty);

    // scan_assets
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("scan_assets"),
            TEXT("Asset"),
            TEXT("Folder-scoped asset scan. Returns v1 linter metadata: naming, path, parent class, material slots, LOD count, collision presence."),
            {
                FSmithUEToolParam(TEXT("folder_path"),   TEXT("string"),  TEXT("UE content path, e.g. /Game/MyProject"), true),
                FSmithUEToolParam(TEXT("recursive"),     TEXT("boolean"), TEXT("Recurse into sub-folders. Default: false")),
                FSmithUEToolParam(TEXT("class_filter"),  TEXT("array"),   TEXT("Class name filter array, e.g. [\"Blueprint\"]. Empty = all assets."))
            }),
        &FSmithUEAssetAuditCommands::HandleScanAssets);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    // Serialize a single FProperty value at LeafValuePtr to a JSON value.
    TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Prop, void* ValuePtr)
    {
        if (!Prop || !ValuePtr) return MakeShared<FJsonValueNull>();

        if (FBoolProperty* P = CastField<FBoolProperty>(Prop))
            return MakeShared<FJsonValueBoolean>(P->GetPropertyValue(ValuePtr));

        if (FNumericProperty* P = CastField<FNumericProperty>(Prop))
        {
            if (P->IsFloatingPoint())
                return MakeShared<FJsonValueNumber>(P->GetFloatingPointPropertyValue(ValuePtr));
            if (FByteProperty* BP = CastField<FByteProperty>(Prop))
            {
                if (BP->Enum)
                {
                    int64 EnumVal = BP->GetSignedIntPropertyValue(ValuePtr);
                    FString EnumStr = BP->Enum->GetNameStringByValue(EnumVal);
                    return MakeShared<FJsonValueString>(EnumStr);
                }
            }
            return MakeShared<FJsonValueNumber>((double)P->GetSignedIntPropertyValue(ValuePtr));
        }

        if (FEnumProperty* P = CastField<FEnumProperty>(Prop))
        {
            int64 Val = P->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
            return MakeShared<FJsonValueString>(P->GetEnum()->GetNameStringByValue(Val));
        }

        if (FNameProperty* P = CastField<FNameProperty>(Prop))
            return MakeShared<FJsonValueString>(P->GetPropertyValue(ValuePtr).ToString());

        if (FStrProperty* P = CastField<FStrProperty>(Prop))
            return MakeShared<FJsonValueString>(P->GetPropertyValue(ValuePtr));

        if (FTextProperty* P = CastField<FTextProperty>(Prop))
            return MakeShared<FJsonValueString>(P->GetPropertyValue(ValuePtr).ToString());

        if (FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Prop))
        {
            UObject* Obj = P->GetObjectPropertyValue(ValuePtr);
            return MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : TEXT("None"));
        }

        // Fallback: ExportText
        FString TextVal;
        Prop->ExportTextItem_Direct(TextVal, ValuePtr, nullptr, nullptr, PPF_None);
        return MakeShared<FJsonValueString>(TextVal);
    }
} // namespace

// ---------------------------------------------------------------------------
// HandleGetAssetProperty
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetAuditCommands::HandleGetAssetProperty(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path"), TEXT("property")}, Error))
        return FSmithUECommonUtils::CreateErrorResponse(Error);

    FString AssetPath, PropertyPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);
    Params->TryGetStringField(TEXT("property"), PropertyPath);

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found or not loaded: %s"), *AssetPath));

    FAssetResolvedPropertyPath Resolved;
    if (!ResolveAssetObjectPropertyPath(Asset, PropertyPath, Resolved, Error))
        return FSmithUECommonUtils::CreateErrorResponse(Error);

    TSharedPtr<FJsonValue> JsonVal = PropertyToJsonValue(Resolved.LeafProperty, Resolved.LeafValuePtr);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("property"), PropertyPath);
    Data->SetField(TEXT("value"), JsonVal);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleScanAssets
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetAuditCommands::HandleScanAssets(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("folder_path")}, Error))
        return FSmithUECommonUtils::CreateErrorResponse(Error);

    FString FolderPath;
    bool bRecursive = false;
    Params->TryGetStringField(TEXT("folder_path"), FolderPath);
    Params->TryGetBoolField(TEXT("recursive"), bRecursive);

    // Strip /All prefix if present (content browser virtual path)
    if (FolderPath.StartsWith(TEXT("/All/")))
        FolderPath = FolderPath.Mid(4);

    IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = bRecursive;

    // Optional class filter
    const TArray<TSharedPtr<FJsonValue>>* ClassFilterArr = nullptr;
    if (Params->TryGetArrayField(TEXT("class_filter"), ClassFilterArr) && ClassFilterArr)
    {
        for (const TSharedPtr<FJsonValue>& V : *ClassFilterArr)
        {
            FString ClassName = V->AsString();
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->GetName() == ClassName)
                {
                    Filter.ClassPaths.Add(It->GetClassPathName());
                    break;
                }
            }
        }
    }

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> AssetArray;
    for (const FAssetData& AD : Assets)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"),         AD.AssetName.ToString());
        Obj->SetStringField(TEXT("path"),         AD.GetObjectPathString());
        Obj->SetStringField(TEXT("package_name"), AD.PackageName.ToString());
        Obj->SetStringField(TEXT("package_path"), AD.PackagePath.ToString());
        Obj->SetStringField(TEXT("class"),        AD.AssetClassPath.GetAssetName().ToString());

        // parent_class (Blueprint only)
        FString ParentClass;
        AD.GetTagValue(TEXT("ParentClass"), ParentClass);
        if (ParentClass.IsEmpty())
            Obj->SetField(TEXT("parent_class"), MakeShared<FJsonValueNull>());
        else
            Obj->SetStringField(TEXT("parent_class"), ParentClass);

        // material_slots (StaticMesh/SkeletalMesh tag: Materials = slot count)
        FString MaterialsTag;
        int32 MaterialSlots = 0;
        AD.GetTagValue(TEXT("Materials"), MaterialsTag);
        if (!MaterialsTag.IsEmpty()) LexTryParseString(MaterialSlots, *MaterialsTag);
        Obj->SetNumberField(TEXT("material_slots"), MaterialSlots);
        // material_slot_paths: requires LoadObject, return empty array for now (fast path)
        Obj->SetArrayField(TEXT("material_slot_paths"), TArray<TSharedPtr<FJsonValue>>());

        // lod_count / min_lod
        FString LodsTag, MinLodTag;
        int32 LodCount = 0, MinLod = 0;
        AD.GetTagValue(TEXT("LODs"), LodsTag);
        AD.GetTagValue(TEXT("MinLOD"), MinLodTag);
        if (!LodsTag.IsEmpty())   LexTryParseString(LodCount, *LodsTag);
        if (!MinLodTag.IsEmpty()) LexTryParseString(MinLod, *MinLodTag);
        Obj->SetNumberField(TEXT("lod_count"), LodCount);
        Obj->SetNumberField(TEXT("min_lod"),   MinLod);

        // has_collision (tag: CollisionPrims > 0)
        FString CollisionTag;
        int32 CollisionPrims = 0;
        AD.GetTagValue(TEXT("CollisionPrims"), CollisionTag);
        if (!CollisionTag.IsEmpty()) LexTryParseString(CollisionPrims, *CollisionTag);
        Obj->SetBoolField(TEXT("has_collision"), CollisionPrims > 0);

        AssetArray.Add(MakeShared<FJsonValueObject>(Obj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("assets"), AssetArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

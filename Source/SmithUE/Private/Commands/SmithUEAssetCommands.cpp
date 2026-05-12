// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEAssetCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    TSharedPtr<FJsonObject> AssetDataToJson(const FAssetData& AssetData, bool bDetailed)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
        Obj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        Obj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
        Obj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());

        if (bDetailed)
        {
            Obj->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());

            // Disk size from tag
            int64 DiskSize = -1;
            AssetData.GetTagValue(FName("Size"), DiskSize);
            if (DiskSize >= 0)
            {
                Obj->SetNumberField(TEXT("disk_size"), static_cast<double>(DiskSize));
            }

            // Is loaded / dirty
            UPackage* Package = FindPackage(nullptr, *AssetData.PackageName.ToString());
            Obj->SetBoolField(TEXT("is_loaded"), Package != nullptr);
            if (Package)
            {
                Obj->SetBoolField(TEXT("is_dirty"), Package->IsDirty());
            }

            // Tags
            TSharedPtr<FJsonObject> TagsObj = MakeShared<FJsonObject>();
            FAssetDataTagMap TagsAndValues = AssetData.TagsAndValues.CopyMap();
            for (const TPair<FName, FString>& Tag : TagsAndValues)
            {
                TagsObj->SetStringField(Tag.Key.ToString(), Tag.Value);
            }
            Obj->SetObjectField(TEXT("tags"), TagsObj);
        }

        return Obj;
    }

    IAssetRegistry& GetAssetRegistry()
    {
        return FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEAssetCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("list_assets"),
            TEXT("Asset"),
            TEXT("List assets in a content folder"),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder path (e.g. /Game)"), true),
                FSmithUEToolParam(TEXT("type_filter"), TEXT("string"), TEXT("Optional: filter by asset type name (e.g. StaticMesh)"))
            }),
        &HandleListAssets);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("find_asset"),
            TEXT("Asset"),
            TEXT("Find assets by name wildcard pattern"),
            {
                FSmithUEToolParam(TEXT("name_pattern"), TEXT("string"), TEXT("Wildcard pattern to match asset names (e.g. *Weapon*)"), true),
                FSmithUEToolParam(TEXT("asset_type"), TEXT("string"), TEXT("Optional: filter by asset type name"))
            }),
        &HandleFindAsset);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_asset_info"),
            TEXT("Asset"),
            TEXT("Get detailed information about a specific asset"),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path (e.g. /Game/Materials/M_Base)"), true)
            }),
        &HandleGetAssetInfo);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("rename_asset"),
            TEXT("Asset"),
            TEXT("Rename an asset to a new name within the same folder"),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path to rename"), true),
                FSmithUEToolParam(TEXT("new_name"), TEXT("string"), TEXT("New asset name (without path)"), true)
            }),
        &HandleRenameAsset);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("duplicate_asset"),
            TEXT("Asset"),
            TEXT("Duplicate an asset to a new path"),
            {
                FSmithUEToolParam(TEXT("source_path"), TEXT("string"), TEXT("Source asset path"), true),
                FSmithUEToolParam(TEXT("dest_path"), TEXT("string"), TEXT("Destination asset path (full path including new name)"), true)
            }),
        &HandleDuplicateAsset);
}

// ---------------------------------------------------------------------------
// Command: list_assets
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleListAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("folder_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString FolderPath;
    Params->TryGetStringField(TEXT("folder_path"), FolderPath);

    FString TypeFilter;
    Params->TryGetStringField(TEXT("type_filter"), TypeFilter);

    IAssetRegistry& AssetRegistry = GetAssetRegistry();

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;

    if (!TypeFilter.IsEmpty())
    {
        // UE 5.2: use ClassNames
        Filter.ClassNames.Add(FName(*TypeFilter));
        Filter.bRecursiveClasses = true;
    }

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    AssetsArray.Reserve(Assets.Num());
    for (const FAssetData& AssetData : Assets)
    {
        AssetsArray.Add(MakeShared<FJsonValueObject>(AssetDataToJson(AssetData, false)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("assets"), AssetsArray);
    Data->SetNumberField(TEXT("count"), Assets.Num());
    Data->SetStringField(TEXT("folder_path"), FolderPath);

    UE_LOG(LogSmithUE, Log, TEXT("list_assets: found %d assets in %s"), Assets.Num(), *FolderPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: find_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleFindAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name_pattern")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString NamePattern;
    Params->TryGetStringField(TEXT("name_pattern"), NamePattern);

    FString AssetType;
    Params->TryGetStringField(TEXT("asset_type"), AssetType);

    IAssetRegistry& AssetRegistry = GetAssetRegistry();

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(TEXT("/Game")));
    Filter.bRecursivePaths = true;

    if (!AssetType.IsEmpty())
    {
        Filter.ClassNames.Add(FName(*AssetType));
        Filter.bRecursiveClasses = true;
    }

    TArray<FAssetData> AllAssets;
    AssetRegistry.GetAssets(Filter, AllAssets);

    TArray<TSharedPtr<FJsonValue>> MatchingAssets;
    const int32 MaxResults = 100;

    for (const FAssetData& AssetData : AllAssets)
    {
        if (MatchingAssets.Num() >= MaxResults)
        {
            break;
        }

        const FString AssetName = AssetData.AssetName.ToString();
        if (AssetName.MatchesWildcard(NamePattern) || AssetName.Contains(NamePattern))
        {
            MatchingAssets.Add(MakeShared<FJsonValueObject>(AssetDataToJson(AssetData, false)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("assets"), MatchingAssets);
    Data->SetNumberField(TEXT("count"), MatchingAssets.Num());
    Data->SetStringField(TEXT("name_pattern"), NamePattern);

    UE_LOG(LogSmithUE, Log, TEXT("find_asset: pattern '%s' matched %d assets"), *NamePattern, MatchingAssets.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: get_asset_info
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    IAssetRegistry& AssetRegistry = GetAssetRegistry();
    FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));

    if (!AssetData.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not retrieve asset data for: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Data = AssetDataToJson(AssetData, true);

    // Hard dependencies
    TArray<FName> Dependencies;
    AssetRegistry.GetDependencies(AssetData.PackageName, Dependencies);
    TArray<TSharedPtr<FJsonValue>> DepsArray;
    for (const FName& Dep : Dependencies)
    {
        DepsArray.Add(MakeShared<FJsonValueString>(Dep.ToString()));
    }
    Data->SetArrayField(TEXT("dependencies"), DepsArray);

    // Referencers
    TArray<FName> Referencers;
    AssetRegistry.GetReferencers(AssetData.PackageName, Referencers);
    TArray<TSharedPtr<FJsonValue>> RefsArray;
    for (const FName& Ref : Referencers)
    {
        RefsArray.Add(MakeShared<FJsonValueString>(Ref.ToString()));
    }
    Data->SetArrayField(TEXT("referencers"), RefsArray);

    UE_LOG(LogSmithUE, Log, TEXT("get_asset_info: %s"), *AssetPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: rename_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleRenameAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path"), TEXT("new_name")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    FString NewName;
    Params->TryGetStringField(TEXT("new_name"), NewName);

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
    const FString NewPath = PackagePath / NewName;

    if (UEditorAssetLibrary::DoesAssetExist(NewPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists at destination: %s"), *NewPath));
    }

    if (!UEditorAssetLibrary::RenameAsset(AssetPath, NewPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to rename asset: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("old_path"), AssetPath);
    Data->SetStringField(TEXT("new_path"), NewPath);
    Data->SetStringField(TEXT("new_name"), NewName);

    UE_LOG(LogSmithUE, Log, TEXT("rename_asset: %s -> %s"), *AssetPath, *NewPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: duplicate_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleDuplicateAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("source_path"), TEXT("dest_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString SourcePath;
    Params->TryGetStringField(TEXT("source_path"), SourcePath);

    FString DestPath;
    Params->TryGetStringField(TEXT("dest_path"), DestPath);

    if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source asset not found: %s"), *SourcePath));
    }

    if (UEditorAssetLibrary::DoesAssetExist(DestPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Destination asset already exists: %s"), *DestPath));
    }

    if (!UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to duplicate asset: %s"), *SourcePath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("source_path"), SourcePath);
    Data->SetStringField(TEXT("dest_path"), DestPath);

    UE_LOG(LogSmithUE, Log, TEXT("duplicate_asset: %s -> %s"), *SourcePath, *DestPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

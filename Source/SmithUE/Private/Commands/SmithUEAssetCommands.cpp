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
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "FileHelpers.h"

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

    // Close all editors for an asset. Returns true if any editors were closed.
    // bWasDirty is set to true if the asset had unsaved changes before closing.
    bool CloseEditorsForAsset(UObject* Asset, bool& bWasDirty)
    {
        bWasDirty = false;
        if (!Asset) return false;

        UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!EditorSubsystem) return false;

        IAssetEditorInstance* Editor = EditorSubsystem->FindEditorForAsset(Asset, false);
        if (!Editor) return false;

        UPackage* Package = Asset->GetOutermost();
        if (Package)
        {
            bWasDirty = Package->IsDirty();
        }

        EditorSubsystem->CloseAllEditorsForAsset(Asset);
        return true;
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

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("delete_asset"),
            TEXT("Asset"),
            TEXT("Delete an asset. Checks references first and returns them if found. Use force=true to delete anyway."),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path to delete (e.g. /Game/Materials/M_Old)"), true),
                FSmithUEToolParam(TEXT("force"), TEXT("boolean"), TEXT("Force delete even if references exist (default: false)"))
            }),
        &HandleDeleteAsset);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("move_asset"),
            TEXT("Asset"),
            TEXT("Move an asset to a new path (different folder and/or name). Updates all references."),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Current full asset path"), true),
                FSmithUEToolParam(TEXT("new_path"), TEXT("string"), TEXT("New full asset path (e.g. /Game/NewFolder/NewName)"), true)
            }),
        &HandleMoveAsset);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("asset_editor"),
            TEXT("Asset"),
            TEXT("Open or close asset editors. Supports single or multiple assets."),
            {
                FSmithUEToolParam(TEXT("action"), TEXT("string"), TEXT("'open' or 'close'"), true),
                FSmithUEToolParam(TEXT("asset_paths"), TEXT("array"), TEXT("Array of asset paths (e.g. [\"/Game/Materials/M_A\", \"/Game/Materials/M_B\"])"), true)
            }),
        &HandleAssetEditor);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("save_asset"),
            TEXT("Asset"),
            TEXT("Save a single asset to disk"),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path to save (e.g. /Game/Materials/M_Base)"), true)
            }),
        &HandleSaveAsset);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("save_all_dirty"),
            TEXT("Asset"),
            TEXT("Save all dirty (modified) assets to disk"),
            {}),
        &HandleSaveAllDirty);
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

    // Use FARFilter on PackageNames — GetAssetByObjectPath fails when given
    // a package path (e.g. /Game/Foo/Bar) without the .ObjectName suffix.
    FARFilter Filter;
    Filter.PackageNames.Add(FName(*AssetPath));
    TArray<FAssetData> FoundAssets;
    AssetRegistry.GetAssets(Filter, FoundAssets);

    if (FoundAssets.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not retrieve asset data for: %s"), *AssetPath));
    }
    FAssetData AssetData = FoundAssets[0];

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

// ---------------------------------------------------------------------------
// Command: delete_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    bool bForce = false;
    Params->TryGetBoolField(TEXT("force"), bForce);

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    IAssetRegistry& AssetRegistry = GetAssetRegistry();

    // Use FARFilter on PackageNames — GetAssetByObjectPath fails when given
    // a package path (e.g. /Game/Foo/Bar) without the .ObjectName suffix.
    FARFilter Filter;
    Filter.PackageNames.Add(FName(*AssetPath));
    TArray<FAssetData> FoundAssets;
    AssetRegistry.GetAssets(Filter, FoundAssets);

    if (FoundAssets.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not retrieve asset data for: %s"), *AssetPath));
    }
    FAssetData AssetData = FoundAssets[0];

    // Check referencers
    TArray<FName> Referencers;
    AssetRegistry.GetReferencers(AssetData.PackageName, Referencers);

    // Filter out self-reference
    Referencers.RemoveAll([&](const FName& Ref) {
        return Ref == AssetData.PackageName;
    });

    if (Referencers.Num() > 0 && !bForce)
    {
        TArray<TSharedPtr<FJsonValue>> RefsArray;
        for (const FName& Ref : Referencers)
        {
            RefsArray.Add(MakeShared<FJsonValueString>(Ref.ToString()));
        }
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("asset_path"), AssetPath);
        Data->SetBoolField(TEXT("deleted"), false);
        Data->SetNumberField(TEXT("referencer_count"), Referencers.Num());
        Data->SetArrayField(TEXT("referencers"), RefsArray);
        Data->SetStringField(TEXT("hint"), TEXT("Use force=true to delete anyway"));
        // Return success=false with data so caller can see the referencers list
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("success"), false);
        Response->SetStringField(TEXT("error"),
            FString::Printf(TEXT("Asset has %d referencer(s). Use force=true to delete anyway."), Referencers.Num()));
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    // Close any open editors
    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    bool bWasDirty = false;
    if (Asset)
    {
        CloseEditorsForAsset(Asset, bWasDirty);
    }

    // Delete
    TArray<FAssetData> AssetsToDelete;
    AssetsToDelete.Add(AssetData);
    int32 DeletedCount = ObjectTools::DeleteAssets(AssetsToDelete, false);

    if (DeletedCount == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to delete asset: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetBoolField(TEXT("deleted"), true);
    Data->SetBoolField(TEXT("was_dirty"), bWasDirty);
    Data->SetBoolField(TEXT("force_used"), bForce && Referencers.Num() > 0);
    Data->SetNumberField(TEXT("referencers_removed"), bForce ? Referencers.Num() : 0);

    UE_LOG(LogSmithUE, Log, TEXT("delete_asset: deleted %s (force=%s, was_dirty=%s)"),
        *AssetPath, bForce ? TEXT("true") : TEXT("false"), bWasDirty ? TEXT("true") : TEXT("false"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: move_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleMoveAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path"), TEXT("new_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    FString NewPath;
    Params->TryGetStringField(TEXT("new_path"), NewPath);

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    if (UEditorAssetLibrary::DoesAssetExist(NewPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Destination asset already exists: %s"), *NewPath));
    }

    // Ensure destination directory exists (RenameAsset won't create it)
    const FString DestDir = FPackageName::GetLongPackagePath(NewPath);
    if (!UEditorAssetLibrary::DoesDirectoryExist(DestDir))
    {
        if (!UEditorAssetLibrary::MakeDirectory(DestDir))
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to create destination directory: %s"), *DestDir));
        }
    }

    // Close any open editors before moving
    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    bool bWasDirty = false;
    bool bEditorsWereClosed = false;
    if (Asset)
    {
        bEditorsWereClosed = CloseEditorsForAsset(Asset, bWasDirty);
    }

    if (!UEditorAssetLibrary::RenameAsset(AssetPath, NewPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to move asset from %s to %s"), *AssetPath, *NewPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("old_path"), AssetPath);
    Data->SetStringField(TEXT("new_path"), NewPath);
    Data->SetBoolField(TEXT("was_dirty"), bWasDirty);
    Data->SetBoolField(TEXT("editors_closed"), bEditorsWereClosed);

    UE_LOG(LogSmithUE, Log, TEXT("move_asset: %s -> %s"), *AssetPath, *NewPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: asset_editor (open / close, single or multiple assets)
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleAssetEditor(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("action"), TEXT("asset_paths")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString Action;
    Params->TryGetStringField(TEXT("action"), Action);
    Action = Action.ToLower();

    if (Action != TEXT("open") && Action != TEXT("close"))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid action '%s'. Must be 'open' or 'close'."), *Action));
    }

    const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
    if (!Params->TryGetArrayField(TEXT("asset_paths"), PathsArray) || !PathsArray || PathsArray->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("asset_paths must be a non-empty array of strings"));
    }

    UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!EditorSubsystem)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("AssetEditorSubsystem not available"));
    }

    TArray<TSharedPtr<FJsonValue>> ResultsArray;
    ResultsArray.Reserve(PathsArray->Num());
    int32 SuccessCount = 0;

    for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
    {
        FString AssetPath = PathValue->AsString();
        TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
        ItemResult->SetStringField(TEXT("asset_path"), AssetPath);

        if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            ItemResult->SetBoolField(TEXT("success"), false);
            ItemResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
            ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
            continue;
        }

        UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
        if (!Asset)
        {
            ItemResult->SetBoolField(TEXT("success"), false);
            ItemResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
            ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
            continue;
        }

        if (Action == TEXT("open"))
        {
            const bool bOpened = EditorSubsystem->OpenEditorForAsset(Asset);
            ItemResult->SetBoolField(TEXT("success"), bOpened);
            ItemResult->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
            if (bOpened) ++SuccessCount;
        }
        else // close
        {
            bool bWasDirty = false;
            bool bClosed = CloseEditorsForAsset(Asset, bWasDirty);
            ItemResult->SetBoolField(TEXT("success"), true); // closing is always "success" even if no editor was open
            ItemResult->SetBoolField(TEXT("editor_was_open"), bClosed);
            ItemResult->SetBoolField(TEXT("was_dirty"), bWasDirty);
            ++SuccessCount;
        }

        ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("action"), Action);
    Data->SetNumberField(TEXT("total"), PathsArray->Num());
    Data->SetNumberField(TEXT("succeeded"), SuccessCount);
    Data->SetArrayField(TEXT("results"), ResultsArray);

    UE_LOG(LogSmithUE, Log, TEXT("asset_editor: %s %d/%d assets"),
        *Action, SuccessCount, PathsArray->Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: save_asset
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleSaveAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    if (AssetPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("asset_path cannot be empty"));
    }

    // Check if asset exists
    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    // Load asset to verify it exists and is valid
    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!Asset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
    }

    // Save the asset silently (no dialog)
    bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, false);

    if (!bSaved)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("saved"), true);
    Data->SetStringField(TEXT("asset_path"), AssetPath);

    UE_LOG(LogSmithUE, Log, TEXT("save_asset: saved %s"), *AssetPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: save_all_dirty
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleSaveAllDirty(const TSharedPtr<FJsonObject>& Params)
{
    // No parameters required for this command
    
    // Save all dirty packages silently
    // Parameters: bPromptUserToSave=false, bSaveMapPackages=true, bSaveContentPackages=true
    int32 SavedCount = FEditorFileUtils::SaveDirtyPackages(false, true, true);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("saved_count"), SavedCount);
    Data->SetStringField(TEXT("status"), TEXT("success"));

    UE_LOG(LogSmithUE, Log, TEXT("save_all_dirty: saved %d dirty assets"), SavedCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

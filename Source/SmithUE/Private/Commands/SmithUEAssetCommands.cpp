// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEAssetCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "Utils/SmithUEAssetPropertyUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/StrongObjectPtr.h"
#include "Dialog/SmithUEDialogWatcher.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "FileHelpers.h"
#include "Misc/OutputDeviceNull.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    struct FAssetPropertyPathSegment
    {
        FString Name;
        int32 Index = INDEX_NONE;
        bool bHasIndex = false;
    };

    // FAssetResolvedPropertyPath is defined in Utils/SmithUEAssetPropertyUtils.h

    FString AssetJsonValueToImportText(const TSharedPtr<FJsonValue>& JsonValue)
    {
        if (!JsonValue.IsValid() || JsonValue->IsNull())
        {
            return TEXT("None");
        }
        switch (JsonValue->Type)
        {
            case EJson::String: return JsonValue->AsString();
            case EJson::Number: return FString::SanitizeFloat(JsonValue->AsNumber());
            case EJson::Boolean: return JsonValue->AsBool() ? TEXT("True") : TEXT("False");
            default: break;
        }
        FString Serialized;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
        FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT(""), Writer);
        return Serialized;
    }

    bool ParseAssetPropertyPathSegment(const FString& SegmentText, FAssetPropertyPathSegment& OutSegment, FString& OutError)
    {
        OutSegment = FAssetPropertyPathSegment();
        const int32 BracketStart = SegmentText.Find(TEXT("["), ESearchCase::CaseSensitive);
        if (BracketStart == INDEX_NONE)
        {
            OutSegment.Name = SegmentText;
        }
        else
        {
            if (!SegmentText.EndsWith(TEXT("]")))
            {
                OutError = FString::Printf(TEXT("Invalid indexed property segment '%s'"), *SegmentText);
                return false;
            }
            OutSegment.Name = SegmentText.Left(BracketStart);
            const FString IndexText = SegmentText.Mid(BracketStart + 1, SegmentText.Len() - BracketStart - 2);
            if (!LexTryParseString(OutSegment.Index, *IndexText) || OutSegment.Index < 0)
            {
                OutError = FString::Printf(TEXT("Invalid array index in property segment '%s'"), *SegmentText);
                return false;
            }
            OutSegment.bHasIndex = true;
        }

        if (OutSegment.Name.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Invalid empty property segment in '%s'"), *SegmentText);
            return false;
        }
        return true;
    }

    FString ExportAssetPropertyValue(FProperty* Property, void* ValuePtr, UObject* Owner)
    {
        FString Value;
        if (Property && ValuePtr)
        {
            Property->ExportTextItem_Direct(Value, ValuePtr, nullptr, Owner, PPF_None);
        }
        return Value;
    }

    bool ImportAssetPropertyValueWithNotify(UObject* Object, FAssetResolvedPropertyPath& Resolved, const FString& TextValue, FString& OutError)
    {
        if (!Object || !Resolved.TopLevelProperty || !Resolved.LeafProperty || !Resolved.LeafValuePtr)
        {
            OutError = TEXT("Invalid resolved property path");
            return false;
        }

        Resolved.Chain.SetActivePropertyNode(Resolved.LeafProperty);
        Resolved.Chain.SetActiveMemberPropertyNode(Resolved.TopLevelProperty);
        Object->PreEditChange(Resolved.Chain);
        FOutputDeviceNull ErrorDevice;
        const TCHAR* ImportResult = Resolved.LeafProperty->ImportText_Direct(*TextValue, Resolved.LeafValuePtr, Object, PPF_None, &ErrorDevice);
        if (!ImportResult)
        {
            OutError = FString::Printf(TEXT("Failed to import value '%s' into property '%s'"), *TextValue, *Resolved.LeafProperty->GetName());
            return false;
        }
        FPropertyChangedEvent Event(Resolved.LeafProperty, EPropertyChangeType::ValueSet, MakeArrayView((const UObject* const*)&Object, 1));
        Event.SetActiveMemberProperty(Resolved.TopLevelProperty);
        FPropertyChangedChainEvent ChainEvent(Resolved.Chain, Event);
        Object->PostEditChangeChainProperty(ChainEvent);
        return true;
    }

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
// ResolveAssetObjectPropertyPath (global — shared with SmithUEAssetAuditCommands)
// ---------------------------------------------------------------------------

bool ResolveAssetObjectPropertyPath(UObject* Object, const FString& PropertyPath, FAssetResolvedPropertyPath& OutResolved, FString& OutError)
{
    if (!Object)
    {
        OutError = TEXT("Invalid object");
        return false;
    }

    TArray<FString> SegmentTexts;
    PropertyPath.ParseIntoArray(SegmentTexts, TEXT("."), true);
    if (SegmentTexts.Num() == 0)
    {
        OutError = TEXT("property_path is empty");
        return false;
    }

    void* Container = Object;
    UStruct* CurrentStruct = Object->GetClass();
    for (int32 SegmentIndex = 0; SegmentIndex < SegmentTexts.Num(); ++SegmentIndex)
    {
        FAssetPropertyPathSegment Segment;
        if (!ParseAssetPropertyPathSegment(SegmentTexts[SegmentIndex], Segment, OutError))
        {
            return false;
        }

        if (!CurrentStruct)
        {
            OutError = FString::Printf(TEXT("Cannot resolve '%s' after non-struct property in '%s'"), *Segment.Name, *PropertyPath);
            return false;
        }

        FProperty* Property = CurrentStruct->FindPropertyByName(FName(*Segment.Name));
        if (!Property)
        {
            OutError = FString::Printf(TEXT("Property not found: '%s' on '%s'"), *Segment.Name, *CurrentStruct->GetName());
            return false;
        }

        OutResolved.Chain.AddTail(Property);
        if (!OutResolved.TopLevelProperty)
        {
            OutResolved.TopLevelProperty = Property;
        }
        OutResolved.LeafProperty = Property;
        void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

        if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            if (!Segment.bHasIndex)
            {
                if (SegmentIndex != SegmentTexts.Num() - 1)
                {
                    OutError = FString::Printf(TEXT("Array property '%s' requires an index"), *Segment.Name);
                    return false;
                }
                OutResolved.LeafValuePtr = ValuePtr;
                CurrentStruct = nullptr;
                Container = ValuePtr;
                continue;
            }

            FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
            if (!ArrayHelper.IsValidIndex(Segment.Index))
            {
                OutError = FString::Printf(TEXT("Array index %d out of range for '%s' (num=%d)"), Segment.Index, *Segment.Name, ArrayHelper.Num());
                return false;
            }

            void* ElementPtr = ArrayHelper.GetRawPtr(Segment.Index);
            if (FStructProperty* InnerStructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
            {
                OutResolved.Chain.AddTail(ArrayProperty->Inner);
                OutResolved.LeafProperty = ArrayProperty->Inner;
                OutResolved.LeafValuePtr = ElementPtr;
                CurrentStruct = InnerStructProperty->Struct;
                Container = ElementPtr;
            }
            else
            {
                OutResolved.Chain.AddTail(ArrayProperty->Inner);
                OutResolved.LeafProperty = ArrayProperty->Inner;
                OutResolved.LeafValuePtr = ElementPtr;
                CurrentStruct = nullptr;
                Container = ElementPtr;
            }
            continue;
        }

        if (Segment.bHasIndex)
        {
            OutError = FString::Printf(TEXT("Property '%s' is not an array"), *Segment.Name);
            return false;
        }

        OutResolved.LeafValuePtr = ValuePtr;
        if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            CurrentStruct = StructProperty->Struct;
            Container = ValuePtr;
        }
        else
        {
            CurrentStruct = nullptr;
            Container = ValuePtr;
        }
    }

    if (!OutResolved.TopLevelProperty || !OutResolved.LeafProperty || !OutResolved.LeafValuePtr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve property_path '%s'"), *PropertyPath);
        return false;
    }
    return true;
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
            TEXT("Delete an asset. Checks references first; returns referencers if found. force=true force-deletes even with in-memory referencers, nulling them."),
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
            TEXT("move_folder"),
            TEXT("Asset"),
            TEXT("Move all assets under a content folder (recursive) to another mount point/folder, preserving relative structure. Uses batched IAssetTools::RenameAssets, which updates all references and leaves redirectors. Supports dry_run."),
            {
                FSmithUEToolParam(TEXT("source_folder"), TEXT("string"), TEXT("Source content folder (e.g. /Game/UltraDynamicSky)"), true),
                FSmithUEToolParam(TEXT("dest_folder"), TEXT("string"), TEXT("Destination content folder (e.g. /UltraDynamicSky). Mount point must exist."), true),
                FSmithUEToolParam(TEXT("dry_run"), TEXT("boolean"), TEXT("Preview the rename plan without applying (default: false)")),
                FSmithUEToolParam(TEXT("save"), TEXT("boolean"), TEXT("Save all dirty packages after moving (default: true)"))
            }),
        &HandleMoveFolder);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_dependency_closure"),
            TEXT("Asset"),
            TEXT("Compute the full recursive dependency closure of one or more root assets, restricted to a content prefix (default /Game). Returns every dependency package plus a 'shared' flag for those still referenced by packages OUTSIDE the closure (i.e. would break other content if migrated). Read-only. Use before migrating a map's dependencies into a plugin."),
            {
                FSmithUEToolParam(TEXT("root_assets"), TEXT("array"), TEXT("Root asset/object paths (e.g. [\"/Plugin/Maps/MyLevel\"])"), true),
                FSmithUEToolParam(TEXT("content_prefix"), TEXT("string"), TEXT("Only follow/collect dependencies under this prefix (default /Game)")),
                FSmithUEToolParam(TEXT("max_list"), TEXT("number"), TEXT("Cap the returned package/shared lists (counts are always exact; default 1000)"))
            }),
        &HandleGetDependencyClosure);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("move_paths"),
            TEXT("Asset"),
            TEXT("Batch-move an explicit list of asset packages to a destination root, remapping strip_prefix->dest_root (e.g. /Game/Building/X -> /SOBSJunction3/Building/X). Uses one batched IAssetTools::RenameAssets (updates references, leaves redirectors). Pair with get_dependency_closure to migrate a map's whole dependency set. Batch renames of Blueprints can raise CDO-reference confirm modals: arm set_dialog_auto_response mode=confirm first. Supports dry_run."),
            {
                FSmithUEToolParam(TEXT("paths"), TEXT("array"), TEXT("Asset/package paths to move (e.g. [\"/Game/Building/SM_X\"])"), true),
                FSmithUEToolParam(TEXT("dest_root"), TEXT("string"), TEXT("Destination mount/root (e.g. /SOBSJunction3). Must be a mounted content root."), true),
                FSmithUEToolParam(TEXT("strip_prefix"), TEXT("string"), TEXT("Prefix stripped from each source path before prepending dest_root (default /Game)")),
                FSmithUEToolParam(TEXT("dry_run"), TEXT("boolean"), TEXT("Preview the remap plan without applying (default false)")),
                FSmithUEToolParam(TEXT("save"), TEXT("boolean"), TEXT("Save all dirty packages after moving (default true)"))
            }),
        &HandleMovePaths);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("fixup_redirectors"),
            TEXT("Asset"),
            TEXT("Clean up ObjectRedirectors under a folder (recursive). SAFE DEFAULT: only deletes redirectors that have NO external referencers (the normal state after a folder migration, since references were already rewritten) - this avoids AssetTools::FixupReferencers, which ASSERTS/crashes on World & Blueprint-class/CDO redirectors left by migrated .umap packages. Redirectors that still have referencers are reported (not touched) unless force_fixup=true."),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder to scan for redirectors (e.g. /Game)"), true),
                FSmithUEToolParam(TEXT("force_fixup"), TEXT("boolean"), TEXT("Run AssetTools::FixupReferencers over still-referenced redirectors (rewrites+saves referencers). Default false. UNSAFE for World/Blueprint redirectors - can crash the editor."))
            }),
        &HandleFixupRedirectors);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("resolve_redirectors"),
            TEXT("Asset"),
            TEXT("Resolve ObjectRedirectors under a folder by REWRITING every referencer (hard AND soft references) to point at the redirector's real target, then deleting the redirector. Uses ObjectTools::ConsolidateObjects per redirector - a DIFFERENT engine code path than fixup_redirectors/AssetTools::FixupReferencers (which asserts/crashes on some projects in UE5.8). Handles the soft-reference redirectors left after batch-migrating a map's dependencies. Saves modified packages."),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder to scan for redirectors (e.g. /Game)"), true),
                FSmithUEToolParam(TEXT("max_resolve"), TEXT("number"), TEXT("Optional cap on how many redirectors to resolve this call (for batching very large sets; default: all)")),
                FSmithUEToolParam(TEXT("save"), TEXT("boolean"), TEXT("Save modified packages after resolving (default true)"))
            }),
        &HandleResolveRedirectors);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("consolidate_assets"),
            TEXT("Asset"),
            TEXT("Consolidate assets: replace all references to assets_to_merge with asset_to_keep, then delete the merged assets (same as Content Browser 'Replace References'). All assets must be of compatible classes."),
            {
                FSmithUEToolParam(TEXT("asset_to_keep"), TEXT("string"), TEXT("Asset path that references will point to after consolidation"), true),
                FSmithUEToolParam(TEXT("assets_to_merge"), TEXT("array"), TEXT("Array of asset paths to be replaced by asset_to_keep and deleted"), true)
            }),
        &HandleConsolidateAssets);

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

    Registry.Register(FSmithUEToolSchema(TEXT("set_asset_property"), TEXT("Asset"),
        TEXT("Set any property on a loaded UObject asset (Texture2D, StaticMesh, SkeletalMesh, Material, etc.) by dotted property path. Use for texture compression, LOD settings, mesh properties, etc."),
        {
            FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Full asset path (e.g. /Game/BP/T_Wood_Normal.T_Wood_Normal)"), true),
            FSmithUEToolParam(TEXT("property_path"), TEXT("string"), TEXT("Dotted property path (e.g. CompressionSettings, SRGB, LightMapResolution, BodySetup.CollisionTraceFlag)"), true),
            FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value to set (string representation; enums use name like TC_Normalmap)"), true),
            FSmithUEToolParam(TEXT("save"), TEXT("boolean"), TEXT("Auto-save the asset after modification (default: true)"))
        }),
        &HandleSetAssetProperty);

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

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_content_browser_selection"),
            TEXT("Asset"),
            TEXT("Get the folders and assets currently selected in the Content Browser"),
            {}),
        &HandleGetContentBrowserSelection);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("sync_content_browser"),
            TEXT("Asset"),
            TEXT("Navigate the Content Browser to a folder or asset and bring it to focus"),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder to navigate to (e.g. /Game/Materials)")),
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Asset to select and reveal (e.g. /Game/Materials/M_Base)"))
            }),
        &HandleSyncContentBrowser);
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
        // UE 5.1+: ClassPaths replaces ClassNames
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->GetName() == TypeFilter)
            {
                Filter.ClassPaths.Add(It->GetClassPathName());
                break;
            }
        }
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
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->GetName() == AssetType)
            {
                Filter.ClassPaths.Add(It->GetClassPathName());
                break;
            }
        }
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

    // Delete — force path uses ForceDeleteObjects which nulls in-memory referencers;
    // non-force path uses DeleteAssets which aborts if in-memory referencers exist.
    int32 DeletedCount = 0;
    if (bForce && Asset)
    {
        TArray<UObject*> ObjectsToForceDelete;
        ObjectsToForceDelete.Add(Asset);
        DeletedCount = ObjectTools::ForceDeleteObjects(ObjectsToForceDelete, /*bShowConfirmation=*/false);
    }
    else
    {
        TArray<FAssetData> AssetsToDelete;
        AssetsToDelete.Add(AssetData);
        DeletedCount = ObjectTools::DeleteAssets(AssetsToDelete, false);
    }

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
// Command: move_folder
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleMoveFolder(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("source_folder"), TEXT("dest_folder")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString SourceFolder, DestFolder;
    Params->TryGetStringField(TEXT("source_folder"), SourceFolder);
    Params->TryGetStringField(TEXT("dest_folder"), DestFolder);

    bool bDryRun = false;
    Params->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bSave = true;
    Params->TryGetBoolField(TEXT("save"), bSave);

    // Normalize: strip trailing slashes
    while (SourceFolder.EndsWith(TEXT("/"))) { SourceFolder.LeftChopInline(1); }
    while (DestFolder.EndsWith(TEXT("/"))) { DestFolder.LeftChopInline(1); }

    if (SourceFolder.IsEmpty() || DestFolder.IsEmpty() || !SourceFolder.StartsWith(TEXT("/")) || !DestFolder.StartsWith(TEXT("/")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("source_folder and dest_folder must be long package paths like /Game/Foo or /PluginName"));
    }

    if (DestFolder.StartsWith(SourceFolder + TEXT("/")) || DestFolder == SourceFolder)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("dest_folder must not be inside source_folder"));
    }

    // Destination mount point must exist (e.g. content plugin already loaded)
    if (FPackageName::GetPackageMountPoint(DestFolder + TEXT("/__probe__")).IsNone())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Destination mount point does not exist for '%s'. Is the content plugin enabled and mounted?"), *DestFolder));
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
    AssetRegistry.ScanPathsSynchronous({ SourceFolder }, /*bForceRescan*/ false);

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssetsByPath(FName(*SourceFolder), Assets, /*bRecursive*/ true, /*bIncludeOnlyOnDiskAssets*/ false);

    if (Assets.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("No assets found under %s"), *SourceFolder));
    }

    TArray<FAssetRenameData> RenameData;
    RenameData.Reserve(Assets.Num());
    TArray<TSharedPtr<FJsonValue>> PlanArray;
    int32 SkippedExisting = 0;

    for (const FAssetData& Asset : Assets)
    {
        const FString OldPackageName = Asset.PackageName.ToString();
        if (!OldPackageName.StartsWith(SourceFolder))
        {
            continue;
        }
        const FString Relative = OldPackageName.Mid(SourceFolder.Len()); // starts with '/'
        const FString NewPackageName = DestFolder + Relative;
        const FString NewPackagePath = FPackageName::GetLongPackagePath(NewPackageName);
        const FString AssetName = Asset.AssetName.ToString();

        const FString NewObjectPath = NewPackageName + TEXT(".") + AssetName;
        if (UEditorAssetLibrary::DoesAssetExist(NewObjectPath))
        {
            SkippedExisting++;
            continue;
        }

        if (bDryRun)
        {
            if (PlanArray.Num() < 50)
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("from"), Asset.GetObjectPathString());
                Item->SetStringField(TEXT("to"), NewObjectPath);
                PlanArray.Add(MakeShared<FJsonValueObject>(Item));
            }
            continue;
        }

        UObject* AssetObject = Asset.GetAsset();
        if (!AssetObject)
        {
            continue;
        }
        // Close any open editors for this asset before renaming
        bool bWasDirtyUnused = false;
        CloseEditorsForAsset(AssetObject, bWasDirtyUnused);
        RenameData.Add(FAssetRenameData(AssetObject, NewPackagePath, AssetName));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("source_folder"), SourceFolder);
    Data->SetStringField(TEXT("dest_folder"), DestFolder);
    Data->SetNumberField(TEXT("total_assets"), Assets.Num());
    Data->SetNumberField(TEXT("skipped_existing"), SkippedExisting);

    if (bDryRun)
    {
        Data->SetBoolField(TEXT("dry_run"), true);
        Data->SetNumberField(TEXT("planned_moves"), Assets.Num() - SkippedExisting);
        Data->SetArrayField(TEXT("plan_sample"), PlanArray);
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    const bool bRenameOk = AssetToolsModule.Get().RenameAssets(RenameData);

    int32 SavedCount = 0;
    if (bSave)
    {
        TArray<UPackage*> DirtyPackages;
        FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
        FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
        SavedCount = DirtyPackages.Num();
        FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ false, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ true);
    }

    Data->SetBoolField(TEXT("rename_succeeded"), bRenameOk);
    Data->SetNumberField(TEXT("moved"), RenameData.Num());
    Data->SetNumberField(TEXT("saved_dirty_packages"), SavedCount);

    UE_LOG(LogSmithUE, Log, TEXT("move_folder: %s -> %s (%d assets, ok=%s)"),
        *SourceFolder, *DestFolder, RenameData.Num(), bRenameOk ? TEXT("true") : TEXT("false"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: get_dependency_closure
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleGetDependencyClosure(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* RootsArr = nullptr;
    if (!Params->TryGetArrayField(TEXT("root_assets"), RootsArr) || !RootsArr || RootsArr->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("root_assets must be a non-empty array of asset paths"));
    }

    FString ContentPrefix = TEXT("/Game");
    Params->TryGetStringField(TEXT("content_prefix"), ContentPrefix);
    while (ContentPrefix.EndsWith(TEXT("/"))) { ContentPrefix.LeftChopInline(1); }

    int32 MaxList = 1000;
    { double Tmp; if (Params->TryGetNumberField(TEXT("max_list"), Tmp)) { MaxList = FMath::Max(1, (int32)Tmp); } }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Seed BFS from the root packages.
    TSet<FName> RootPackages;
    TArray<FName> Frontier;
    for (const TSharedPtr<FJsonValue>& V : *RootsArr)
    {
        const FString PkgName = FPackageName::ObjectPathToPackageName(V->AsString());
        const FName PkgFName(*PkgName);
        RootPackages.Add(PkgFName);
        Frontier.Add(PkgFName);
    }

    const FString PrefixSlash = ContentPrefix + TEXT("/");

    // BFS over package dependencies, collecting those under the content prefix.
    TSet<FName> Closure;
    while (Frontier.Num() > 0)
    {
        const FName Pkg = Frontier.Pop(EAllowShrinking::No);
        TArray<FName> Deps;
        AssetRegistry.GetDependencies(Pkg, Deps, UE::AssetRegistry::EDependencyCategory::Package);
        for (const FName& Dep : Deps)
        {
            const FString DepStr = Dep.ToString();
            if (!DepStr.StartsWith(PrefixSlash))
            {
                continue; // engine / other-plugin / out-of-scope
            }
            if (Closure.Contains(Dep))
            {
                continue;
            }
            Closure.Add(Dep);
            Frontier.Add(Dep);
        }
    }

    // Shared detection: a closure package still referenced by a package that is
    // neither in the closure nor a root would break if migrated away.
    int32 SharedCount = 0;
    TArray<TSharedPtr<FJsonValue>> PackageArr;
    TArray<TSharedPtr<FJsonValue>> SharedArr;
    for (const FName& Pkg : Closure)
    {
        TArray<FName> Referencers;
        AssetRegistry.GetReferencers(Pkg, Referencers, UE::AssetRegistry::EDependencyCategory::Package);

        FString ExternalSample;
        for (const FName& Ref : Referencers)
        {
            if (Ref == Pkg || Closure.Contains(Ref) || RootPackages.Contains(Ref))
            {
                continue;
            }
            ExternalSample = Ref.ToString();
            break;
        }
        const bool bShared = !ExternalSample.IsEmpty();
        if (bShared)
        {
            ++SharedCount;
            if (SharedArr.Num() < MaxList)
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("package"), Pkg.ToString());
                Item->SetStringField(TEXT("external_referencer"), ExternalSample);
                SharedArr.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
        if (PackageArr.Num() < MaxList)
        {
            PackageArr.Add(MakeShared<FJsonValueString>(Pkg.ToString()));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("content_prefix"), ContentPrefix);
    Data->SetNumberField(TEXT("root_count"), RootPackages.Num());
    Data->SetNumberField(TEXT("closure_count"), Closure.Num());
    Data->SetNumberField(TEXT("shared_count"), SharedCount);
    Data->SetNumberField(TEXT("migratable_count"), Closure.Num() - SharedCount);
    Data->SetArrayField(TEXT("packages"), PackageArr);
    Data->SetArrayField(TEXT("shared"), SharedArr);
    Data->SetBoolField(TEXT("list_truncated"), Closure.Num() > MaxList);

    UE_LOG(LogSmithUE, Log, TEXT("get_dependency_closure: roots=%d closure=%d shared=%d"),
        RootPackages.Num(), Closure.Num(), SharedCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: move_paths
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleMovePaths(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
    if (!Params->TryGetArrayField(TEXT("paths"), PathsArr) || !PathsArr || PathsArr->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("paths must be a non-empty array of asset paths"));
    }

    FString DestRoot;
    if (!Params->TryGetStringField(TEXT("dest_root"), DestRoot) || DestRoot.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("dest_root is required (e.g. /SOBSJunction3)"));
    }
    while (DestRoot.EndsWith(TEXT("/"))) { DestRoot.LeftChopInline(1); }

    FString StripPrefix = TEXT("/Game");
    Params->TryGetStringField(TEXT("strip_prefix"), StripPrefix);
    while (StripPrefix.EndsWith(TEXT("/"))) { StripPrefix.LeftChopInline(1); }

    bool bDryRun = false; Params->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bSave = true;     Params->TryGetBoolField(TEXT("save"), bSave);

    if (FPackageName::GetPackageMountPoint(DestRoot + TEXT("/__probe__")).IsNone())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Destination mount point does not exist for '%s'. Is the content plugin enabled and mounted?"), *DestRoot));
    }

    const FString StripSlash = StripPrefix + TEXT("/");

    TArray<FAssetRenameData> RenameData;
    TArray<TSharedPtr<FJsonValue>> PlanArr;
    int32 SkippedExisting = 0, SkippedBadPrefix = 0, SkippedMissing = 0;

    for (const TSharedPtr<FJsonValue>& V : *PathsArr)
    {
        const FString InPath = V->AsString();
        const FString PkgName = FPackageName::ObjectPathToPackageName(InPath);
        if (!(PkgName + TEXT("/")).StartsWith(StripSlash) && PkgName != StripPrefix)
        {
            ++SkippedBadPrefix;
            continue;
        }

        const FString Relative = PkgName.Mid(StripPrefix.Len()); // starts with '/'
        const FString NewPkgName = DestRoot + Relative;
        const FString NewPkgPath = FPackageName::GetLongPackagePath(NewPkgName);

        if (!UEditorAssetLibrary::DoesAssetExist(PkgName))
        {
            ++SkippedMissing;
            continue;
        }
        const FString AssetName = FPackageName::GetLongPackageAssetName(PkgName);
        const FString NewObjectPath = NewPkgName + TEXT(".") + AssetName;
        if (UEditorAssetLibrary::DoesAssetExist(NewObjectPath))
        {
            ++SkippedExisting;
            continue;
        }

        if (bDryRun)
        {
            if (PlanArr.Num() < 100)
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("from"), PkgName);
                Item->SetStringField(TEXT("to"), NewPkgName);
                PlanArr.Add(MakeShared<FJsonValueObject>(Item));
            }
            continue;
        }

        UObject* AssetObject = UEditorAssetLibrary::LoadAsset(PkgName);
        if (!AssetObject)
        {
            ++SkippedMissing;
            continue;
        }
        bool bWasDirtyUnused = false;
        CloseEditorsForAsset(AssetObject, bWasDirtyUnused);
        RenameData.Add(FAssetRenameData(AssetObject, NewPkgPath, AssetName));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("dest_root"), DestRoot);
    Data->SetStringField(TEXT("strip_prefix"), StripPrefix);
    Data->SetNumberField(TEXT("input_count"), PathsArr->Num());
    Data->SetNumberField(TEXT("skipped_existing"), SkippedExisting);
    Data->SetNumberField(TEXT("skipped_bad_prefix"), SkippedBadPrefix);
    Data->SetNumberField(TEXT("skipped_missing"), SkippedMissing);

    if (bDryRun)
    {
        Data->SetBoolField(TEXT("dry_run"), true);
        Data->SetNumberField(TEXT("planned_moves"), PathsArr->Num() - SkippedExisting - SkippedBadPrefix - SkippedMissing);
        Data->SetArrayField(TEXT("plan_sample"), PlanArr);
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    const bool bRenameOk = AssetToolsModule.Get().RenameAssets(RenameData);

    int32 SavedCount = 0;
    if (bSave)
    {
        TArray<UPackage*> DirtyPackages;
        FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
        FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
        SavedCount = DirtyPackages.Num();
        FEditorFileUtils::SaveDirtyPackages(false, true, true);
    }

    Data->SetBoolField(TEXT("rename_succeeded"), bRenameOk);
    Data->SetNumberField(TEXT("moved"), RenameData.Num());
    Data->SetNumberField(TEXT("saved_dirty_packages"), SavedCount);

    UE_LOG(LogSmithUE, Log, TEXT("move_paths: -> %s (moved %d, ok=%s)"),
        *DestRoot, RenameData.Num(), bRenameOk ? TEXT("true") : TEXT("false"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: fixup_redirectors
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleFixupRedirectors(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("folder_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString FolderPath;
    Params->TryGetStringField(TEXT("folder_path"), FolderPath);
    while (FolderPath.EndsWith(TEXT("/"))) { FolderPath.LeftChopInline(1); }

    // force_fixup=true routes still-referenced redirectors through
    // AssetTools::FixupReferencers (rewrites + saves referencers). Default false:
    // we ONLY delete redirectors that have no external referencers. This is the
    // safe path for post-migration cleanup, and critically it AVOIDS running
    // FixupReferencers over World / Blueprint-class / CDO redirectors (e.g. a
    // migrated .umap leaves World + _C + Default__*_C redirectors), which asserts
    // deep inside the engine (Optional.h GetValue on an unset TOptional).
    bool bForceFixup = false;
    Params->TryGetBoolField(TEXT("force_fixup"), bForceFixup);

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());

    TArray<FAssetData> RedirectorAssets;
    AssetRegistry.GetAssets(Filter, RedirectorAssets);

    TArray<UObject*> SafeToDelete;                     // redirectors with no external referencers
    TArray<UObjectRedirector*> ReferencedRedirectors;  // still referenced -> need real fixup
    TArray<TSharedPtr<FJsonValue>> DeletedList;
    TArray<TSharedPtr<FJsonValue>> ReferencedList;
    TSet<FName> CountedPackages;

    for (const FAssetData& Asset : RedirectorAssets)
    {
        UObjectRedirector* Redirector = Cast<UObjectRedirector>(Asset.GetAsset());
        if (!Redirector)
        {
            continue;
        }

        const FName PkgName = Asset.PackageName;
        TArray<FName> Referencers;
        AssetRegistry.GetReferencers(PkgName, Referencers);
        Referencers.RemoveAll([&PkgName](const FName& R) { return R == PkgName; });

        if (Referencers.Num() == 0)
        {
            SafeToDelete.Add(Redirector);
            if (DeletedList.Num() < 100 && !CountedPackages.Contains(PkgName))
            {
                DeletedList.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
            }
        }
        else
        {
            ReferencedRedirectors.Add(Redirector);
            if (ReferencedList.Num() < 100)
            {
                ReferencedList.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
            }
        }
        CountedPackages.Add(PkgName);
    }

    int32 DeletedCount = 0;
    if (SafeToDelete.Num() > 0)
    {
        // bShowConfirmation=false; these have no referencers so no force-delete prompt.
        DeletedCount = ObjectTools::DeleteObjects(SafeToDelete, /*bShowConfirmation*/ false);
    }

    int32 FixedUpCount = 0;
    if (bForceFixup && ReferencedRedirectors.Num() > 0)
    {
        // Only reachable when the caller explicitly opts in. Arm the DialogWatcher
        // to auto-close the fixup report modal (runs inside the nested modal loop),
        // instead of forcing GIsRunningUnattendedScript (which corrupts AssetTools'
        // save path). NOTE: still unsafe for World/Blueprint-class redirectors.
        FSmithUEDialogWatcher& Watcher = FSmithUEDialogWatcher::Get();
        const FSmithUEDialogWatcher::EResponse PrevMode = Watcher.GetAutoResponse();
        Watcher.SetAutoResponse(FSmithUEDialogWatcher::EResponse::Accept);

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        AssetToolsModule.Get().FixupReferencers(ReferencedRedirectors, /*bCheckoutDialogPrompt*/ false, ERedirectFixupMode::DeleteFixedUpRedirectors);

        Watcher.SetAutoResponse(PrevMode);
        FixedUpCount = ReferencedRedirectors.Num();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("folder_path"), FolderPath);
    Data->SetNumberField(TEXT("redirectors_found"), RedirectorAssets.Num());
    Data->SetNumberField(TEXT("deleted_unreferenced"), DeletedCount);
    Data->SetArrayField(TEXT("deleted"), DeletedList);
    Data->SetNumberField(TEXT("still_referenced"), ReferencedRedirectors.Num());
    Data->SetArrayField(TEXT("still_referenced_list"), ReferencedList);
    Data->SetNumberField(TEXT("force_fixed_up"), FixedUpCount);
    if (ReferencedRedirectors.Num() > 0 && !bForceFixup)
    {
        Data->SetStringField(TEXT("note"), TEXT("Some redirectors still have external referencers and were left in place. Migrate/rewrite those referencers, or re-run with force_fixup=true (unsafe for World/Blueprint redirectors)."));
    }

    UE_LOG(LogSmithUE, Log, TEXT("fixup_redirectors: %s (found=%d, deleted=%d, still_referenced=%d, force_fixed=%d)"),
        *FolderPath, RedirectorAssets.Num(), DeletedCount, ReferencedRedirectors.Num(), FixedUpCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: resolve_redirectors
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleResolveRedirectors(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("folder_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString FolderPath;
    Params->TryGetStringField(TEXT("folder_path"), FolderPath);
    while (FolderPath.EndsWith(TEXT("/"))) { FolderPath.LeftChopInline(1); }

    int32 MaxResolve = MAX_int32;
    { double Tmp; if (Params->TryGetNumberField(TEXT("max_resolve"), Tmp)) { MaxResolve = FMath::Max(1, (int32)Tmp); } }

    bool bSave = true; Params->TryGetBoolField(TEXT("save"), bSave);

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());

    TArray<FAssetData> RedirectorAssets;
    AssetRegistryModule.Get().GetAssets(Filter, RedirectorAssets);

    int32 Resolved = 0, NoDestination = 0, LoadFailed = 0;
    TArray<TSharedPtr<FJsonValue>> ResolvedList;

    for (const FAssetData& Asset : RedirectorAssets)
    {
        if (Resolved >= MaxResolve)
        {
            break;
        }

        UObjectRedirector* Redirector = Cast<UObjectRedirector>(Asset.GetAsset());
        if (!Redirector)
        {
            ++LoadFailed;
            continue;
        }

        // Follow the redirector chain to a final, valid, non-redirector target.
        // A dangling or chained DestinationObject passed to ConsolidateObjects
        // causes an access-violation deep inside the engine.
        UObject* Destination = Redirector->DestinationObject;
        int32 ChainGuard = 0;
        while (UObjectRedirector* DestRedir = Cast<UObjectRedirector>(Destination))
        {
            if (++ChainGuard > 16 || DestRedir == Redirector)
            {
                Destination = nullptr;
                break;
            }
            Destination = DestRedir->DestinationObject;
        }
        if (!IsValid(Destination) || Destination == Redirector)
        {
            ++NoDestination;
            continue;
        }

        // Keep both objects alive across ConsolidateObjects (it runs a GC internally).
        TStrongObjectPtr<UObject> KeepDest(Destination);
        TStrongObjectPtr<UObjectRedirector> KeepRedir(Redirector);

        // ConsolidateObjects rewrites every referencer (hard + soft references)
        // of the redirector to point at Destination, then deletes the redirector.
        // This is a different engine path than AssetTools::FixupReferencers and
        // does not hit the Optional.h assertion that crashes on this project.
        TArray<UObject*> ToMerge;
        ToMerge.Add(Redirector);
        ObjectTools::ConsolidateObjects(Destination, ToMerge, /*bShowDeleteConfirmation*/ false);

        ++Resolved;
        if (ResolvedList.Num() < 100)
        {
            ResolvedList.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
        }
    }

    int32 SavedCount = 0;
    if (bSave && Resolved > 0)
    {
        TArray<UPackage*> DirtyPackages;
        FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
        FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
        SavedCount = DirtyPackages.Num();
        FEditorFileUtils::SaveDirtyPackages(false, true, true);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("folder_path"), FolderPath);
    Data->SetNumberField(TEXT("redirectors_found"), RedirectorAssets.Num());
    Data->SetNumberField(TEXT("resolved"), Resolved);
    Data->SetNumberField(TEXT("no_destination"), NoDestination);
    Data->SetNumberField(TEXT("load_failed"), LoadFailed);
    Data->SetNumberField(TEXT("saved_dirty_packages"), SavedCount);
    Data->SetArrayField(TEXT("resolved_sample"), ResolvedList);

    UE_LOG(LogSmithUE, Log, TEXT("resolve_redirectors: %s (found=%d, resolved=%d, no_dest=%d, load_failed=%d)"),
        *FolderPath, RedirectorAssets.Num(), Resolved, NoDestination, LoadFailed);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: consolidate_assets
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleConsolidateAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_to_keep"), TEXT("assets_to_merge")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString KeepPath;
    Params->TryGetStringField(TEXT("asset_to_keep"), KeepPath);

    const TArray<TSharedPtr<FJsonValue>>* MergeArray = nullptr;
    if (!Params->TryGetArrayField(TEXT("assets_to_merge"), MergeArray) || !MergeArray || MergeArray->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("assets_to_merge must be a non-empty array of asset paths"));
    }

    UObject* KeepAsset = UEditorAssetLibrary::LoadAsset(KeepPath);
    if (!KeepAsset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("asset_to_keep not found or failed to load: %s"), *KeepPath));
    }

    TArray<UObject*> ObjectsToConsolidate;
    for (const TSharedPtr<FJsonValue>& Value : *MergeArray)
    {
        const FString MergePath = Value->AsString();
        UObject* MergeAsset = UEditorAssetLibrary::LoadAsset(MergePath);
        if (!MergeAsset)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Asset to merge not found or failed to load: %s"), *MergePath));
        }
        if (MergeAsset == KeepAsset)
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("assets_to_merge must not contain asset_to_keep"));
        }
        ObjectsToConsolidate.Add(MergeAsset);
    }

    ObjectTools::FConsolidationResults Results = ObjectTools::ConsolidateObjects(KeepAsset, ObjectsToConsolidate, /*bShowDeleteConfirmation*/ false);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_to_keep"), KeepPath);
    Data->SetNumberField(TEXT("requested_merges"), ObjectsToConsolidate.Num());
    Data->SetNumberField(TEXT("failed_consolidations"), Results.FailedConsolidationObjs.Num());
    Data->SetNumberField(TEXT("invalid_consolidations"), Results.InvalidConsolidationObjs.Num());

    TArray<TSharedPtr<FJsonValue>> FailedArray;
    for (const TWeakObjectPtr<UObject>& Failed : Results.FailedConsolidationObjs)
    {
        if (Failed.IsValid())
        {
            FailedArray.Add(MakeShared<FJsonValueString>(Failed->GetPathName()));
        }
    }
    Data->SetArrayField(TEXT("failed"), FailedArray);

    UE_LOG(LogSmithUE, Log, TEXT("consolidate_assets: keep=%s merged=%d failed=%d"),
        *KeepPath, ObjectsToConsolidate.Num(), Results.FailedConsolidationObjs.Num());
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
// Command: set_asset_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleSetAssetProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path"), TEXT("property_path"), TEXT("value")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);
    FString PropertyPath;
    Params->TryGetStringField(TEXT("property_path"), PropertyPath);
    const TSharedPtr<FJsonValue> ValueJson = Params->TryGetField(TEXT("value"));
    const FString TextValue = AssetJsonValueToImportText(ValueJson);
    const bool bSave = !Params->HasField(TEXT("save")) || Params->GetBoolField(TEXT("save"));

    if (AssetPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("asset_path cannot be empty"));
    }
    if (PropertyPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("property_path cannot be empty"));
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
    }

    FAssetResolvedPropertyPath Resolved;
    if (!ResolveAssetObjectPropertyPath(Asset, PropertyPath, Resolved, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    const FString Before = ExportAssetPropertyValue(Resolved.LeafProperty, Resolved.LeafValuePtr, Asset);
    Asset->Modify();
    if (!ImportAssetPropertyValueWithNotify(Asset, Resolved, TextValue, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }
    const FString After = ExportAssetPropertyValue(Resolved.LeafProperty, Resolved.LeafValuePtr, Asset);
    const bool bChanged = Before != After;
    Asset->MarkPackageDirty();

    bool bSaved = false;
    if (bSave)
    {
        bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, false);
        if (!bSaved)
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Property was set but failed to save asset: %s"), *AssetPath));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("property_path"), PropertyPath);
    Data->SetStringField(TEXT("before"), Before);
    Data->SetStringField(TEXT("after"), After);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetBoolField(TEXT("saved"), bSaved);

    UE_LOG(LogSmithUE, Log, TEXT("set_asset_property: %s.%s %s -> %s"), *AssetPath, *PropertyPath, *Before, *After);
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

// ---------------------------------------------------------------------------
// Command: get_content_browser_selection
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleGetContentBrowserSelection(const TSharedPtr<FJsonObject>& Params)
{
    FContentBrowserModule& ContentBrowserModule =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

    // Folders: merge source-tree selection and asset-view folder selection, de-duplicated.
    TArray<FString> PathViewFolders;
    ContentBrowser.GetSelectedPathViewFolders(PathViewFolders);

    TArray<FString> AssetViewFolders;
    ContentBrowser.GetSelectedFolders(AssetViewFolders);

    TArray<FString> Folders;
    for (const FString& F : PathViewFolders) { Folders.AddUnique(F); }
    for (const FString& F : AssetViewFolders) { Folders.AddUnique(F); }

    TArray<TSharedPtr<FJsonValue>> FoldersArray;          // normalized real package paths
    TArray<TSharedPtr<FJsonValue>> FoldersVirtualArray;   // original /All/... virtual paths (debug)
    FoldersArray.Reserve(Folders.Num());
    FoldersVirtualArray.Reserve(Folders.Num());
    for (const FString& F : Folders)
    {
        FString Real;
        FSmithUECommonUtils::NormalizeContentBrowserPath(F, Real);
        FoldersArray.Add(MakeShared<FJsonValueString>(Real));
        FoldersVirtualArray.Add(MakeShared<FJsonValueString>(F));
    }

    // Selected assets in the asset view.
    TArray<FAssetData> SelectedAssets;
    ContentBrowser.GetSelectedAssets(SelectedAssets);

    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    AssetsArray.Reserve(SelectedAssets.Num());
    for (const FAssetData& AssetData : SelectedAssets)
    {
        AssetsArray.Add(MakeShared<FJsonValueObject>(AssetDataToJson(AssetData, false)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("selected_folders"), FoldersArray);
    Data->SetArrayField(TEXT("selected_folders_virtual"), FoldersVirtualArray);
    Data->SetArrayField(TEXT("selected_assets"), AssetsArray);
    Data->SetNumberField(TEXT("folder_count"), Folders.Num());
    Data->SetNumberField(TEXT("asset_count"), SelectedAssets.Num());

    UE_LOG(LogSmithUE, Log, TEXT("get_content_browser_selection: %d folders, %d assets"),
        Folders.Num(), SelectedAssets.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: sync_content_browser
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAssetCommands::HandleSyncContentBrowser(const TSharedPtr<FJsonObject>& Params)
{
    FString FolderPath;
    Params->TryGetStringField(TEXT("folder_path"), FolderPath);

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    if (FolderPath.IsEmpty() && AssetPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            TEXT("Provide either 'folder_path' or 'asset_path' to navigate to"));
    }

    FContentBrowserModule& ContentBrowserModule =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

    if (!AssetPath.IsEmpty())
    {
        IAssetRegistry& AssetRegistry = GetAssetRegistry();

        // Accept both the full object path (/Game/Foo.Foo) and the bare
        // package path (/Game/Foo) that list_assets / users commonly use.
        FString ResolvedPath = AssetPath;
        FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ResolvedPath));
        if (!AssetData.IsValid() && !AssetPath.Contains(TEXT(".")))
        {
            FString AssetName;
            if (AssetPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd)
                && !AssetName.IsEmpty())
            {
                ResolvedPath = AssetPath + TEXT(".") + AssetName;
                AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ResolvedPath));
            }
        }

        if (!AssetData.IsValid())
        {
            return FSmithUECommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
        }

        TArray<FAssetData> AssetsToSync;
        AssetsToSync.Add(AssetData);
        ContentBrowser.SyncBrowserToAssets(AssetsToSync, /*bAllowLockedBrowsers*/ false, /*bFocusContentBrowser*/ true);

        Data->SetStringField(TEXT("synced_to"), ResolvedPath);
        Data->SetStringField(TEXT("kind"), TEXT("asset"));
    }
    else
    {
        TArray<FString> FoldersToSync;
        FoldersToSync.Add(FolderPath);
        ContentBrowser.SyncBrowserToFolders(FoldersToSync, /*bAllowLockedBrowsers*/ false, /*bFocusContentBrowser*/ true);

        Data->SetStringField(TEXT("synced_to"), FolderPath);
        Data->SetStringField(TEXT("kind"), TEXT("folder"));
    }

    Data->SetBoolField(TEXT("synced"), true);

    UE_LOG(LogSmithUE, Log, TEXT("sync_content_browser: navigated to %s"),
        AssetPath.IsEmpty() ? *FolderPath : *AssetPath);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

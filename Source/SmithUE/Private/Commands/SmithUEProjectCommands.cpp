// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEProjectCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/ConfigCacheIni.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEProjectCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // get_project_info
    Registry.Register(
        FSmithUEToolSchema(TEXT("get_project_info"), TEXT("Project"),
            TEXT("Returns basic project and engine information (name, version, directories).")),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleGetProjectInfo(Params); });

    // list_plugins
    Registry.Register(
        FSmithUEToolSchema(TEXT("list_plugins"), TEXT("Project"),
            TEXT("Lists all discovered plugins with name, version, enabled status, description, and category."),
            {
                FSmithUEToolParam(TEXT("enabled_only"), TEXT("boolean"),
                    TEXT("If true, return only enabled plugins. Default: false."), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleListPlugins(Params); });

    // create_folder
    Registry.Register(
        FSmithUEToolSchema(TEXT("create_folder"), TEXT("Project"),
            TEXT("Creates a content browser folder (e.g. /Game/MyFolder/SubFolder)."),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"),
                    TEXT("Content path starting with /Game/, e.g. /Game/MyFolder/SubFolder"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleCreateFolder(Params); });

    // get_source_files
    Registry.Register(
        FSmithUEToolSchema(TEXT("get_source_files"), TEXT("Project"),
            TEXT("Lists source files recursively under a given path, filtered by extension."),
            {
                FSmithUEToolParam(TEXT("path"), TEXT("string"),
                    TEXT("Filesystem path to search. Defaults to project Source directory."), false),
                FSmithUEToolParam(TEXT("extensions"), TEXT("array"),
                    TEXT("File extensions to include, e.g. [\".h\",\".cpp\"]. Defaults to both."), false, TEXT(""), TEXT("string"))
            }),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleGetSourceFiles(Params); });
}

// ---------------------------------------------------------------------------
// Command 1: get_project_info
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEProjectCommands::HandleGetProjectInfo(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

    Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Data->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    Data->SetStringField(TEXT("content_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
    Data->SetStringField(TEXT("config_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()));
    Data->SetStringField(TEXT("engine_dir"), FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));

#if UE_BUILD_DEBUG
    Data->SetStringField(TEXT("build_config"), TEXT("Debug"));
#elif UE_BUILD_DEVELOPMENT
    Data->SetStringField(TEXT("build_config"), TEXT("Development"));
#elif UE_BUILD_SHIPPING
    Data->SetStringField(TEXT("build_config"), TEXT("Shipping"));
#else
    Data->SetStringField(TEXT("build_config"), TEXT("Unknown"));
#endif

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 2: list_plugins
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEProjectCommands::HandleListPlugins(const TSharedPtr<FJsonObject>& Params)
{
    bool bEnabledOnly = false;
    if (Params.IsValid() && Params->HasField(TEXT("enabled_only")))
    {
        bEnabledOnly = Params->GetBoolField(TEXT("enabled_only"));
    }

    TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetDiscoveredPlugins();

    TArray<TSharedPtr<FJsonValue>> PluginsArray;
    for (const TSharedRef<IPlugin>& Plugin : Plugins)
    {
        const bool bEnabled = Plugin->IsEnabled();
        if (bEnabledOnly && !bEnabled)
        {
            continue;
        }

        const FPluginDescriptor& Desc = Plugin->GetDescriptor();

        TSharedPtr<FJsonObject> PluginObj = MakeShared<FJsonObject>();
        PluginObj->SetStringField(TEXT("name"), Plugin->GetName());
        PluginObj->SetBoolField(TEXT("enabled"), bEnabled);
        PluginObj->SetStringField(TEXT("version"), FString::Printf(TEXT("%d"), Desc.Version));
        PluginObj->SetStringField(TEXT("version_name"), Desc.VersionName);
        PluginObj->SetStringField(TEXT("description"), Desc.Description);
        PluginObj->SetStringField(TEXT("category"), Desc.Category);
        PluginObj->SetStringField(TEXT("friendly_name"), Desc.FriendlyName);

        PluginsArray.Add(MakeShared<FJsonValueObject>(PluginObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("plugins"), PluginsArray);
    Data->SetNumberField(TEXT("count"), PluginsArray.Num());

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 3: get_project_settings
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEProjectCommands::HandleGetProjectSettings(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing parameters"));
    }

    FString Section;
    if (!Params->TryGetStringField(TEXT("section"), Section) || Section.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'section'"));
    }

    FString Key;
    if (!Params->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'key'"));
    }

    // Try reading from multiple config files in priority order
    FString Value;
    bool bFound = false;

    const FString ConfigFiles[] = { GGameIni, GEditorIni, GEngineIni, GEditorPerProjectIni };
    for (const FString& ConfigFile : ConfigFiles)
    {
        if (!ConfigFile.IsEmpty() && GConfig->GetString(*Section, *Key, Value, ConfigFile))
        {
            bFound = true;
            break;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("section"), Section);
    Data->SetStringField(TEXT("key"), Key);

    if (bFound)
    {
        Data->SetStringField(TEXT("value"), Value);
    }
    else
    {
        // Return null value rather than crashing
        Data->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
        UE_LOG(LogSmithUE, Warning, TEXT("get_project_settings: key '%s' not found in section '%s'"), *Key, *Section);
    }

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 4: create_folder
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEProjectCommands::HandleCreateFolder(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing parameters"));
    }

    FString FolderPath;
    if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath) || FolderPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'folder_path'"));
    }

    // Attempt content-browser-level folder creation first (handles /Game/ paths)
    if (FolderPath.StartsWith(TEXT("/Game")) || FolderPath.StartsWith(TEXT("/Engine")))
    {
        const bool bCreated = UEditorAssetLibrary::MakeDirectory(FolderPath);
        if (bCreated)
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("folder_path"), FolderPath);
            Data->SetBoolField(TEXT("created"), true);
            return FSmithUECommonUtils::CreateSuccessResponse(Data);
        }
        // MakeDirectory returns false if already exists too — check filesystem
        // Map /Game/ → actual content dir
        FString FilesystemPath = FolderPath;
        FilesystemPath.RemoveFromStart(TEXT("/Game"));
        FilesystemPath = FPaths::ProjectContentDir() / FilesystemPath;
        FPaths::NormalizeDirectoryName(FilesystemPath);

        if (FPaths::DirectoryExists(FilesystemPath))
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("folder_path"), FolderPath);
            Data->SetStringField(TEXT("filesystem_path"), FilesystemPath);
            Data->SetBoolField(TEXT("created"), false);
            Data->SetStringField(TEXT("note"), TEXT("Folder already exists"));
            return FSmithUECommonUtils::CreateSuccessResponse(Data);
        }

        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create folder: %s"), *FolderPath));
    }

    // Fallback: treat as raw filesystem path
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const bool bCreated = PlatformFile.CreateDirectoryTree(*FolderPath);
    if (bCreated || PlatformFile.DirectoryExists(*FolderPath))
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("folder_path"), FolderPath);
        Data->SetBoolField(TEXT("created"), bCreated);
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    return FSmithUECommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Failed to create directory: %s"), *FolderPath));
}

// ---------------------------------------------------------------------------
// Command 5: get_source_files
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEProjectCommands::HandleGetSourceFiles(const TSharedPtr<FJsonObject>& Params)
{
    // Determine base path
    FString BasePath;
    if (Params.IsValid() && Params->HasField(TEXT("path")))
    {
        Params->TryGetStringField(TEXT("path"), BasePath);
    }
    if (BasePath.IsEmpty())
    {
        BasePath = FPaths::ProjectDir() / TEXT("Source");
    }
    FPaths::NormalizeDirectoryName(BasePath);

    // Determine extensions filter
    TArray<FString> Extensions;
    if (Params.IsValid() && Params->HasField(TEXT("extensions")))
    {
        const TArray<TSharedPtr<FJsonValue>>* ExtArray = nullptr;
        if (Params->TryGetArrayField(TEXT("extensions"), ExtArray) && ExtArray)
        {
            for (const TSharedPtr<FJsonValue>& Val : *ExtArray)
            {
                FString Ext;
                if (Val.IsValid() && Val->TryGetString(Ext) && !Ext.IsEmpty())
                {
                    Extensions.Add(Ext.ToLower());
                }
            }
        }
    }
    if (Extensions.IsEmpty())
    {
        Extensions.Add(TEXT(".h"));
        Extensions.Add(TEXT(".cpp"));
    }

    // Recursively find all files
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> AllFiles;
    FileManager.FindFilesRecursive(AllFiles, *BasePath, TEXT("*"), true, false);

    // Filter by extension and build relative paths
    const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    TArray<TSharedPtr<FJsonValue>> FilesArray;

    for (const FString& FilePath : AllFiles)
    {
        const FString Ext = FPaths::GetExtension(FilePath, /*bIncludeDot=*/true).ToLower();
        if (!Extensions.Contains(Ext))
        {
            continue;
        }

        FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);
        FString RelPath = FullPath;
        FPaths::MakePathRelativeTo(RelPath, *ProjectRoot);

        FilesArray.Add(MakeShared<FJsonValueString>(RelPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("base_path"), BasePath);
    Data->SetArrayField(TEXT("files"), FilesArray);
    Data->SetNumberField(TEXT("count"), FilesArray.Num());

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

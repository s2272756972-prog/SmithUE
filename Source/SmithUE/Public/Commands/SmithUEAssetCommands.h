#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEAssetCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleListAssets(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleFindAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleRenameAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDuplicateAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleMoveAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAssetEditor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSaveAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSaveAllDirty(const TSharedPtr<FJsonObject>& Params);
};

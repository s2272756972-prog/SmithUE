#pragma once
#include "CoreMinimal.h"

class FUEAgentToolRegistry;

class FUEAgentAssetCommands
{
public:
    static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleListAssets(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleFindAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleRenameAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDuplicateAsset(const TSharedPtr<FJsonObject>& Params);
};

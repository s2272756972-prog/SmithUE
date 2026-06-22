// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FSmithUEToolRegistry;

class FSmithUEAssetAuditCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

    static TSharedPtr<FJsonObject> HandleGetAssetProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleScanAssets(const TSharedPtr<FJsonObject>& Params);
};

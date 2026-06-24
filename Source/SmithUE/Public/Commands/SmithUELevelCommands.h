// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUELevelCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleLevelNew(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelOpen(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelSave(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelGetInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelCreateLandscape(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelSetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelGetLandscapeInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelAddFoliageType(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelPaintFoliage(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelEraseFoliage(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelGetFoliageStats(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLevelAddBasicEnv(const TSharedPtr<FJsonObject>& Params);
};

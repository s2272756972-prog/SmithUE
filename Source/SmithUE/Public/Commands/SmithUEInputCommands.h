// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEInputCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleInputCreateAction(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleInputCreateMappingContext(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleInputFindActions(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleInputReadMappingContext(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleInputEditMappingContext(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleInputDeleteAsset(const TSharedPtr<FJsonObject>& Params);
};

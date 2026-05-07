// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FUEAgentToolRegistry;

class FUEAgentViewportCommands
{
public:
    static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleSetViewportCamera(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleFocusOnActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetViewportMode(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetViewportInfoDetailed(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSelectActors(const TSharedPtr<FJsonObject>& Params);
};

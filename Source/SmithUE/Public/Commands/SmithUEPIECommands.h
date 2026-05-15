// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEPIECommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandlePieTeleportActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieSpawnActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieDestroyActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieGetProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieSetProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieGetGameState(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieListActors(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieConsoleCommand(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieStart(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieStop(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePieIsActive(const TSharedPtr<FJsonObject>& Params);
};

// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEPCGCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleCreatePcgGraph(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleFindPcgGraphs(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSpawnPcgVolume(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandlePcgGenerate(const TSharedPtr<FJsonObject>& Params);
};

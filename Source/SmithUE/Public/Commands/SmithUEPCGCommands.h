// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * PCG (Procedural Content Generation) tools — create/inspect PCG Graph assets and
 * drive PCG Volume actors in the editor world. Requires the engine "PCG" plugin.
 */
class FSmithUEPCGCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleCreatePcgGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadPcgGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleFindPcgGraphs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleSpawnPcgVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandlePcgGenerate(const TSharedPtr<FJsonObject>& Params);
};

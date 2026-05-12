// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUESourceAnalysisCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleAnalyzeModule(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleAnalyzeDependencies(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleAnalyzeBlueprints(const TSharedPtr<FJsonObject>& Params);
};

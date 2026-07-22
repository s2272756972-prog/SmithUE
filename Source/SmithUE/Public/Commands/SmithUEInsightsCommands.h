// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * Unreal Insights trace tools: capture a .utrace (FTraceAuxiliary) and analyze it
 * offline (TraceServices IAnalysisService) into a perf summary — frame-time
 * distribution / hitches + stat counters. Requires the TraceServices module.
 */
class FSmithUEInsightsCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleStartTrace(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleStopTrace(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleTraceStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleAnalyzeTrace(const TSharedPtr<FJsonObject>& Params);
};

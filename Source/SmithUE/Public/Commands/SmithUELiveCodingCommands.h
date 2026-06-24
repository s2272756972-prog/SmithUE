// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

class FSmithUEToolRegistry;

class FSmithUELiveCodingCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

    // Always-compiled public seams (no engine types)
    static TSharedPtr<FJsonObject> BuildUnsupportedStatusResponse();
    static TSharedPtr<FJsonObject> BuildUnsupportedCompileResponse();
    static bool ReadBoolParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, bool Default);

#if WITH_LIVE_CODING
    static TSharedPtr<FJsonObject> MapCompileResult(ELiveCodingCompileResult Result, class ILiveCodingModule* LC);
#endif

private:
    static TSharedPtr<FJsonObject> HandleLiveCodingStatus(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleLiveCodingCompile(const TSharedPtr<FJsonObject>& Params);
};

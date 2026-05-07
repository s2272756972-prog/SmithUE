// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "Dom/JsonObject.h"

class FUEAgentToolRegistry;

struct FUEAgentScreenshotCommands
{
    static void RegisterTools(FUEAgentToolRegistry& Registry);
private:
    static TSharedPtr<FJsonObject> HandleTakeViewportScreenshot(const TSharedPtr<FJsonObject>& Params);
};

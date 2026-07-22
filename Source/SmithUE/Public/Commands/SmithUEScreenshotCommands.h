// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "Dom/JsonObject.h"

class FSmithUEToolRegistry;

struct FSmithUEScreenshotCommands
{
    static void RegisterTools(FSmithUEToolRegistry& Registry);
private:
    static TSharedPtr<FJsonObject> HandleTakeViewportScreenshot(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleTakeBlueprintPreviewScreenshot(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCaptureAssetThumbnail(const TSharedPtr<FJsonObject>& Params);
};

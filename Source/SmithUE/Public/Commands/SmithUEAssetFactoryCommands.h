// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * Asset factory commands - create and read common standalone assets:
 * Curves (Float / LinearColor / Vector), Curve Linear Color Atlas,
 * Texture Render Targets, Data Assets, and Physical Materials.
 */
class FSmithUEAssetFactoryCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleCreateCurve(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleReadCurve(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCreateCurveAtlas(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleReadCurveAtlas(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCreateRenderTarget(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleReadRenderTarget(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCreateDataAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCreatePhysicalMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleReadPhysicalMaterial(const TSharedPtr<FJsonObject>& Params);
};

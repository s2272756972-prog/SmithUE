// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEEnvironmentCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleEnvSetPostProcess(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvSetFog(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvSetSkyAtmosphere(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvSetLight(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvSetPhysics(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvSetCollision(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvGetPhysicsInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvCreateSpline(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvAddSplinePoint(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvSetSplinePoint(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleEnvGetSplineInfo(const TSharedPtr<FJsonObject>& Params);
};

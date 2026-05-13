// Copyright 2026, 123dx-svg. MIT License.

#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEMaterialFunctionCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleCreateMaterialFunction(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetMaterialFunctionInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAddMfExpression(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleConnectMfPins(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetMfExpressionProperty(const TSharedPtr<FJsonObject>& Params);
};

#pragma once
#include "CoreMinimal.h"

class FUEAgentToolRegistry;

class FUEAgentMaterialCommands
{
public:
    static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleConnectMaterialPins(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCompileMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetMaterialProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetExpressionProperty(const TSharedPtr<FJsonObject>& Params);
};

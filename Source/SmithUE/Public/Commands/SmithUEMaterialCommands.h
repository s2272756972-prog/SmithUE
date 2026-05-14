#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEMaterialCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleConnectMaterialPins(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCompileMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetMaterialProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetExpressionProperty(const TSharedPtr<FJsonObject>& Params);

    // Material Parameter Collection commands
    static TSharedPtr<FJsonObject> HandleCreateMPC(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAddMPCScalar(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAddMPCVector(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetMPCValue(const TSharedPtr<FJsonObject>& Params);

    // Material Instance commands
    static TSharedPtr<FJsonObject> HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetMIScalar(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetMIVector(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetMIInfo(const TSharedPtr<FJsonObject>& Params);
};

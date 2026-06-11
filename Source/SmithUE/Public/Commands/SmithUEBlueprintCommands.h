#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;
class FJsonObject;

class FSmithUEBlueprintCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleBpGetSummary(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpGetClassMembers(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpDescribeGraph(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpCompileCode(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpBatchOp(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpValidateCode(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpSearch(const TSharedPtr<FJsonObject>& Params);
};

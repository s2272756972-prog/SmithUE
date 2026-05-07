#pragma once
#include "CoreMinimal.h"

class FUEAgentToolRegistry;
class FJsonObject;

class FUEAgentBlueprintCommands
{
public:
    static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleBpGetSummary(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpDescribeGraph(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpCompileCode(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpBatchOp(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpValidateCode(const TSharedPtr<FJsonObject>& Params);
};

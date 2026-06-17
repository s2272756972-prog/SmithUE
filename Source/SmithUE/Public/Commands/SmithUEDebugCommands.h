// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEDebugCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleBpGetCompileErrors(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpRefreshAllNodes(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpFindUnconnectedPins(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpFixBrokenReferences(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAssetGetReferences(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAssetGetReferencers(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAssetFindOrphans(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAssetGetDependencyTree(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAssetValidate(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleMapCheckErrors(const TSharedPtr<FJsonObject>& Params);

    // Breakpoint commands
    static TSharedPtr<FJsonObject> HandleBpSetBreakpoint(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpClearBreakpoint(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleBpListBreakpoints(const TSharedPtr<FJsonObject>& Params);
};

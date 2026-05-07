// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"

class FUEAgentToolRegistry;

class FUEAgentInteractionCommands
{
public:
    static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleExecuteEditorCommand(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleExecuteConsoleCommand(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleListEditorCommands(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleUndo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleRedo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSimulateKey(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleListKeyBindings(const TSharedPtr<FJsonObject>& Params);
};

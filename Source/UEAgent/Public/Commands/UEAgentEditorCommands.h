#pragma once
#include "CoreMinimal.h"

class FUEAgentToolRegistry;

class FUEAgentEditorCommands
{
public:
    static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetAllActors(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetViewportInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAddPostProcessMaterial(const TSharedPtr<FJsonObject>& Params);
};

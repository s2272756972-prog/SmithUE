// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUENiagaraCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleCreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetNiagaraSystemInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraAddEmitter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraAddEmitterFromTemplate(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraSetEmitterProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraCompile(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraAddRenderer(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraSetRendererProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraDeleteRenderer(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraAddModule(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraSetModuleInput(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraAddUserParameter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleSpawnNiagaraActor(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraStaticSwitch(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraSearchAssets(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraDeleteEmitter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleNiagaraDeleteModule(const TSharedPtr<FJsonObject>& Params);
};

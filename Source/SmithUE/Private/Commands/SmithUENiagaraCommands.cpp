// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUENiagaraCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraGraph.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraTypes.h"
#include "NiagaraParameterStore.h"
#include "NiagaraCommon.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraWorldManager.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraEditorUtilities.h"
#include "Engine/World.h"
#include "Editor.h"

static FNiagaraEmitterHandle* FindEmitterHandle(
    const FString& SystemPath,
    const FString& EmitterName,
    UNiagaraSystem*& OutSystem,
    FString& OutError)
{
    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(SystemPath);
    OutSystem = Cast<UNiagaraSystem>(LoadedAsset);
    if (!OutSystem)
    {
        OutError = FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath);
        return nullptr;
    }

    for (FNiagaraEmitterHandle& Handle : OutSystem->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            return &Handle;
        }
    }

    OutError = FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *SystemPath);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleCreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, Path;
    if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: name"));
    if (!Params->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: path"));

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    // Locate the factory class via reflection (avoids direct DLL symbol dependency)
    UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"));
    if (!FactoryClass)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("NiagaraSystemFactoryNew class not found. Ensure Niagara plugin is enabled."));

    UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
    UObject* CreatedAsset = AssetTools.CreateAsset(Name, Path, UNiagaraSystem::StaticClass(), Factory);
    if (!CreatedAsset)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create Niagara system '%s' at '%s'"), *Name, *Path));

    FString FullPath = FString::Printf(TEXT("%s/%s"), *Path, *Name);
    UEditorAssetLibrary::SaveAsset(FullPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("asset_path"), FullPath);
    ResultJson->SetStringField(TEXT("name"), Name);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleGetNiagaraSystemInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));

    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(SystemPath);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadedAsset);
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    TArray<TSharedPtr<FJsonValue>> EmittersArray;
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
        EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
        EmitterObj->SetStringField(TEXT("id"), Handle.GetId().ToString());
        EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

        FVersionedNiagaraEmitterData* EmitterData = Handle.GetInstance().GetEmitterData();
        if (EmitterData)
        {
            // Renderers
            TArray<TSharedPtr<FJsonValue>> RenderersArray;
            const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
            for (int32 i = 0; i < Renderers.Num(); i++)
            {
                UNiagaraRendererProperties* Renderer = Renderers[i];
                TSharedPtr<FJsonObject> RendererObj = MakeShared<FJsonObject>();
                RendererObj->SetNumberField(TEXT("index"), i);
                RendererObj->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
                RendererObj->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
                if (UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer))
                {
                    RendererObj->SetStringField(TEXT("material"), SpriteRenderer->Material ? SpriteRenderer->Material->GetPathName() : TEXT("null"));
                }
                RenderersArray.Add(MakeShared<FJsonValueObject>(RendererObj));
            }
            EmitterObj->SetArrayField(TEXT("renderers"), RenderersArray);

            // Module stacks - iterate graph nodes to find function calls per stage
            auto CollectModulesForScript = [&](UNiagaraScript* Script, const FString& StageName) -> TArray<TSharedPtr<FJsonValue>>
            {
                TArray<TSharedPtr<FJsonValue>> ModulesArray;
                if (!Script) return ModulesArray;
                UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
                if (!Source || !Source->NodeGraph) return ModulesArray;
                for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
                {
                    UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
                    if (FuncNode)
                    {
                        TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
                        ModObj->SetStringField(TEXT("name"), FuncNode->GetFunctionName());
                        ModObj->SetStringField(TEXT("node_class"), FuncNode->GetClass()->GetName());
                        if (FuncNode->FunctionScript)
                        {
                            ModObj->SetStringField(TEXT("script_path"), FuncNode->FunctionScript->GetPathName());
                        }
                        ModulesArray.Add(MakeShared<FJsonValueObject>(ModObj));
                    }
                }
                return ModulesArray;
            };

            // All stages share the same graph, so collect all function call nodes once
            UNiagaraScript* AnyScript = EmitterData->SpawnScriptProps.Script;
            if (!AnyScript) AnyScript = EmitterData->UpdateScriptProps.Script;
            TArray<TSharedPtr<FJsonValue>> AllModules;
            if (AnyScript)
            {
                UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(AnyScript->GetLatestSource());
                if (Source && Source->NodeGraph)
                {
                    for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
                    {
                        UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
                        if (FuncNode)
                        {
                            TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
                            ModObj->SetStringField(TEXT("name"), FuncNode->GetFunctionName());
                            if (FuncNode->FunctionScript)
                                ModObj->SetStringField(TEXT("script"), FuncNode->FunctionScript->GetPathName());
                            AllModules.Add(MakeShared<FJsonValueObject>(ModObj));
                        }
                        // Also report output nodes with their usage
                        UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node);
                        if (OutNode)
                        {
                            TSharedPtr<FJsonObject> OutObj = MakeShared<FJsonObject>();
                            OutObj->SetStringField(TEXT("name"), TEXT("OutputNode"));
                            OutObj->SetNumberField(TEXT("usage"), (int32)OutNode->GetUsage());
                            // Count connected inputs
                            int32 ConnectedInputs = 0;
                            for (UEdGraphPin* Pin : OutNode->Pins)
                            {
                                if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0)
                                    ConnectedInputs++;
                            }
                            OutObj->SetNumberField(TEXT("connected_inputs"), ConnectedInputs);
                            AllModules.Add(MakeShared<FJsonValueObject>(OutObj));
                        }
                    }
                }
            }
            EmitterObj->SetArrayField(TEXT("graph_nodes"), AllModules);

            // RI parameter details per script (names + values read from the parameter store)
            auto DumpRIParams = [](UNiagaraScript* S) -> TSharedPtr<FJsonObject>
            {
                TSharedPtr<FJsonObject> ScriptObj = MakeShared<FJsonObject>();
                if (!S)
                {
                    ScriptObj->SetNumberField(TEXT("count"), -1);
                    return ScriptObj;
                }
                FNiagaraParameterStore& RIStore = S->RapidIterationParameters;
                TArray<FNiagaraVariable> Vars;
                RIStore.GetParameters(Vars);
                ScriptObj->SetNumberField(TEXT("count"), Vars.Num());
                TArray<TSharedPtr<FJsonValue>> ParamsList;
                for (FNiagaraVariable& Var : Vars)
                {
                    TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
                    ParamObj->SetStringField(TEXT("name"), Var.GetName().ToString());
                    ParamObj->SetStringField(TEXT("type"), Var.GetType().GetName());
                    // Read value from parameter store (not from variable which may be empty)
                    int32 ParamOffset = RIStore.IndexOf(Var);
                    if (ParamOffset != INDEX_NONE)
                    {
                        const uint8* Data = RIStore.GetParameterData(Var);
                        if (Data)
                        {
                            if (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
                            {
                                float Val;
                                FMemory::Memcpy(&Val, Data, sizeof(float));
                                ParamObj->SetNumberField(TEXT("value"), Val);
                            }
                            else if (Var.GetType() == FNiagaraTypeDefinition::GetIntDef())
                            {
                                int32 Val;
                                FMemory::Memcpy(&Val, Data, sizeof(int32));
                                ParamObj->SetNumberField(TEXT("value"), Val);
                            }
                            else if (Var.GetType() == FNiagaraTypeDefinition::GetBoolDef())
                            {
                                FNiagaraBool Val;
                                FMemory::Memcpy(&Val, Data, sizeof(FNiagaraBool));
                                ParamObj->SetBoolField(TEXT("value"), Val.GetValue());
                            }
                            else if (Var.GetType() == FNiagaraTypeDefinition::GetVec3Def())
                            {
                                FVector3f Val;
                                FMemory::Memcpy(&Val, Data, sizeof(FVector3f));
                                ParamObj->SetStringField(TEXT("value"), FString::Printf(TEXT("(%f,%f,%f)"), Val.X, Val.Y, Val.Z));
                            }
                            else if (Var.GetType() == FNiagaraTypeDefinition::GetVec2Def())
                            {
                                FVector2f Val;
                                FMemory::Memcpy(&Val, Data, sizeof(FVector2f));
                                ParamObj->SetStringField(TEXT("value"), FString::Printf(TEXT("(%f,%f)"), Val.X, Val.Y));
                            }
                            else
                            {
                                ParamObj->SetStringField(TEXT("value"), TEXT("<complex>"));
                            }
                        }
                        else
                        {
                            ParamObj->SetStringField(TEXT("value"), TEXT("<null_data>"));
                        }
                    }
                    else
                    {
                        ParamObj->SetStringField(TEXT("value"), TEXT("<not_found>"));
                    }
                    ParamsList.Add(MakeShared<FJsonValueObject>(ParamObj));
                }
                ScriptObj->SetArrayField(TEXT("params"), ParamsList);
                return ScriptObj;
            };
            TSharedPtr<FJsonObject> RIObj = MakeShared<FJsonObject>();
            RIObj->SetObjectField(TEXT("particle_spawn"), DumpRIParams(EmitterData->SpawnScriptProps.Script));
            RIObj->SetObjectField(TEXT("particle_update"), DumpRIParams(EmitterData->UpdateScriptProps.Script));
            RIObj->SetObjectField(TEXT("emitter_spawn"), DumpRIParams(EmitterData->EmitterSpawnScriptProps.Script));
            RIObj->SetObjectField(TEXT("emitter_update"), DumpRIParams(EmitterData->EmitterUpdateScriptProps.Script));
            EmitterObj->SetObjectField(TEXT("ri_params"), RIObj);

            // Sim target
            EmitterObj->SetStringField(TEXT("sim_target"), EmitterData->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));
        }

        EmittersArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
    }

    // Collect exposed/user parameters
    TArray<TSharedPtr<FJsonValue>> ParamsArray;
    const FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();
    TArray<FNiagaraVariable> ParameterVars;
    ExposedParams.GetParameters(ParameterVars);
    for (const FNiagaraVariable& Var : ParameterVars)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
        ParamObj->SetStringField(TEXT("name"), Var.GetName().ToString());
        ParamObj->SetStringField(TEXT("type"), Var.GetType().GetName());
        ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
    }

    // System compile status
    bool bIsValid = System->IsValid();

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetBoolField(TEXT("is_valid"), bIsValid);
    ResultJson->SetArrayField(TEXT("emitters"), EmittersArray);
    ResultJson->SetArrayField(TEXT("user_parameters"), ParamsArray);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraAddEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));

    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(SystemPath);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadedAsset);
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    // Load the "Empty" template emitter which has proper graph structure, output nodes,
    // and basic modules (InitializeParticle, EmitterState, ParticleState) already wired up.
    UNiagaraEmitter* TemplateEmitter = Cast<UNiagaraEmitter>(
        StaticLoadObject(UNiagaraEmitter::StaticClass(), nullptr,
            TEXT("/Niagara/DefaultAssets/Templates/Emitters/Empty.Empty")));
    if (!TemplateEmitter)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to load template emitter '/Niagara/DefaultAssets/Templates/Emitters/Empty'"));

    FVersionedNiagaraEmitterData* TemplateData = TemplateEmitter->GetLatestEmitterData();
    if (!TemplateData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get template emitter data"));

    // Use FNiagaraEditorUtilities::AddEmitterToSystem which properly integrates the emitter
    // into the system's compilation model (RebuildEmitterNodes + SynchronizeOverviewGraph).
    // Unlike raw System->AddEmitterHandle(), this ensures the emitter gets bytecode.
    FGuid TemplateVersion = TemplateData->Version.VersionGuid;
    const FGuid NewEmitterHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateVersion);

    // Rename the emitter handle from the template name ("Empty") to the user-specified name
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetId() == NewEmitterHandleId)
        {
            Handle.SetName(FName(*EmitterName), *System);
            break;
        }
    }

    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("emitter_id"), NewEmitterHandleId.ToString());
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraAddEmitterFromTemplate(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, TemplatePath;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("template_path"), TemplatePath) || TemplatePath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: template_path"));

    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(SystemPath);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadedAsset);
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    // Load the specified template emitter which has proper graph structure, output nodes,
    // and basic modules already wired up.
    UNiagaraEmitter* TemplateEmitter = Cast<UNiagaraEmitter>(
        StaticLoadObject(UNiagaraEmitter::StaticClass(), nullptr, *TemplatePath));
    if (!TemplateEmitter)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load template emitter '%s'"), *TemplatePath));

    FVersionedNiagaraEmitterData* TemplateData = TemplateEmitter->GetLatestEmitterData();
    if (!TemplateData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get template emitter data"));

    FGuid TemplateVersion = TemplateData->Version.VersionGuid;
    const FGuid NewEmitterHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateVersion);

    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetId() == NewEmitterHandleId)
        {
            Handle.SetName(FName(*EmitterName), *System);
            break;
        }
    }

    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("emitter_id"), NewEmitterHandleId.ToString());
    ResultJson->SetStringField(TEXT("template_path"), TemplatePath);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraSetEmitterProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, Property, Value;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("property"), Property) || Property.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: property"));
    if (!Params->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: value"));

    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(SystemPath);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadedAsset);
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    // Find emitter handle by name
    FNiagaraEmitterHandle* FoundHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            FoundHandle = &Handle;
            break;
        }
    }
    if (!FoundHandle)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system '%s'"), *EmitterName, *SystemPath));

    if (Property.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
    {
        bool bEnabled = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
        FoundHandle->SetIsEnabled(bEnabled, *System, false);
    }
    else if (Property.Equals(TEXT("local_space"), ESearchCase::IgnoreCase))
    {
        FVersionedNiagaraEmitterData* EmitterData = FoundHandle->GetEmitterData();
        if (!EmitterData)
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));
        EmitterData->bLocalSpace = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
    }
    else
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported property '%s'. Supported: enabled, local_space"), *Property));
    }

    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("property"), Property);
    ResultJson->SetStringField(TEXT("value"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraCompile(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));

    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(SystemPath);
    UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadedAsset);
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    System->InvalidateCachedData();
    System->MarkPackageDirty();
    bool bCompileRequested = System->RequestCompile(true);
    if (bCompileRequested)
    {
        System->WaitForCompilationComplete(false, false);
    }

    // Collect compile status for each script
    TArray<TSharedPtr<FJsonValue>> ScriptStatusArray;
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        FVersionedNiagaraEmitterData* EmitterData = Handle.GetInstance().GetEmitterData();
        if (!EmitterData) continue;

        auto AddScriptStatus = [&](UNiagaraScript* Script, const FString& StageName)
        {
            if (!Script) return;
            TSharedPtr<FJsonObject> StatusObj = MakeShared<FJsonObject>();
            StatusObj->SetStringField(TEXT("emitter"), Handle.GetName().ToString());
            StatusObj->SetStringField(TEXT("stage"), StageName);
            StatusObj->SetBoolField(TEXT("has_bytecode"), Script->GetVMExecutableData().IsValid());
            TArray<FNiagaraVariable> RIVars;
            Script->RapidIterationParameters.GetParameters(RIVars);
            StatusObj->SetNumberField(TEXT("ri_param_count"), RIVars.Num());
            ScriptStatusArray.Add(MakeShared<FJsonValueObject>(StatusObj));
        };

        AddScriptStatus(EmitterData->SpawnScriptProps.Script, TEXT("particle_spawn"));
        AddScriptStatus(EmitterData->UpdateScriptProps.Script, TEXT("particle_update"));
        AddScriptStatus(EmitterData->EmitterSpawnScriptProps.Script, TEXT("emitter_spawn"));
        AddScriptStatus(EmitterData->EmitterUpdateScriptProps.Script, TEXT("emitter_update"));
    }

    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetBoolField(TEXT("compile_requested"), bCompileRequested);
    ResultJson->SetBoolField(TEXT("is_valid"), System->IsValid());
    ResultJson->SetArrayField(TEXT("script_status"), ScriptStatusArray);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraAddRenderer(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, RendererType;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("renderer_type"), RendererType) || RendererType.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: renderer_type"));

    UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    FNiagaraEmitterHandle* FoundHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            FoundHandle = &Handle;
            break;
        }
    }
    if (!FoundHandle)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

    FVersionedNiagaraEmitterData* EmitterData = FoundHandle->GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    UNiagaraRendererProperties* NewRenderer = nullptr;
    UObject* Outer = FoundHandle->GetInstance().Emitter;

    if (RendererType.Equals(TEXT("sprite"), ESearchCase::IgnoreCase))
    {
        NewRenderer = NewObject<UNiagaraSpriteRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    else if (RendererType.Equals(TEXT("mesh"), ESearchCase::IgnoreCase))
    {
        NewRenderer = NewObject<UNiagaraMeshRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    else if (RendererType.Equals(TEXT("ribbon"), ESearchCase::IgnoreCase))
    {
        NewRenderer = NewObject<UNiagaraRibbonRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    else
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported renderer_type '%s'. Use: sprite, mesh, ribbon"), *RendererType));
    }

    if (!NewRenderer)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create renderer"));

    UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Outer);
    FGuid EmitterVersion = FoundHandle->GetInstance().Version;
    Emitter->AddRenderer(NewRenderer, EmitterVersion);
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("renderer_type"), RendererType);
    ResultJson->SetNumberField(TEXT("renderer_index"), EmitterData->GetRenderers().Num() - 1);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraSetRendererProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, PropertyName, Value;
    int32 RendererIndex = 0;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: property_name"));
    if (!Params->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: value"));
    Params->TryGetNumberField(TEXT("renderer_index"), RendererIndex);

    UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    FNiagaraEmitterHandle* FoundHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            FoundHandle = &Handle;
            break;
        }
    }
    if (!FoundHandle)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

    FVersionedNiagaraEmitterData* EmitterData = FoundHandle->GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
    if (!Renderers.IsValidIndex(RendererIndex))
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Renderer index %d out of range (count: %d)"), RendererIndex, Renderers.Num()));

    UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];

    if (PropertyName.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
    {
        UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(Value));
        if (!Mat)
            return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material at '%s'"), *Value));

        if (UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer))
        {
            SpriteRenderer->Material = Mat;
        }
        else if (UNiagaraMeshRendererProperties* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(Renderer))
        {
            // MeshRenderer uses override materials
        }
        else if (UNiagaraRibbonRendererProperties* RibbonRenderer = Cast<UNiagaraRibbonRendererProperties>(Renderer))
        {
            RibbonRenderer->Material = Mat;
        }
    }
    else
    {
        FProperty* Prop = FindFProperty<FProperty>(Renderer->GetClass(), *PropertyName);
        if (!Prop)
            return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found on renderer"), *PropertyName));

        void* PropertyAddr = Prop->ContainerPtrToValuePtr<void>(Renderer);
        if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
        {
            BoolProp->SetPropertyValue(PropertyAddr, Value.Equals(TEXT("true"), ESearchCase::IgnoreCase));
        }
        else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
        {
            FloatProp->SetPropertyValue(PropertyAddr, FCString::Atof(*Value));
        }
        else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
        {
            IntProp->SetPropertyValue(PropertyAddr, FCString::Atoi(*Value));
        }
        else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
        {
            UEnum* EnumClass = EnumProp->GetEnum();
            int64 EnumValue = EnumClass->GetValueByNameString(Value);
            if (EnumValue == INDEX_NONE)
                EnumValue = FCString::Atoi64(*Value);
            EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropertyAddr, EnumValue);
        }
        else
        {
            Prop->ImportText(*Value, PropertyAddr, 0, Renderer);
        }
    }

    Renderer->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("property_name"), PropertyName);
    ResultJson->SetStringField(TEXT("value"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraAddModule(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, ModulePath, StackGroup;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("module_path"), ModulePath) || ModulePath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: module_path"));
    if (!Params->TryGetStringField(TEXT("stack_group"), StackGroup) || StackGroup.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: stack_group (spawn|update|emitter_spawn|emitter_update)"));

    UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    UNiagaraScript* ModuleScript = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(ModulePath));
    if (!ModuleScript)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Niagara module script at '%s'"), *ModulePath));

    FNiagaraEmitterHandle* FoundHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            FoundHandle = &Handle;
            break;
        }
    }
    if (!FoundHandle)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

    FVersionedNiagaraEmitterData* EmitterData = FoundHandle->GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    // Map stack_group to ENiagaraScriptUsage
    ENiagaraScriptUsage Usage;
    if (StackGroup.Equals(TEXT("spawn"), ESearchCase::IgnoreCase) || StackGroup.Equals(TEXT("particle_spawn"), ESearchCase::IgnoreCase))
        Usage = ENiagaraScriptUsage::ParticleSpawnScript;
    else if (StackGroup.Equals(TEXT("update"), ESearchCase::IgnoreCase) || StackGroup.Equals(TEXT("particle_update"), ESearchCase::IgnoreCase))
        Usage = ENiagaraScriptUsage::ParticleUpdateScript;
    else if (StackGroup.Equals(TEXT("emitter_spawn"), ESearchCase::IgnoreCase))
        Usage = ENiagaraScriptUsage::EmitterSpawnScript;
    else if (StackGroup.Equals(TEXT("emitter_update"), ESearchCase::IgnoreCase))
        Usage = ENiagaraScriptUsage::EmitterUpdateScript;
    else
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported stack_group '%s'. Use: spawn, update, emitter_spawn, emitter_update"), *StackGroup));

    // Get the emitter graph (shared across all stages)
    UNiagaraScript* AnyScript = EmitterData->SpawnScriptProps.Script ? EmitterData->SpawnScriptProps.Script : EmitterData->UpdateScriptProps.Script;
    if (!AnyScript)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No script found on emitter"));

    UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(AnyScript->GetLatestSource());
    if (!ScriptSource)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get script source"));

    UNiagaraGraph* Graph = ScriptSource->NodeGraph;
    if (!Graph)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter graph"));

    // Find the output node for the target stage by iterating graph nodes
    UNiagaraNodeOutput* OutputNode = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node);
        if (OutNode && OutNode->GetUsage() == Usage)
        {
            OutputNode = OutNode;
            break;
        }
    }
    if (!OutputNode)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Output node for stage '%s' not found in emitter graph"), *StackGroup));

    // Use the proper stack utility to add the module
    UNiagaraNodeFunctionCall* NewModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScript, *OutputNode);
    if (!NewModuleNode)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("AddScriptModuleToStack failed"));

    // Notify graph of changes so compilation picks up the new module
    Graph->NotifyGraphChanged();

    // Notify the system of structural changes (propagates to emitters)
    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("module_path"), ModulePath);
    ResultJson->SetStringField(TEXT("stack_group"), StackGroup);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraSetModuleInput(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, ModuleName, InputName, Value, ValueType, StackGroup;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("module_name"), ModuleName) || ModuleName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: module_name"));
    if (!Params->TryGetStringField(TEXT("input_name"), InputName) || InputName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: input_name"));
    if (!Params->TryGetStringField(TEXT("value"), Value) || Value.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: value"));
    if (!Params->TryGetStringField(TEXT("value_type"), ValueType) || ValueType.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: value_type (float|int|bool|vector|color)"));
    if (!Params->TryGetStringField(TEXT("stack_group"), StackGroup) || StackGroup.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: stack_group (spawn|update|emitter_spawn|emitter_update)"));

    UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    // Find emitter handle
    FNiagaraEmitterHandle* FoundHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            FoundHandle = &Handle;
            break;
        }
    }
    if (!FoundHandle)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

    FVersionedNiagaraEmitterData* EmitterData = FoundHandle->GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    // Map stack_group to script usage and get the target script
    ENiagaraScriptUsage Usage;
    UNiagaraScript* TargetScript = nullptr;
    if (StackGroup.Equals(TEXT("spawn"), ESearchCase::IgnoreCase) || StackGroup.Equals(TEXT("particle_spawn"), ESearchCase::IgnoreCase))
    {
        Usage = ENiagaraScriptUsage::ParticleSpawnScript;
        TargetScript = EmitterData->SpawnScriptProps.Script;
    }
    else if (StackGroup.Equals(TEXT("update"), ESearchCase::IgnoreCase) || StackGroup.Equals(TEXT("particle_update"), ESearchCase::IgnoreCase))
    {
        Usage = ENiagaraScriptUsage::ParticleUpdateScript;
        TargetScript = EmitterData->UpdateScriptProps.Script;
    }
    else if (StackGroup.Equals(TEXT("emitter_spawn"), ESearchCase::IgnoreCase))
    {
        Usage = ENiagaraScriptUsage::EmitterSpawnScript;
        TargetScript = EmitterData->EmitterSpawnScriptProps.Script;
    }
    else if (StackGroup.Equals(TEXT("emitter_update"), ESearchCase::IgnoreCase))
    {
        Usage = ENiagaraScriptUsage::EmitterUpdateScript;
        TargetScript = EmitterData->EmitterUpdateScriptProps.Script;
    }
    else
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported stack_group '%s'"), *StackGroup));
    }

    if (!TargetScript)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No script found for stack_group '%s'"), *StackGroup));

    // Build the aliased input variable name: {ModuleName}.{InputName}
    // NOTE: Do NOT prefix with "Module." - ConvertVariableToRapidIterationConstantName
    // prepends "Constants.{EmitterName}." automatically, producing "Constants.{EmitterName}.{ModuleName}.{InputName}"
    FString AliasedInputName = FString::Printf(TEXT("%s.%s"), *ModuleName, *InputName);

    // Determine the type and create the variable
    FNiagaraTypeDefinition TypeDef;
    if (ValueType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetFloatDef();
    else if (ValueType.Equals(TEXT("int"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetIntDef();
    else if (ValueType.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetBoolDef();
    else if (ValueType.Equals(TEXT("vector2"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetVec2Def();
    else if (ValueType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetVec3Def();
    else if (ValueType.Equals(TEXT("color"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetColorDef();
    else
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported value_type '%s'"), *ValueType));

    // Create the RI parameter variable using the engine utility
    FString UniqueEmitterName = FoundHandle->GetUniqueInstanceName();
    FNiagaraVariable InputVar(TypeDef, *AliasedInputName);
    FNiagaraVariable RIVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(InputVar, *UniqueEmitterName, Usage);

    // Set the value in the script's Rapid Iteration Parameters (bAdd=true creates if needed)
    FNiagaraParameterStore& RIStore = TargetScript->RapidIterationParameters;

    if (ValueType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
    {
        float FloatVal = FCString::Atof(*Value);
        RIStore.SetParameterValue(FloatVal, RIVar, true);
    }
    else if (ValueType.Equals(TEXT("int"), ESearchCase::IgnoreCase))
    {
        int32 IntVal = FCString::Atoi(*Value);
        RIStore.SetParameterValue(IntVal, RIVar, true);
    }
    else if (ValueType.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
    {
        FNiagaraBool BoolVal(Value.Equals(TEXT("true"), ESearchCase::IgnoreCase));
        RIStore.SetParameterValue(BoolVal, RIVar, true);
    }
    else if (ValueType.Equals(TEXT("vector2"), ESearchCase::IgnoreCase))
    {
        FVector2f Vec2Val;
        // Parse "X=50,Y=50" or "50,50" format
        FString Trimmed = Value;
        Trimmed.ReplaceInline(TEXT("X="), TEXT(""));
        Trimmed.ReplaceInline(TEXT("Y="), TEXT(""));
        Trimmed.ReplaceInline(TEXT("x="), TEXT(""));
        Trimmed.ReplaceInline(TEXT("y="), TEXT(""));
        TArray<FString> Parts;
        Trimmed.ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() >= 2)
        {
            Vec2Val.X = FCString::Atof(*Parts[0]);
            Vec2Val.Y = FCString::Atof(*Parts[1]);
        }
        else if (Parts.Num() == 1)
        {
            Vec2Val.X = Vec2Val.Y = FCString::Atof(*Parts[0]);
        }
        RIStore.SetParameterValue(Vec2Val, RIVar, true);
    }
    else if (ValueType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
    {
        FVector Parsed;
        Parsed.InitFromString(Value);
        FVector3f VecVal(Parsed);
        RIStore.SetParameterValue(VecVal, RIVar, true);
    }
    else if (ValueType.Equals(TEXT("color"), ESearchCase::IgnoreCase))
    {
        FLinearColor ColorVal;
        ColorVal.InitFromString(Value);
        RIStore.SetParameterValue(ColorVal, RIVar, true);
    }

    // Notify system of parameter changes
    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("parameter_name"), AliasedInputName);
    ResultJson->SetStringField(TEXT("ri_parameter_name"), RIVar.GetName().ToString());
    ResultJson->SetStringField(TEXT("value"), Value);
    ResultJson->SetStringField(TEXT("value_type"), ValueType);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraAddUserParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, ParamName, ParamType, DefaultValue;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName) || ParamName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_name"));
    if (!Params->TryGetStringField(TEXT("param_type"), ParamType) || ParamType.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: param_type (float|int|bool|vector|color|position)"));
    Params->TryGetStringField(TEXT("default_value"), DefaultValue);

    UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    FNiagaraTypeDefinition TypeDef;
    if (ParamType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetFloatDef();
    else if (ParamType.Equals(TEXT("int"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetIntDef();
    else if (ParamType.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetBoolDef();
    else if (ParamType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetVec3Def();
    else if (ParamType.Equals(TEXT("color"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetColorDef();
    else if (ParamType.Equals(TEXT("position"), ESearchCase::IgnoreCase))
        TypeDef = FNiagaraTypeDefinition::GetPositionDef();
    else
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported param_type '%s'. Use: float, int, bool, vector, color, position"), *ParamType));

    FString FullName = FString::Printf(TEXT("User.%s"), *ParamName);
    FNiagaraVariable NewParam(TypeDef, *FullName);

    if (!DefaultValue.IsEmpty())
    {
        if (ParamType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
        {
            float Val = FCString::Atof(*DefaultValue);
            NewParam.SetValue(Val);
        }
        else if (ParamType.Equals(TEXT("int"), ESearchCase::IgnoreCase))
        {
            int32 Val = FCString::Atoi(*DefaultValue);
            NewParam.SetValue(Val);
        }
        else if (ParamType.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
        {
            FNiagaraBool Val(DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase));
            NewParam.SetValue(Val);
        }
        else if (ParamType.Equals(TEXT("vector"), ESearchCase::IgnoreCase) || ParamType.Equals(TEXT("position"), ESearchCase::IgnoreCase))
        {
            FVector Parsed;
            Parsed.InitFromString(DefaultValue);
            FVector3f Val(Parsed);
            NewParam.SetValue(Val);
        }
        else if (ParamType.Equals(TEXT("color"), ESearchCase::IgnoreCase))
        {
            FLinearColor Val;
            Val.InitFromString(DefaultValue);
            NewParam.SetValue(Val);
        }
    }
    else
    {
        NewParam.AllocateData();
    }

    System->GetExposedParameters().AddParameter(NewParam, true);

    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("param_name"), FullName);
    ResultJson->SetStringField(TEXT("param_type"), ParamType);
    if (!DefaultValue.IsEmpty())
        ResultJson->SetStringField(TEXT("default_value"), DefaultValue);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleSpawnNiagaraActor(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, ActorName;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    Params->TryGetStringField(TEXT("name"), ActorName);

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!NiagaraSystem)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load Niagara system at '%s'"), *SystemPath));

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available"));

    FVector Location(0, 0, 200);
    const TSharedPtr<FJsonObject>* LocObj = nullptr;
    if (Params->TryGetObjectField(TEXT("location"), LocObj))
    {
        (*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
        (*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
        (*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
    }

    FRotator Rotation = FRotator::ZeroRotator;

    // Ensure compilation is fully complete before spawning
    if (NiagaraSystem->HasOutstandingCompilationRequests(true))
    {
        NiagaraSystem->WaitForCompilationComplete(true);
    }

    // Spawn a proper ANiagaraActor so it persists in the level with correct class identity.
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ANiagaraActor* NiagaraActor = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Location, Rotation, SpawnParams);
    if (!NiagaraActor)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to spawn ANiagaraActor"));

    // Set actor label
    if (!ActorName.IsEmpty())
    {
        NiagaraActor->SetActorLabel(*ActorName);
    }

    // Configure the NiagaraComponent
    UNiagaraComponent* NiagaraComp = NiagaraActor->GetNiagaraComponent();
    if (!NiagaraComp)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("ANiagaraActor has no NiagaraComponent"));

    NiagaraComp->SetAsset(NiagaraSystem);

    // ForceSolo enables editor-time ticking without PIE
    NiagaraComp->SetForceSolo(true);
    NiagaraComp->Activate(true);

    TSharedPtr<FJsonObject> DiagJson = MakeShared<FJsonObject>();
    DiagJson->SetBoolField(TEXT("system_ready_to_run"), NiagaraSystem->IsReadyToRun());
    DiagJson->SetBoolField(TEXT("active"), NiagaraComp->IsActive());
    DiagJson->SetBoolField(TEXT("ticking"), NiagaraComp->IsComponentTickEnabled());
    DiagJson->SetBoolField(TEXT("is_solo"), NiagaraComp->GetForceSolo());

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("actor_label"), NiagaraActor->GetActorLabel());
    ResultJson->SetStringField(TEXT("actor_class"), TEXT("NiagaraActor"));
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetBoolField(TEXT("component_valid"), true);
    ResultJson->SetBoolField(TEXT("component_active"), NiagaraComp->IsActive());
    ResultJson->SetObjectField(TEXT("diagnostics"), DiagJson);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraStaticSwitch(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, ModuleName, SwitchName, Value;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("module_name"), ModuleName) || ModuleName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: module_name"));
    // switch_name and value are optional — if omitted, we list all switches
    Params->TryGetStringField(TEXT("switch_name"), SwitchName);
    Params->TryGetStringField(TEXT("value"), Value);

    UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
    if (!System)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load UNiagaraSystem at '%s'"), *SystemPath));

    // Find emitter
    FNiagaraEmitterHandle* FoundHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
        {
            FoundHandle = &Handle;
            break;
        }
    }
    if (!FoundHandle)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found"), *EmitterName));

    FVersionedNiagaraEmitterData* EmitterData = FoundHandle->GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    // Get emitter graph
    UNiagaraScript* AnyScript = EmitterData->SpawnScriptProps.Script;
    if (!AnyScript) AnyScript = EmitterData->UpdateScriptProps.Script;
    if (!AnyScript)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No script found on emitter"));

    UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(AnyScript->GetLatestSource());
    if (!ScriptSource || !ScriptSource->NodeGraph)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter graph"));

    // Find the module's NiagaraNodeFunctionCall
    UNiagaraNodeFunctionCall* TargetNode = nullptr;
    for (UEdGraphNode* Node : ScriptSource->NodeGraph->Nodes)
    {
        UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
        if (FuncNode && FuncNode->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase))
        {
            TargetNode = FuncNode;
            break;
        }
    }
    if (!TargetNode)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Module '%s' not found in emitter graph"), *ModuleName));

    // If no switch_name provided, list all static switch pins
    if (SwitchName.IsEmpty())
    {
        // List ALL input pins with their values for complete visibility
        TArray<TSharedPtr<FJsonValue>> AllPinsArray;
        for (UEdGraphPin* Pin : TargetNode->Pins)
        {
            if (Pin->Direction == EGPD_Input)
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
                PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
                PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
                PinObj->SetBoolField(TEXT("linked"), Pin->LinkedTo.Num() > 0);
                AllPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }

        TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
        ResultJson->SetStringField(TEXT("module_name"), ModuleName);
        ResultJson->SetArrayField(TEXT("all_input_pins"), AllPinsArray);
        return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
    }

    // Set the static switch value — find pin by name
    UEdGraphPin* SwitchPin = nullptr;
    for (UEdGraphPin* Pin : TargetNode->Pins)
    {
        if (Pin->Direction == EGPD_Input && Pin->PinName.ToString().Equals(SwitchName, ESearchCase::IgnoreCase))
        {
            SwitchPin = Pin;
            break;
        }
    }
    if (!SwitchPin)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Pin '%s' not found on module '%s'"), *SwitchName, *ModuleName));

    FString OldValue = SwitchPin->DefaultValue;
    SwitchPin->DefaultValue = Value;

    // Mark dirty and trigger recompilation
    ScriptSource->NodeGraph->NotifyGraphChanged();
    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("module_name"), ModuleName);
    ResultJson->SetStringField(TEXT("switch_name"), SwitchPin->PinName.ToString());
    ResultJson->SetStringField(TEXT("old_value"), OldValue);
    ResultJson->SetStringField(TEXT("new_value"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraSearchAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString SearchPath = TEXT("/Game");
    FString ClassFilter = TEXT("all");
    FString NamePattern;

    Params->TryGetStringField(TEXT("search_path"), SearchPath);
    Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
    Params->TryGetStringField(TEXT("name_pattern"), NamePattern);

    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(FName(*SearchPath));

    if (ClassFilter.Equals(TEXT("system"), ESearchCase::IgnoreCase) || ClassFilter.Equals(TEXT("all"), ESearchCase::IgnoreCase))
    {
        Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
    }
    if (ClassFilter.Equals(TEXT("emitter"), ESearchCase::IgnoreCase) || ClassFilter.Equals(TEXT("all"), ESearchCase::IgnoreCase))
    {
        Filter.ClassPaths.Add(UNiagaraEmitter::StaticClass()->GetClassPathName());
    }

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssets(Filter, AssetDataList);

    TArray<TSharedPtr<FJsonValue>> Results;
    const bool bHasNamePattern = !NamePattern.IsEmpty();
    bool bTruncated = false;

    for (const FAssetData& Data : AssetDataList)
    {
        if (bHasNamePattern && !Data.AssetName.ToString().Contains(NamePattern, ESearchCase::IgnoreCase))
        {
            continue;
        }

        if (Results.Num() >= 100)
        {
            bTruncated = true;
            break;
        }

        TSharedPtr<FJsonObject> AssetJson = MakeShared<FJsonObject>();
        AssetJson->SetStringField(TEXT("asset_name"), Data.AssetName.ToString());
        AssetJson->SetStringField(TEXT("asset_path"), Data.GetSoftObjectPath().ToString());
        AssetJson->SetStringField(TEXT("class_name"), Data.AssetClassPath.GetAssetName().ToString());
        AssetJson->SetStringField(TEXT("package_path"), Data.PackagePath.ToString());
        Results.Add(MakeShared<FJsonValueObject>(AssetJson));
    }

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetArrayField(TEXT("results"), Results);
    ResultJson->SetNumberField(TEXT("count"), Results.Num());
    ResultJson->SetStringField(TEXT("search_path"), SearchPath);
    ResultJson->SetStringField(TEXT("class_filter"), ClassFilter);
    ResultJson->SetBoolField(TEXT("truncated"), bTruncated);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraDeleteEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));

    UNiagaraSystem* System = nullptr;
    FString Error;
    FNiagaraEmitterHandle* Handle = FindEmitterHandle(SystemPath, EmitterName, System, Error);
    if (!Handle)
        return FSmithUECommonUtils::CreateErrorResponse(Error);

    FGuid HandleId = Handle->GetId();
    TSet<FGuid> HandleIds;
    HandleIds.Add(HandleId);
    System->RemoveEmitterHandlesById(HandleIds);

    System->RequestCompile(false);
    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("emitter_id"), HandleId.ToString());
    ResultJson->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraDeleteRenderer(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName;
    FString RendererIndexStr = TEXT("0");
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    Params->TryGetStringField(TEXT("renderer_index"), RendererIndexStr);

    int32 RendererIndex = FCString::Atoi(*RendererIndexStr);

    UNiagaraSystem* System = nullptr;
    FString Error;
    FNiagaraEmitterHandle* Handle = FindEmitterHandle(SystemPath, EmitterName, System, Error);
    if (!Handle)
        return FSmithUECommonUtils::CreateErrorResponse(Error);

    FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();
    FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
    if (RendererIndex < 0 || RendererIndex >= Renderers.Num())
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Renderer index %d out of range (emitter has %d renderers)"), RendererIndex, Renderers.Num()));

    UNiagaraRendererProperties* RendererToDelete = Renderers[RendererIndex];
    FString RendererClass = RendererToDelete->GetClass()->GetName();

    FGuid VersionGuid = VersionedEmitter.Version;
    VersionedEmitter.Emitter->RemoveRenderer(RendererToDelete, VersionGuid);

    System->RequestCompile(false);
    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("renderer_class"), RendererClass);
    ResultJson->SetNumberField(TEXT("renderer_index"), RendererIndex);
    ResultJson->SetNumberField(TEXT("renderer_count"), VersionedEmitter.Emitter->GetLatestEmitterData()->GetRenderers().Num());
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

TSharedPtr<FJsonObject> FSmithUENiagaraCommands::HandleNiagaraDeleteModule(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath, EmitterName, ModuleName, StackGroup;
    if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: system_path"));
    if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName) || EmitterName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: emitter_name"));
    if (!Params->TryGetStringField(TEXT("module_name"), ModuleName) || ModuleName.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: module_name"));
    if (!Params->TryGetStringField(TEXT("stack_group"), StackGroup) || StackGroup.IsEmpty())
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: stack_group"));

    UNiagaraSystem* System = nullptr;
    FString Error;
    FNiagaraEmitterHandle* Handle = FindEmitterHandle(SystemPath, EmitterName, System, Error);
    if (!Handle)
        return FSmithUECommonUtils::CreateErrorResponse(Error);

    FVersionedNiagaraEmitterData* EmitterData = Handle->GetInstance().GetEmitterData();
    if (!EmitterData)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get emitter data"));

    UNiagaraScript* Script = nullptr;
    if (StackGroup.Equals(TEXT("spawn"), ESearchCase::IgnoreCase))
        Script = EmitterData->SpawnScriptProps.Script;
    else if (StackGroup.Equals(TEXT("update"), ESearchCase::IgnoreCase))
        Script = EmitterData->UpdateScriptProps.Script;
    else if (StackGroup.Equals(TEXT("emitter_spawn"), ESearchCase::IgnoreCase))
        Script = EmitterData->EmitterSpawnScriptProps.Script;
    else if (StackGroup.Equals(TEXT("emitter_update"), ESearchCase::IgnoreCase))
        Script = EmitterData->EmitterUpdateScriptProps.Script;
    else
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown stack_group '%s'. Use: spawn, update, emitter_spawn, emitter_update"), *StackGroup));

    if (!Script)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No script found for stack_group '%s'"), *StackGroup));

    UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
    if (!Source || !Source->NodeGraph)
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get script source graph"));

    UNiagaraNodeFunctionCall* TargetNode = nullptr;
    for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
    {
        UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
        if (FuncNode && FuncNode->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase))
        {
            TargetNode = FuncNode;
            break;
        }
    }
    if (!TargetNode)
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Module '%s' not found in stack_group '%s' of emitter '%s'"), *ModuleName, *StackGroup, *EmitterName));

    for (UEdGraphPin* Pin : TargetNode->Pins)
    {
        Pin->BreakAllPinLinks();
    }

    Source->NodeGraph->RemoveNode(TargetNode);

    System->RequestCompile(false);
    System->PostEditChange();
    System->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(SystemPath, false);

    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("system_path"), SystemPath);
    ResultJson->SetStringField(TEXT("emitter_name"), EmitterName);
    ResultJson->SetStringField(TEXT("module_name"), ModuleName);
    ResultJson->SetStringField(TEXT("stack_group"), StackGroup);
    return FSmithUECommonUtils::CreateSuccessResponse(ResultJson);
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUENiagaraCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // create_niagara_system
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_niagara_system"),
            TEXT("Niagara"),
            TEXT("Create a new UNiagaraSystem asset at the given content path"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name, e.g. NS_Radar"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path, e.g. /Game/Radar/Niagara"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleCreateNiagaraSystem(Params);
        });

    // niagara_get_system_info
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_get_system_info"),
            TEXT("Niagara"),
            TEXT("Get information about a Niagara system including emitters and user parameters"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path, e.g. /Game/Radar/Niagara/NS_Radar"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleGetNiagaraSystemInfo(Params);
        });

    // niagara_add_emitter
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_add_emitter"),
            TEXT("Niagara"),
            TEXT("Add a new emitter to an existing Niagara system"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name for the new emitter"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraAddEmitter(Params);
        });

    // niagara_add_emitter_from_template
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_add_emitter_from_template"),
            TEXT("Niagara"),
            TEXT("Add an emitter to a Niagara system from a specified template asset path"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name for the new emitter"), true),
                FSmithUEToolParam(TEXT("template_path"), TEXT("string"), TEXT("Full asset path to the template emitter"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraAddEmitterFromTemplate(Params);
        });

    // niagara_set_emitter_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_set_emitter_property"),
            TEXT("Niagara"),
            TEXT("Set a property on a Niagara emitter. Supported properties: enabled (bool), local_space (bool)"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter to modify"), true),
                FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Property name: enabled, local_space"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Property value as string, e.g. 'true' or 'false'"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraSetEmitterProperty(Params);
        });

    // niagara_compile
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_compile"),
            TEXT("Niagara"),
            TEXT("Compile a Niagara system and save the asset"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraCompile(Params);
        });

    // niagara_add_renderer
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_add_renderer"),
            TEXT("Niagara"),
            TEXT("Add a renderer (sprite, mesh, or ribbon) to an emitter"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter to add renderer to"), true),
                FSmithUEToolParam(TEXT("renderer_type"), TEXT("string"), TEXT("Renderer type: sprite, mesh, or ribbon"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraAddRenderer(Params);
        });

    // niagara_set_renderer_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_set_renderer_property"),
            TEXT("Niagara"),
            TEXT("Set a property on a Niagara renderer. Supports Material (asset path), and UObject properties via reflection"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter"), true),
                FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property to set (e.g. Material, Alignment)"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value to set (asset path for Material, or string representation)"), true),
                FSmithUEToolParam(TEXT("renderer_index"), TEXT("string"), TEXT("Renderer index (default: 0)"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraSetRendererProperty(Params);
        });

    // niagara_add_module
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_add_module"),
            TEXT("Niagara"),
            TEXT("Add a Niagara module script to an emitter's stack group"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter"), true),
                FSmithUEToolParam(TEXT("module_path"), TEXT("string"), TEXT("Asset path to the Niagara module script"), true),
                FSmithUEToolParam(TEXT("stack_group"), TEXT("string"), TEXT("Stack group: spawn or update"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraAddModule(Params);
        });

    // niagara_set_module_input
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_set_module_input"),
            TEXT("Niagara"),
            TEXT("Set an input value on a Niagara module"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter"), true),
                FSmithUEToolParam(TEXT("module_name"), TEXT("string"), TEXT("Module name/namespace"), true),
                FSmithUEToolParam(TEXT("input_name"), TEXT("string"), TEXT("Input parameter name"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value to set"), true),
                FSmithUEToolParam(TEXT("value_type"), TEXT("string"), TEXT("Value type: float, int, bool, vector2, vector, color"), true),
                FSmithUEToolParam(TEXT("stack_group"), TEXT("string"), TEXT("Stack group: spawn, update, emitter_spawn, emitter_update"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraSetModuleInput(Params);
        });

    // niagara_add_user_parameter
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_add_user_parameter"),
            TEXT("Niagara"),
            TEXT("Add a user parameter to a Niagara system for Blueprint interaction"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter name (without 'User.' prefix)"), true),
                FSmithUEToolParam(TEXT("param_type"), TEXT("string"), TEXT("Type: float, int, bool, vector, color, position"), true),
                FSmithUEToolParam(TEXT("default_value"), TEXT("string"), TEXT("Optional default value"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraAddUserParameter(Params);
        });

    // spawn_niagara_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("spawn_niagara_actor"),
            TEXT("Niagara"),
            TEXT("Spawn a NiagaraActor in the level with a given system asset, auto-activated"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Actor label"), false),
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Spawn location {x,y,z}"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleSpawnNiagaraActor(Params);
        });

    // niagara_static_switch
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_static_switch"),
            TEXT("Niagara"),
            TEXT("Get or set static switch values on a Niagara module. Omit switch_name to list all switches."),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter"), true),
                FSmithUEToolParam(TEXT("module_name"), TEXT("string"), TEXT("Module function name (e.g. InitializeParticle)"), true),
                FSmithUEToolParam(TEXT("switch_name"), TEXT("string"), TEXT("Static switch name to set (omit to list all)"), false),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value to set (enum index as string, e.g. '1')"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraStaticSwitch(Params);
        });

    // niagara_search_assets
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_search_assets"),
            TEXT("Niagara"),
            TEXT("Search Niagara assets (systems and emitters) in the project via AssetRegistry"),
            {
                FSmithUEToolParam(TEXT("search_path"), TEXT("string"), TEXT("Content folder path to search under, default /Game"), false),
                FSmithUEToolParam(TEXT("class_filter"), TEXT("string"), TEXT("Asset class filter: all, system, emitter"), false),
                FSmithUEToolParam(TEXT("name_pattern"), TEXT("string"), TEXT("Optional substring to match asset names"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraSearchAssets(Params);
        });

    // niagara_delete_renderer
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_delete_renderer"),
            TEXT("Niagara"),
            TEXT("Delete a renderer from an emitter by index"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter"), true),
                FSmithUEToolParam(TEXT("renderer_index"), TEXT("string"), TEXT("Renderer index to delete (default: 0)"), false)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraDeleteRenderer(Params);
        });

    // niagara_delete_module
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_delete_module"),
            TEXT("Niagara"),
            TEXT("Delete a module from an emitter's stack by function name"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter"), true),
                FSmithUEToolParam(TEXT("module_name"), TEXT("string"), TEXT("Module function name to delete"), true),
                FSmithUEToolParam(TEXT("stack_group"), TEXT("string"), TEXT("Stack group: spawn, update, emitter_spawn, emitter_update"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraDeleteModule(Params);
        });

    // niagara_delete_emitter
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("niagara_delete_emitter"),
            TEXT("Niagara"),
            TEXT("Delete an emitter from a Niagara system by name"),
            {
                FSmithUEToolParam(TEXT("system_path"), TEXT("string"), TEXT("Full asset path to the Niagara system"), true),
                FSmithUEToolParam(TEXT("emitter_name"), TEXT("string"), TEXT("Name of the emitter to delete"), true)
            }),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
        {
            return FSmithUENiagaraCommands::HandleNiagaraDeleteEmitter(Params);
        });
}

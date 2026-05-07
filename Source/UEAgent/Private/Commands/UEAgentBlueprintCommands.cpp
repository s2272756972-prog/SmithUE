// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/UEAgentBlueprintCommands.h"
#include "Blueprint/UEAgentBpAtomicAPI.h"
#include "Blueprint/UEAgentBpCompiler.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "ToolRegistry/UEAgentToolSchema.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

namespace
{
    FString CompileStatusToString(EBlueprintStatus Status)
    {
        switch (Status)
        {
        case BS_UpToDate: return TEXT("up_to_date");
        case BS_Dirty:    return TEXT("dirty");
        case BS_Error:    return TEXT("error");
        default:          return TEXT("unknown");
        }
    }

    FString PinTypeToString(const FEdGraphPinType& PinType)
    {
        FString Type = PinType.PinCategory.ToString();
		if (!PinType.PinSubCategory.IsNone())
        {
            Type += TEXT(":");
            Type += PinType.PinSubCategory.ToString();
        }
        if (PinType.PinSubCategoryObject != nullptr)
        {
            Type += TEXT("/");
            Type += PinType.PinSubCategoryObject->GetName();
        }
        return Type;
    }

	TSharedPtr<FJsonObject> MakeErrResp(const FString& Message)
    {
        return FUEAgentCommonUtils::CreateErrorResponse(Message);
    }

    TSharedPtr<FJsonObject> NodeToJson(UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

        TArray<TSharedPtr<FJsonValue>> Inputs;
        TArray<TSharedPtr<FJsonValue>> Outputs;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            if (Pin->Direction == EGPD_Input)
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("pin_type"), PinTypeToString(Pin->PinType));
                PinObj->SetStringField(TEXT("default_value"), Pin->GetDefaultAsString());

                if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0])
                {
                    UEdGraphPin* LinkedPin = Pin->LinkedTo[0];
                    UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                    if (!LinkedNode)
                    {
                        continue;
                    }
                    PinObj->SetStringField(
                        TEXT("connected_to"),
                        FString::Printf(TEXT("%s.%s"),
                            *LinkedNode->NodeGuid.ToString(),
                            *LinkedPin->PinName.ToString()));
                }

                Inputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
            else
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("pin_type"), PinTypeToString(Pin->PinType));

                TArray<TSharedPtr<FJsonValue>> Connections;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin || !LinkedPin->GetOwningNode())
                    {
                        continue;
                    }
                    Connections.Add(MakeShared<FJsonValueString>(
                        FString::Printf(TEXT("%s.%s"),
                            *LinkedPin->GetOwningNode()->NodeGuid.ToString(),
                            *LinkedPin->PinName.ToString())));
                }
                PinObj->SetArrayField(TEXT("connections"), Connections);
                Outputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }

        NodeObj->SetArrayField(TEXT("inputs"), Inputs);
        NodeObj->SetArrayField(TEXT("outputs"), Outputs);
        return NodeObj;
    }

    TSharedPtr<FJsonObject> WrapSuccess(TSharedPtr<FJsonObject> Data)
    {
        return FUEAgentCommonUtils::CreateSuccessResponse(Data);
    }
}

void FUEAgentBlueprintCommands::RegisterTools(FUEAgentToolRegistry& Registry)
{
    Registry.Register(
        FUEAgentToolSchema(
            TEXT("bp_get_summary"),
            TEXT("Blueprint"),
            TEXT("Get Blueprint metadata summary"),
            {
                FUEAgentToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true)
            }),
        &HandleBpGetSummary);

    Registry.Register(
        FUEAgentToolSchema(
            TEXT("bp_describe_graph"),
            TEXT("Blueprint"),
            TEXT("Describe all nodes and connections in a Blueprint graph"),
            {
                FUEAgentToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FUEAgentToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"), true)
            }),
        &HandleBpDescribeGraph);

    Registry.Register(
        FUEAgentToolSchema(
            TEXT("bp_compile_code"),
            TEXT("Blueprint"),
            TEXT("Compile Blueprint DSL into a Blueprint"),
            {
                FUEAgentToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FUEAgentToolParam(TEXT("code"), TEXT("string"), TEXT("Blueprint DSL text"), true)
            }),
        &HandleBpCompileCode);

    Registry.Register(
        FUEAgentToolSchema(
            TEXT("bp_batch_op"),
            TEXT("Blueprint"),
            TEXT("Execute multiple Blueprint atomic operations in a single transaction"),
            {
                FUEAgentToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FUEAgentToolParam(TEXT("operations"), TEXT("array"), TEXT("Array of operation objects"), true),
                FUEAgentToolParam(TEXT("stop_on_error"), TEXT("bool"), TEXT("Stop after first failed operation"))
            }),
        &HandleBpBatchOp);

    Registry.Register(
        FUEAgentToolSchema(
            TEXT("bp_validate_code"),
            TEXT("Blueprint"),
            TEXT("Validate Blueprint DSL syntax without compiling"),
            {
                FUEAgentToolParam(TEXT("code"), TEXT("string"), TEXT("Blueprint DSL text"), true)
            }),
        &HandleBpValidateCode);
}

TSharedPtr<FJsonObject> FUEAgentBlueprintCommands::HandleBpGetSummary(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FUEAgentCommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
    {
		return MakeErrResp(TEXT("Missing required param: 'bp_path'"));
    }

    UBlueprint* BP = FUEAgentBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));
    Data->SetStringField(TEXT("compile_status"), CompileStatusToString(BP->Status));

    TArray<TSharedPtr<FJsonValue>> Functions;
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (Graph)
        {
            Functions.Add(MakeShared<FJsonValueString>(Graph->GetName()));
        }
    }
    Data->SetArrayField(TEXT("functions"), Functions);

    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), PinTypeToString(Var.VarType));
        Variables.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    Data->SetArrayField(TEXT("variables"), Variables);

    TArray<TSharedPtr<FJsonValue>> Components;
    if (USimpleConstructionScript* SCS = BP->SimpleConstructionScript)
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node)
            {
                continue;
            }
            TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
            CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None"));
            Components.Add(MakeShared<FJsonValueObject>(CompObj));
        }
    }
    Data->SetArrayField(TEXT("components"), Components);

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FUEAgentBlueprintCommands::HandleBpDescribeGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FUEAgentCommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("graph_name")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    FString GraphName;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    UBlueprint* BP = FUEAgentBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    UEdGraph* Graph = FUEAgentBpAtomicAPI::FindGraph(BP, GraphName);
    if (!Graph)
    {
		return MakeErrResp(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node)
        {
            Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Node)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("graph_name"), GraphName);
    Data->SetArrayField(TEXT("nodes"), Nodes);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FUEAgentBlueprintCommands::HandleBpCompileCode(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FUEAgentCommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("code")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    FString Code;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("code"), Code);

    UBlueprint* BP = FUEAgentBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    FUEAgentCompileResult Result = FUEAgentBpCompiler::CompileFunction(BP, Code);

    TArray<TSharedPtr<FJsonValue>> Errors;
    for (const FString& Item : Result.Errors)
    {
        Errors.Add(MakeShared<FJsonValueString>(Item));
    }

    TArray<TSharedPtr<FJsonValue>> CreatedNodeIds;
    for (const FString& NodeId : Result.CreatedNodeIds)
    {
        CreatedNodeIds.Add(MakeShared<FJsonValueString>(NodeId));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("success"), Result.bSuccess);
    Data->SetStringField(TEXT("graph_name"), Result.GraphName);
    Data->SetArrayField(TEXT("errors"), Errors);
    Data->SetArrayField(TEXT("created_node_ids"), CreatedNodeIds);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FUEAgentBlueprintCommands::HandleBpBatchOp(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FUEAgentCommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("operations")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
    {
		return MakeErrResp(TEXT("Invalid or missing param: 'operations'"));
    }

    bool bStopOnError = true;
    Params->TryGetBoolField(TEXT("stop_on_error"), bStopOnError);

    UBlueprint* BP = FUEAgentBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("UEAgent: Blueprint Batch Op")));

    TArray<TSharedPtr<FJsonValue>> Results;
    for (const TSharedPtr<FJsonValue>& OpValue : *Operations)
    {
        TSharedPtr<FJsonObject> OpObj = OpValue.IsValid() ? OpValue->AsObject() : nullptr;
        if (!OpObj.IsValid())
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("error"), TEXT("Each operation must be an object"));
            Results.Add(MakeShared<FJsonValueObject>(Item));
            if (bStopOnError)
            {
                break;
            }
            continue;
        }

        FString OpName;
        if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("error"), TEXT("Operation missing 'op'"));
            Results.Add(MakeShared<FJsonValueObject>(Item));
            if (bStopOnError)
            {
                break;
            }
            continue;
        }

        const TSharedPtr<FJsonObject>* OpParamsPtr = nullptr;
        TSharedPtr<FJsonObject> OpParams = MakeShared<FJsonObject>();
        if (OpObj->TryGetObjectField(TEXT("params"), OpParamsPtr) && OpParamsPtr)
        {
            OpParams = *OpParamsPtr;
        }

        TSharedPtr<FJsonObject> DispatchResult = FUEAgentToolRegistry::Get().DispatchCommand(OpName, OpParams);
        if (!DispatchResult.IsValid())
        {
			DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *OpName));
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("op"), OpName);
        Item->SetObjectField(TEXT("result"), DispatchResult);
        Results.Add(MakeShared<FJsonValueObject>(Item));

        FString Status;
        if (DispatchResult->TryGetStringField(TEXT("status"), Status) && Status.Equals(TEXT("error"), ESearchCase::IgnoreCase))
        {
            if (bStopOnError)
            {
                break;
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetArrayField(TEXT("results"), Results);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FUEAgentBlueprintCommands::HandleBpValidateCode(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FUEAgentCommonUtils::ValidateRequiredParams(Params, {TEXT("code")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString Code;
    if (!Params->TryGetStringField(TEXT("code"), Code))
    {
		return MakeErrResp(TEXT("Missing required param: 'code'"));
    }

    FString SyntaxError;
    const bool bValid = FUEAgentBpCompiler::ValidateSyntax(Code, SyntaxError);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("valid"), bValid);
    if (!bValid)
    {
        Data->SetStringField(TEXT("error"), SyntaxError);
    }
    return WrapSuccess(Data);
}

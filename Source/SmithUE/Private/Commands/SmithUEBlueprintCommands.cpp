// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEBlueprintCommands.h"
#include "Blueprint/SmithUEBpAtomicAPI.h"
#include "Blueprint/SmithUEBpAtomicAPIHelpers.h"
#include "Blueprint/SmithUEBpCompiler.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
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
        // Wrap with container type if applicable
        if (PinType.ContainerType == EPinContainerType::Array)
        {
            Type = FString::Printf(TEXT("TArray<%s>"), *Type);
        }
        else if (PinType.ContainerType == EPinContainerType::Set)
        {
            Type = FString::Printf(TEXT("TSet<%s>"), *Type);
        }
        else if (PinType.ContainerType == EPinContainerType::Map)
        {
            // Build value type string
            FString ValueType = PinType.PinValueType.TerminalCategory.ToString();
            if (!PinType.PinValueType.TerminalSubCategory.IsNone())
            {
                ValueType += TEXT(":");
                ValueType += PinType.PinValueType.TerminalSubCategory.ToString();
            }
            if (PinType.PinValueType.TerminalSubCategoryObject != nullptr)
            {
                ValueType += TEXT("/");
                ValueType += PinType.PinValueType.TerminalSubCategoryObject->GetName();
            }
            Type = FString::Printf(TEXT("TMap<%s, %s>"), *Type, *ValueType);
        }
        return Type;
    }

	TSharedPtr<FJsonObject> MakeErrResp(const FString& Message)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Message);
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
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }
}

void FSmithUEBlueprintCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_get_summary"),
            TEXT("Blueprint"),
            TEXT("Get Blueprint metadata summary"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true)
            }),
        &HandleBpGetSummary);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_describe_graph"),
            TEXT("Blueprint"),
            TEXT("Describe nodes in a Blueprint graph. mode: full(default)/compact/summary/node_pins/exec_chain. exec_chain mode follows exec pins from entry points (add entry_node param to start from specific N-id)."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"), true),
                FSmithUEToolParam(TEXT("entry_node"), TEXT("string"), TEXT("For exec_chain mode: N-id to start BFS from (default: all entry points)"))
            }),
        &HandleBpDescribeGraph);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_compile_code"),
            TEXT("Blueprint"),
            TEXT("Compile Blueprint DSL into a Blueprint"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true),
                FSmithUEToolParam(TEXT("code"), TEXT("string"), TEXT("Blueprint DSL text"), true)
            }),
        &HandleBpCompileCode);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_batch_op"),
            TEXT("Blueprint"),
            TEXT("Execute multiple Blueprint atomic operations in a single transaction. Supports op aliases (connect/link/disconnect/unlink/set_default/set_value/create/add_node/delete/remove_node). Max 50 ops. Partial commit: failures do not stop subsequent ops."),
            {
                FSmithUEToolParam(TEXT("operations"), TEXT("array"), TEXT("Array of operation objects {op, params}. Max 50."), true),
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Shared Blueprint asset path injected into each op (op-level overrides)")),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Shared graph name injected into each op (op-level overrides)"))
            }),
        &HandleBpBatchOp);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_validate_code"),
            TEXT("Blueprint"),
            TEXT("Validate Blueprint DSL syntax without compiling"),
            {
                FSmithUEToolParam(TEXT("code"), TEXT("string"), TEXT("Blueprint DSL text"), true)
            }),
        &HandleBpValidateCode);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_search"),
            TEXT("Blueprint"),
            TEXT("Search nodes in a Blueprint by name (substring, case-insensitive) and/or type (exact class name). Searches all graphs (event, function, macro)."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true),
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Substring to match against node title (case-insensitive). Empty = no filter.")),
                FSmithUEToolParam(TEXT("type"), TEXT("string"), TEXT("Exact node class name to match (e.g. 'K2Node_CallFunction'). Empty = no filter.")),
                FSmithUEToolParam(TEXT("verbose"), TEXT("boolean"), TEXT("If true, include pins (in/out) for each matched node. Default false.")),
                FSmithUEToolParam(TEXT("limit"), TEXT("integer"), TEXT("Maximum number of nodes to return. Default 100."))
            }),
        &HandleBpSearch);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpGetSummary(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
    {
		return MakeErrResp(TEXT("Missing required param: 'bp_path'"));
    }

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetPathName() : TEXT("None"));
    Data->SetStringField(TEXT("compile_status"), CompileStatusToString(BP->Status));

    // --- Interfaces ---
    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
    {
        if (Iface.Interface)
        {
            Interfaces.Add(MakeShared<FJsonValueString>(Iface.Interface->GetName()));
        }
    }
    Data->SetArrayField(TEXT("interfaces"), Interfaces);

    // --- Variables (with flags) ---
    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), PinTypeToString(Var.VarType));
        if (Var.PropertyFlags & CPF_BlueprintReadOnly)
        {
            VarObj->SetBoolField(TEXT("read_only"), true);
        }
        if (Var.PropertyFlags & CPF_Net)
        {
            VarObj->SetBoolField(TEXT("replicated"), true);
        }
        if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate ||
            Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
        {
            VarObj->SetBoolField(TEXT("is_delegate"), true);
        }
        Variables.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    Data->SetArrayField(TEXT("variables"), Variables);

    // --- Functions (with signatures) ---
    TArray<TSharedPtr<FJsonValue>> Functions;
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());
        FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        // Extract signature from FunctionEntry/FunctionResult nodes
        TArray<TSharedPtr<FJsonValue>> Inputs;
        TArray<TSharedPtr<FJsonValue>> Outputs;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            if (Node->GetClass()->GetName().Contains(TEXT("K2Node_FunctionEntry")))
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->Direction == EGPD_Output &&
                        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        Inputs.Add(MakeShared<FJsonValueString>(
                            FString::Printf(TEXT("%s:%s"), *Pin->PinName.ToString(), *PinTypeToString(Pin->PinType))));
                    }
                }
            }
            else if (Node->GetClass()->GetName().Contains(TEXT("K2Node_FunctionResult")))
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->Direction == EGPD_Input &&
                        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        Outputs.Add(MakeShared<FJsonValueString>(
                            FString::Printf(TEXT("%s:%s"), *Pin->PinName.ToString(), *PinTypeToString(Pin->PinType))));
                    }
                }
            }
        }
        if (Inputs.Num() > 0)
        {
            FuncObj->SetArrayField(TEXT("inputs"), Inputs);
        }
        if (Outputs.Num() > 0)
        {
            FuncObj->SetArrayField(TEXT("outputs"), Outputs);
        }
        Functions.Add(MakeShared<FJsonValueObject>(FuncObj));
    }
    Data->SetArrayField(TEXT("functions"), Functions);

    // --- Macro graphs ---
    TArray<TSharedPtr<FJsonValue>> Macros;
    for (UEdGraph* Graph : BP->MacroGraphs)
    {
        if (Graph)
        {
            TSharedPtr<FJsonObject> MacroObj = MakeShared<FJsonObject>();
            MacroObj->SetStringField(TEXT("name"), Graph->GetName());
            MacroObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
            Macros.Add(MakeShared<FJsonValueObject>(MacroObj));
        }
    }
    if (Macros.Num() > 0)
    {
        Data->SetArrayField(TEXT("macros"), Macros);
    }

    // --- Event graphs (with node count + custom events + input bindings) ---
    TArray<TSharedPtr<FJsonValue>> EventGraphs;
    TArray<TSharedPtr<FJsonValue>> CustomEvents;
    TArray<TSharedPtr<FJsonValue>> InputBindings;
    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
        GraphObj->SetStringField(TEXT("name"), Graph->GetName());
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        EventGraphs.Add(MakeShared<FJsonValueObject>(GraphObj));

        // Scan nodes for custom events and input bindings
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            const FString ClassName = Node->GetClass()->GetName();
            if (ClassName.Contains(TEXT("K2Node_CustomEvent")))
            {
                CustomEvents.Add(MakeShared<FJsonValueString>(
                    Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
            }
            else if (ClassName.Contains(TEXT("K2Node_InputKey")) ||
                     ClassName.Contains(TEXT("K2Node_InputAction")))
            {
                InputBindings.Add(MakeShared<FJsonValueString>(
                    Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
            }
        }
    }
    Data->SetArrayField(TEXT("event_graphs"), EventGraphs);
    if (CustomEvents.Num() > 0)
    {
        Data->SetArrayField(TEXT("custom_events"), CustomEvents);
    }
    if (InputBindings.Num() > 0)
    {
        Data->SetArrayField(TEXT("input_bindings"), InputBindings);
    }

    // --- Delegate signature graphs (event dispatchers) ---
    TArray<TSharedPtr<FJsonValue>> Delegates;
    for (UEdGraph* Graph : BP->DelegateSignatureGraphs)
    {
        if (Graph)
        {
            Delegates.Add(MakeShared<FJsonValueString>(Graph->GetName()));
        }
    }
    if (Delegates.Num() > 0)
    {
        Data->SetArrayField(TEXT("event_dispatchers"), Delegates);
    }

    // --- 组件列表 (含层级关系) ---
    // --- Components (with hierarchy) ---
    TArray<TSharedPtr<FJsonValue>> Components;
    if (USimpleConstructionScript* SCS = BP->SimpleConstructionScript)
    {
        // 构建父级查找表: 子节点 → 父节点
        // Build parent lookup: child node → parent node
        TMap<USCS_Node*, USCS_Node*> ParentMap;
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node) { continue; }
            for (USCS_Node* Child : Node->ChildNodes)
            {
                if (Child) { ParentMap.Add(Child, Node); }
            }
        }

        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node)
            {
                continue;
            }
            TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
            CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None"));
            // Show SCS tree parent
            if (USCS_Node** ParentPtr = ParentMap.Find(Node))
            {
                CompObj->SetStringField(TEXT("parent"), (*ParentPtr)->GetVariableName().ToString());
            }
            // Show attachment to inherited component
            if (Node->ParentComponentOrVariableName != NAME_None)
            {
                CompObj->SetStringField(TEXT("attached_to"), Node->ParentComponentOrVariableName.ToString());
            }
            if (Node->ChildNodes.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> Children;
                for (USCS_Node* Child : Node->ChildNodes)
                {
                    if (Child) { Children.Add(MakeShared<FJsonValueString>(Child->GetVariableName().ToString())); }
                }
                CompObj->SetArrayField(TEXT("children"), Children);
            }
            Components.Add(MakeShared<FJsonValueObject>(CompObj));
        }
    }
    Data->SetArrayField(TEXT("components"), Components);

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpDescribeGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("graph_name")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    FString GraphName;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    // Mode parameter: "full" (default), "compact", "summary", "node_pins", "exec_chain"
    FString Mode = TEXT("full");
    Params->TryGetStringField(TEXT("mode"), Mode);

    // node_ids param for "node_pins" mode
    TSet<FString> NodeIdsFilter;
    if (Mode == TEXT("node_pins"))
    {
        const TArray<TSharedPtr<FJsonValue>>* NodeIdsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArr) && NodeIdsArr)
        {
            for (const TSharedPtr<FJsonValue>& Val : *NodeIdsArr)
            {
                FString NidStr;
                if (Val.IsValid() && Val->TryGetString(NidStr))
                {
                    NodeIdsFilter.Add(NidStr);
                }
            }
        }
    }

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    UEdGraph* Graph = FSmithUEBpAtomicAPI::FindGraph(BP, GraphName);
    if (!Graph)
    {
		return MakeErrResp(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }

    // Build GUID → short ID mapping (N0, N1, ...)
    TMap<FGuid, FString> GuidToShortId;
    int32 NodeIndex = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node)
        {
            GuidToShortId.Add(Node->NodeGuid, FString::Printf(TEXT("N%d"), NodeIndex++));
        }
    }

    // Store server-side N-id → GUID mapping for subsequent commands (e.g. bp_connect_pins).
    // GraphPath is scoped per-graph so N-ids are never shared across graphs.
    {
        const FString GraphPath = BpPath + TEXT("::") + GraphName;
        TMap<int32, FGuid> IndexToGuid;
        IndexToGuid.Reserve(GuidToShortId.Num());
        for (const TPair<FGuid, FString>& Pair : GuidToShortId)
        {
            // NidStr is "N<index>"; strip the leading 'N' to get the integer key.
            const FString& NidStr = Pair.Value;
            if (NidStr.Len() >= 2 && NidStr[0] == TEXT('N'))
            {
                const int32 Idx = FCString::Atoi(*NidStr.Mid(1));
                IndexToGuid.Add(Idx, Pair.Key);
            }
        }
        FSmithUEToolRegistry::Get().NidSession.StoreNids(GraphPath, IndexToGuid);
    }

    // For exec_chain mode: collect only exec-reachable nodes via BFS.
    // IMPORTANT: placed after StoreNids so entry_node N-id resolution works.
    TSet<UEdGraphNode*> ExecChainNodes;
    if (Mode == TEXT("exec_chain"))
    {
        TQueue<UEdGraphNode*> BfsQueue;

        // Optional: start from a specific node.
        FString EntryNodeId;
        if (Params->TryGetStringField(TEXT("entry_node"), EntryNodeId) && !EntryNodeId.IsEmpty())
        {
            FString DummyError;
            const FString GraphPath = BpPath + TEXT("::") + GraphName;
            UEdGraphNode* StartNode = SmithUEBpAtomicAPIHelpers::ResolveNodeId(Graph, GraphPath, EntryNodeId, DummyError);
            if (StartNode)
            {
                BfsQueue.Enqueue(StartNode);
            }
        }
        else
        {
            // Find all entry point nodes (events, function entries, input key nodes).
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }
                const FString NodeClass = Node->GetClass()->GetName();
                if (NodeClass.Contains(TEXT("K2Node_FunctionEntry")) ||
                    NodeClass.Contains(TEXT("K2Node_Event")) ||
                    NodeClass.Contains(TEXT("K2Node_InputKey")) ||
                    NodeClass.Contains(TEXT("K2Node_InputAction")) ||
                    NodeClass.Contains(TEXT("K2Node_EnhancedInputAction")) ||
                    NodeClass.Contains(TEXT("K2Node_CustomEvent")))
                {
                    BfsQueue.Enqueue(Node);
                }
            }
        }

        // BFS following exec output pins only.
        while (!BfsQueue.IsEmpty())
        {
            UEdGraphNode* Current = nullptr;
            BfsQueue.Dequeue(Current);
            if (!Current || ExecChainNodes.Contains(Current))
            {
                continue;
            }
            ExecChainNodes.Add(Current);
            for (UEdGraphPin* Pin : Current->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output)
                {
                    continue;
                }
                if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                {
                    continue;
                }
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (LinkedPin && LinkedPin->GetOwningNode())
                    {
                        BfsQueue.Enqueue(LinkedPin->GetOwningNode());
                    }
                }
            }
        }
    }

    // Helper: resolve a linked pin to "ShortId.PinName"
    auto ResolvePinRef = [&GuidToShortId](UEdGraphPin* LinkedPin) -> FString
    {
        if (!LinkedPin)
        {
            return FString();
        }
        UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
        if (!LinkedNode)
        {
            return FString();
        }
        const FString* ShortId = GuidToShortId.Find(LinkedNode->NodeGuid);
        if (!ShortId)
        {
            return FString();
        }
        return FString::Printf(TEXT("%s.%s"), **ShortId, *LinkedPin->PinName.ToString());
    };

    // Helper: build full pin arrays for a node (shared by "full" and "node_pins" modes)
    auto BuildPinArrays = [&](UEdGraphNode* Node,
                               TArray<TSharedPtr<FJsonValue>>& Inputs,
                               TArray<TSharedPtr<FJsonValue>>& Outputs,
                               bool bCompact)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            // compact: skip hidden pins and pins with no default and no connections
            if (bCompact)
            {
                if (Pin->bHidden)
                {
                    continue;
                }
                if (Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.Num() == 0)
                {
                    continue;
                }
            }

            if (Pin->Direction == EGPD_Input)
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

                // Default value: only include if non-empty
                const FString DefaultVal = Pin->GetDefaultAsString();
                if (!DefaultVal.IsEmpty())
                {
                    PinObj->SetStringField(TEXT("default"), DefaultVal);
                }

                // All connections
                if (Pin->LinkedTo.Num() > 0)
                {
                    TArray<TSharedPtr<FJsonValue>> Conns;
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        FString Ref = ResolvePinRef(LinkedPin);
                        if (!Ref.IsEmpty())
                        {
                            Conns.Add(MakeShared<FJsonValueString>(Ref));
                        }
                    }
                    if (Conns.Num() == 1)
                    {
                        PinObj->SetStringField(TEXT("from"), Conns[0]->AsString());
                    }
                    else if (Conns.Num() > 1)
                    {
                        PinObj->SetArrayField(TEXT("from"), Conns);
                    }
                }

                Inputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
            else // Output
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

                TArray<TSharedPtr<FJsonValue>> Conns;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    FString Ref = ResolvePinRef(LinkedPin);
                    if (!Ref.IsEmpty())
                    {
                        Conns.Add(MakeShared<FJsonValueString>(Ref));
                    }
                }
                if (Conns.Num() == 1)
                {
                    PinObj->SetStringField(TEXT("to"), Conns[0]->AsString());
                }
                else if (Conns.Num() > 1)
                {
                    PinObj->SetArrayField(TEXT("to"), Conns);
                }
                Outputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }
    };

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        const FString* ShortId = GuidToShortId.Find(Node->NodeGuid);
        if (!ShortId)
        {
            continue;
        }

        // exec_chain mode: skip nodes not on the exec path.
        if (Mode == TEXT("exec_chain") && !ExecChainNodes.Contains(Node))
        {
            continue;
        }

        // node_pins mode: skip nodes not in the requested list
        if (Mode == TEXT("node_pins") && !NodeIdsFilter.Contains(*ShortId))
        {
            continue;
        }

        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("id"), *ShortId);
        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

        // summary mode: no pins
        if (Mode != TEXT("summary"))
        {
            const bool bCompact = (Mode == TEXT("compact") || Mode == TEXT("exec_chain"));
            TArray<TSharedPtr<FJsonValue>> Inputs;
            TArray<TSharedPtr<FJsonValue>> Outputs;
            BuildPinArrays(Node, Inputs, Outputs, bCompact);

            if (Inputs.Num() > 0)
            {
                NodeObj->SetArrayField(TEXT("in"), Inputs);
            }
            if (Outputs.Num() > 0)
            {
                NodeObj->SetArrayField(TEXT("out"), Outputs);
            }
        }

        Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("graph_name"), GraphName);
    Data->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
    Data->SetArrayField(TEXT("nodes"), Nodes);

    // Only "full" mode includes id_to_guid (largest token cost)
    if (Mode == TEXT("full"))
    {
        TSharedPtr<FJsonObject> IdMap = MakeShared<FJsonObject>();
        for (const auto& Pair : GuidToShortId)
        {
            IdMap->SetStringField(Pair.Value, Pair.Key.ToString());
        }
        Data->SetObjectField(TEXT("id_to_guid"), IdMap);
    }

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpCompileCode(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("code")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    FString Code;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("code"), Code);

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    FSmithUECompileResult Result = FSmithUEBpCompiler::CompileFunction(BP, Code);

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

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpBatchOp(const TSharedPtr<FJsonObject>& Params)
{
    // --- Op alias table (resolved once at first call) ---
    static const TMap<FString, FString> OpAliases = []()
    {
        TMap<FString, FString> T;
        T.Add(TEXT("connect"),       TEXT("bp_connect_pins"));
        T.Add(TEXT("link"),          TEXT("bp_connect_pins"));
        T.Add(TEXT("disconnect"),    TEXT("bp_disconnect_pins"));
        T.Add(TEXT("unlink"),        TEXT("bp_disconnect_pins"));
        T.Add(TEXT("set_default"),   TEXT("bp_set_pin_default"));
        T.Add(TEXT("set_value"),     TEXT("bp_set_pin_default"));
        T.Add(TEXT("create"),        TEXT("bp_create_node"));
        T.Add(TEXT("add_node"),      TEXT("bp_create_node"));
        T.Add(TEXT("delete"),        TEXT("bp_delete_node"));
        T.Add(TEXT("remove_node"),   TEXT("bp_delete_node"));
        return T;
    }();

    // --- Validate 'operations' is present ---
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("operations")}, Error))
    {
        return MakeErrResp(Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
    {
        return MakeErrResp(TEXT("Invalid or missing param: 'operations'"));
    }

    // --- Batch size limit ---
    if (Operations->Num() > 50)
    {
        return MakeErrResp(FString::Printf(
            TEXT("Batch size %d exceeds maximum of 50 operations"), Operations->Num()));
    }

    // --- Shared batch-level params (bp_path, graph_name) ---
    FString BatchBpPath;
    Params->TryGetStringField(TEXT("bp_path"), BatchBpPath);
    FString BatchGraphName;
    Params->TryGetStringField(TEXT("graph_name"), BatchGraphName);

    bool bAtomic = false;
    Params->TryGetBoolField(TEXT("atomic"), bAtomic);
    if (bAtomic)
    {
        auto MakeAtomicDryRunError = [](const TArray<FString>& Errors) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
            Response->SetStringField(TEXT("status"), TEXT("error"));
            Response->SetStringField(TEXT("error"), TEXT("atomic dry-run failed"));
            TArray<TSharedPtr<FJsonValue>> ErrorValues;
            for (const FString& Item : Errors)
            {
                ErrorValues.Add(MakeShared<FJsonValueString>(Item));
            }
            Response->SetArrayField(TEXT("errors"), ErrorValues);
            return Response;
        };

        auto BuildOpParams = [&BatchBpPath, &BatchGraphName](const TSharedPtr<FJsonObject>& OpObj) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> OpParams = MakeShared<FJsonObject>();
            if (!BatchBpPath.IsEmpty())
            {
                OpParams->SetStringField(TEXT("bp_path"), BatchBpPath);
            }
            if (!BatchGraphName.IsEmpty())
            {
                OpParams->SetStringField(TEXT("graph_name"), BatchGraphName);
            }
            for (const auto& KV : OpObj->Values)
            {
                if (!KV.Key.Equals(TEXT("op"), ESearchCase::IgnoreCase) &&
                    !KV.Key.Equals(TEXT("atomic"), ESearchCase::IgnoreCase))
                {
                    OpParams->Values.Add(KV.Key, KV.Value);
                }
            }
            const TSharedPtr<FJsonObject>* OpParamsPtr = nullptr;
            if (OpObj->TryGetObjectField(TEXT("params"), OpParamsPtr) && OpParamsPtr && OpParamsPtr->IsValid())
            {
                for (const auto& KV : (*OpParamsPtr)->Values)
                {
                    OpParams->Values.Add(KV.Key, KV.Value);
                }
            }
            return OpParams;
        };

        auto FindNodeById = [](UEdGraph* Graph, const FString& GraphPath, const FString& NodeId, FString& Error) -> UEdGraphNode*
        {
            if (!Graph)
            {
                Error = TEXT("Graph not found");
                return nullptr;
            }

            bool bIsStale = false;
            const FGuid NodeGuid = FSmithUEToolRegistry::Get().NidSession.ResolveNid(GraphPath, NodeId, bIsStale);
            if (bIsStale)
            {
                Error = FString::Printf(TEXT("N-id session is stale for graph '%s'"), *GraphPath);
                return nullptr;
            }
            if (!NodeGuid.IsValid())
            {
                Error = FString::Printf(TEXT("Node id '%s' not found in N-id session for graph '%s'"), *NodeId, *GraphPath);
                return nullptr;
            }

            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && Node->NodeGuid == NodeGuid)
                {
                    return Node;
                }
            }

            Error = FString::Printf(TEXT("Node id '%s' resolved to missing node in graph '%s'"), *NodeId, *GraphPath);
            return nullptr;
        };

        auto ValidateNodeAndPin = [&FindNodeById](UEdGraph* Graph, const FString& GraphPath, const FString& NodeField, const FString& NodeId, const FString& PinField, const FString& PinName, FString& Error) -> bool
        {
            UEdGraphNode* Node = FindNodeById(Graph, GraphPath, NodeId, Error);
            if (!Node)
            {
                Error = FString::Printf(TEXT("%s: %s"), *NodeField, *Error);
                return false;
            }

            if (!PinName.IsEmpty())
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->PinName.ToString() == PinName)
                    {
                        return true;
                    }
                }
                Error = FString::Printf(TEXT("%s '%s' not found on %s '%s'"), *PinField, *PinName, *NodeField, *NodeId);
                return false;
            }

            return true;
        };

        auto ExtractDispatchError = [](const TSharedPtr<FJsonObject>& DispatchResult, const FString& OpName) -> FString
        {
            FString OpError;
            if (DispatchResult.IsValid())
            {
                DispatchResult->TryGetStringField(TEXT("error"), OpError);
            }
            return OpError.IsEmpty() ? FString::Printf(TEXT("Op '%s' failed"), *OpName) : OpError;
        };

        struct FAtomicBatchOp
        {
            int32 Index = INDEX_NONE;
            FString OpName;
            TSharedPtr<FJsonObject> Params;
        };

        TArray<FAtomicBatchOp> AtomicOps;
        TArray<FAtomicBatchOp> CompileOps;
        TArray<FString> DryRunErrors;

        for (int32 OpIndex = 0; OpIndex < Operations->Num(); ++OpIndex)
        {
            const TSharedPtr<FJsonValue>& OpValue = (*Operations)[OpIndex];
            TSharedPtr<FJsonObject> OpObj = OpValue.IsValid() ? OpValue->AsObject() : nullptr;
            if (!OpObj.IsValid())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d]: Each operation must be an object"), OpIndex));
                continue;
            }

            FString OpName;
            if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d]: Operation missing 'op'"), OpIndex));
                continue;
            }
            if (const FString* Resolved = OpAliases.Find(OpName))
            {
                OpName = *Resolved;
            }

            TSharedPtr<FJsonObject> OpParams = BuildOpParams(OpObj);
            FAtomicBatchOp BatchOp{OpIndex, OpName, OpParams};
            if (OpName.Equals(TEXT("bp_compile"), ESearchCase::IgnoreCase))
            {
                CompileOps.Add(BatchOp);
                continue;
            }

            AtomicOps.Add(BatchOp);

            FString BpPath;
            FString GraphName;
            const bool bNeedsGraph = OpParams->HasField(TEXT("node_id")) ||
                                     OpParams->HasField(TEXT("source_node_id")) ||
                                     OpParams->HasField(TEXT("target_node_id"));
            if (!bNeedsGraph)
            {
                continue;
            }

            if (!OpParams->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Missing required param: 'bp_path'"), OpIndex, *OpName));
                continue;
            }
            if (!OpParams->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Missing required param: 'graph_name'"), OpIndex, *OpName));
                continue;
            }

            UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
            if (!BP)
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Failed to load blueprint: %s"), OpIndex, *OpName, *BpPath));
                continue;
            }
            UEdGraph* Graph = FSmithUEBpAtomicAPI::FindGraph(BP, GraphName);
            if (!Graph)
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Graph not found: %s"), OpIndex, *OpName, *GraphName));
                continue;
            }

            const FString GraphPath = BpPath + TEXT("::") + GraphName;
            FString ValidationError;
            FString NodeId;
            if (OpParams->TryGetStringField(TEXT("node_id"), NodeId) && !NodeId.IsEmpty())
            {
                FString PinName;
                OpParams->TryGetStringField(TEXT("pin_name"), PinName);
                if (!ValidateNodeAndPin(Graph, GraphPath, TEXT("node_id"), NodeId, TEXT("pin_name"), PinName, ValidationError))
                {
                    DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': %s"), OpIndex, *OpName, *ValidationError));
                }
            }

            FString SourceNodeId;
            if (OpParams->TryGetStringField(TEXT("source_node_id"), SourceNodeId) && !SourceNodeId.IsEmpty())
            {
                FString SourcePin;
                OpParams->TryGetStringField(TEXT("source_pin"), SourcePin);
                if (!ValidateNodeAndPin(Graph, GraphPath, TEXT("source_node_id"), SourceNodeId, TEXT("source_pin"), SourcePin, ValidationError))
                {
                    DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': %s"), OpIndex, *OpName, *ValidationError));
                }
            }

            FString TargetNodeId;
            if (OpParams->TryGetStringField(TEXT("target_node_id"), TargetNodeId) && !TargetNodeId.IsEmpty())
            {
                FString TargetPin;
                OpParams->TryGetStringField(TEXT("target_pin"), TargetPin);
                if (!ValidateNodeAndPin(Graph, GraphPath, TEXT("target_node_id"), TargetNodeId, TEXT("target_pin"), TargetPin, ValidationError))
                {
                    DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': %s"), OpIndex, *OpName, *ValidationError));
                }
            }
        }

        if (DryRunErrors.Num() > 0)
        {
            return MakeAtomicDryRunError(DryRunErrors);
        }

        TSet<FString> StaleGraphPaths;
        {
            FScopedTransaction AtomicTxn(FText::FromString(TEXT("SmithUE Atomic Batch")));
            for (const FAtomicBatchOp& Op : AtomicOps)
            {
                TSharedPtr<FJsonObject> DispatchResult = FSmithUEToolRegistry::Get().DispatchCommand(Op.OpName, Op.Params);
                if (!DispatchResult.IsValid())
                {
                    DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *Op.OpName));
                }

                FString OpStatus;
                DispatchResult->TryGetStringField(TEXT("status"), OpStatus);
                if (OpStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase))
                {
                    AtomicTxn.Cancel();
                    return MakeErrResp(FString::Printf(TEXT("op[%d] '%s' failed: %s"), Op.Index, *Op.OpName, *ExtractDispatchError(DispatchResult, Op.OpName)));
                }

                if (Op.OpName.Equals(TEXT("bp_create_node"), ESearchCase::IgnoreCase) ||
                    Op.OpName.Equals(TEXT("bp_delete_node"), ESearchCase::IgnoreCase))
                {
                    FString OpBpPath;
                    FString OpGraphName;
                    if (Op.Params->TryGetStringField(TEXT("bp_path"), OpBpPath) &&
                        Op.Params->TryGetStringField(TEXT("graph_name"), OpGraphName) &&
                        !OpBpPath.IsEmpty() && !OpGraphName.IsEmpty())
                    {
                        StaleGraphPaths.Add(OpBpPath + TEXT("::") + OpGraphName);
                    }
                }
            }
        }

        for (const FString& GraphPath : StaleGraphPaths)
        {
            FSmithUEToolRegistry::Get().NidSession.MarkStale(GraphPath);
        }

        for (const FAtomicBatchOp& Op : CompileOps)
        {
            TSharedPtr<FJsonObject> DispatchResult = FSmithUEToolRegistry::Get().DispatchCommand(Op.OpName, Op.Params);
            if (!DispatchResult.IsValid())
            {
                DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *Op.OpName));
            }

            FString OpStatus;
            DispatchResult->TryGetStringField(TEXT("status"), OpStatus);
            if (OpStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase))
            {
                return MakeErrResp(FString::Printf(TEXT("op[%d] '%s' failed: %s"), Op.Index, *Op.OpName, *ExtractDispatchError(DispatchResult, Op.OpName)));
            }
        }

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("atomic"), true);
        Data->SetNumberField(TEXT("operations_executed"), Operations->Num());
        return WrapSuccess(Data);
    }

    // --- Single transaction wrapping the entire batch ---
    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Blueprint Batch Op")));

    TArray<TSharedPtr<FJsonValue>> Results;
    TSet<FString> StaleGraphPaths;
    for (int32 OpIndex = 0; OpIndex < Operations->Num(); ++OpIndex)
    {
        const TSharedPtr<FJsonValue>& OpValue = (*Operations)[OpIndex];

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("index"), OpIndex);

        TSharedPtr<FJsonObject> OpObj = OpValue.IsValid() ? OpValue->AsObject() : nullptr;
        if (!OpObj.IsValid())
        {
            Item->SetStringField(TEXT("status"), TEXT("error"));
            Item->SetStringField(TEXT("error"), TEXT("Each operation must be an object"));
            Results.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }

        FString OpName;
        if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
        {
            Item->SetStringField(TEXT("status"), TEXT("error"));
            Item->SetStringField(TEXT("error"), TEXT("Operation missing 'op'"));
            Results.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }

        // --- Resolve alias ---
        if (const FString* Resolved = OpAliases.Find(OpName))
        {
            OpName = *Resolved;
        }

        // --- Build merged params: batch-level defaults, op-level overrides ---
        TSharedPtr<FJsonObject> OpParams = MakeShared<FJsonObject>();

        // Inject batch-level shared params first (lower priority)
        if (!BatchBpPath.IsEmpty())
        {
            OpParams->SetStringField(TEXT("bp_path"), BatchBpPath);
        }
        if (!BatchGraphName.IsEmpty())
        {
            OpParams->SetStringField(TEXT("graph_name"), BatchGraphName);
        }

        // Overlay op-level params (higher priority — overwrite batch-level)
        // First: copy direct op-level fields (flat format: {op:"set_default", node_id:"N5", ...})
        for (const auto& KV : OpObj->Values)
        {
            if (!KV.Key.Equals(TEXT("op"), ESearchCase::IgnoreCase))
            {
                OpParams->Values.Add(KV.Key, KV.Value);
            }
        }
        // Also support nested "params" sub-object (legacy format), overrides flat fields
        const TSharedPtr<FJsonObject>* OpParamsPtr = nullptr;
        if (OpObj->TryGetObjectField(TEXT("params"), OpParamsPtr) && OpParamsPtr && OpParamsPtr->IsValid())
        {
            for (const auto& KV : (*OpParamsPtr)->Values)
            {
                OpParams->Values.Add(KV.Key, KV.Value);
            }
        }

        // --- Dispatch ---
        TSharedPtr<FJsonObject> DispatchResult = FSmithUEToolRegistry::Get().DispatchCommand(OpName, OpParams);
        if (!DispatchResult.IsValid())
        {
            DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *OpName));
        }

        // --- Build per-op result entry ---
        FString OpStatus;
        DispatchResult->TryGetStringField(TEXT("status"), OpStatus);
        const bool bOpFailed = OpStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase);
        const bool bMutationOp = OpName.Equals(TEXT("bp_create_node"), ESearchCase::IgnoreCase) ||
                                 OpName.Equals(TEXT("bp_delete_node"), ESearchCase::IgnoreCase);

        Item->SetStringField(TEXT("status"), bOpFailed ? TEXT("error") : TEXT("success"));
        if (bOpFailed)
        {
            FString OpError;
            DispatchResult->TryGetStringField(TEXT("error"), OpError);
            if (OpError.IsEmpty())
            {
                // Fallback: serialize the full result as error detail
                OpError = FString::Printf(TEXT("Op '%s' failed"), *OpName);
            }
            Item->SetStringField(TEXT("error"), OpError);
        }
        else if (bMutationOp)
        {
            FString OpBpPath;
            FString OpGraphName;
            if (OpParams->TryGetStringField(TEXT("bp_path"), OpBpPath) &&
                OpParams->TryGetStringField(TEXT("graph_name"), OpGraphName) &&
                !OpBpPath.IsEmpty() && !OpGraphName.IsEmpty())
            {
                const FString GraphPath = OpBpPath + TEXT("::") + OpGraphName;
                StaleGraphPaths.Add(GraphPath);
                Item->SetBoolField(TEXT("nid_stale"), true);
            }
        }

        Results.Add(MakeShared<FJsonValueObject>(Item));
        // Partial commit: always continue to next op regardless of failure
    }

    for (const FString& GraphPath : StaleGraphPaths)
    {
        FSmithUEToolRegistry::Get().NidSession.MarkStale(GraphPath);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("results"), Results);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpValidateCode(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("code")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString Code;
    if (!Params->TryGetStringField(TEXT("code"), Code))
    {
		return MakeErrResp(TEXT("Missing required param: 'code'"));
    }

    FString SyntaxError;
    const bool bValid = FSmithUEBpCompiler::ValidateSyntax(Code, SyntaxError);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("valid"), bValid);
    if (!bValid)
    {
        Data->SetStringField(TEXT("error"), SyntaxError);
    }
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpSearch(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    // --- Filter params ---
    FString NameFilter;
    Params->TryGetStringField(TEXT("name"), NameFilter);
    const FString NameFilterLower = NameFilter.ToLower();

    FString TypeFilter;
    Params->TryGetStringField(TEXT("type"), TypeFilter);

    bool bVerbose = false;
    Params->TryGetBoolField(TEXT("verbose"), bVerbose);

    int32 Limit = 100;
    {
        int32 LimitParam = 0;
        if (Params->TryGetNumberField(TEXT("limit"), LimitParam) && LimitParam > 0)
        {
            Limit = LimitParam;
        }
    }

    // --- Collect all graphs ---
    TArray<UEdGraph*> AllGraphs;
    BP->GetAllGraphs(AllGraphs);

    // --- Search nodes ---
    TArray<TSharedPtr<FJsonValue>> ResultNodes;
    int32 MatchCount = 0;

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        const FString GraphName = Graph->GetName();
        const FString GraphPath = BpPath + TEXT("::") + GraphName;

        // --- Build / refresh GuidToShortId for this graph (mirrors bp_describe_graph) ---
        // Always rebuild: ensures the session is fresh and consistent with current node order.
        TMap<FGuid, FString> GuidToShortId;
        {
            int32 NodeIndex = 0;
            for (UEdGraphNode* N : Graph->Nodes)
            {
                if (N)
                {
                    GuidToShortId.Add(N->NodeGuid, FString::Printf(TEXT("N%d"), NodeIndex++));
                }
            }

            // Store into NidSession so subsequent commands (bp_connect_pins etc.) can resolve.
            TMap<int32, FGuid> IndexToGuid;
            IndexToGuid.Reserve(GuidToShortId.Num());
            for (const TPair<FGuid, FString>& Pair : GuidToShortId)
            {
                const FString& NidStr = Pair.Value;
                if (NidStr.Len() >= 2 && NidStr[0] == TEXT('N'))
                {
                    const int32 Idx = FCString::Atoi(*NidStr.Mid(1));
                    IndexToGuid.Add(Idx, Pair.Key);
                }
            }
            FSmithUEToolRegistry::Get().NidSession.StoreNids(GraphPath, IndexToGuid);
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            if (MatchCount >= Limit)
            {
                break;
            }

            // --- Type filter (exact class name match) ---
            if (!TypeFilter.IsEmpty())
            {
                if (!Node->GetClass()->GetName().Equals(TypeFilter, ESearchCase::IgnoreCase))
                {
                    continue;
                }
            }

            // --- Name filter (case-insensitive substring of full title) ---
            if (!NameFilterLower.IsEmpty())
            {
                const FString TitleLower = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().ToLower();
                if (!TitleLower.Contains(NameFilterLower))
                {
                    continue;
                }
            }

            // Resolve N-id from session map (guaranteed present after StoreNids above).
            const FString* ShortId = GuidToShortId.Find(Node->NodeGuid);
            const FString NidStr = ShortId ? *ShortId : Node->NodeGuid.ToString();

            // --- Build node object ---
            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("nid"),      NidStr);
            NodeObj->SetStringField(TEXT("title"),    Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            NodeObj->SetStringField(TEXT("type"),     Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("graph"),    GraphName);
            NodeObj->SetObjectField(TEXT("position"), [&]()
            {
                TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
                Pos->SetNumberField(TEXT("x"), Node->NodePosX);
                Pos->SetNumberField(TEXT("y"), Node->NodePosY);
                return Pos;
            }());

            // --- Verbose: include pins ---
            if (bVerbose)
            {
                TArray<TSharedPtr<FJsonValue>> PinsIn;
                TArray<TSharedPtr<FJsonValue>> PinsOut;

                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin)
                    {
                        continue;
                    }
                    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                    PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
                    PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

                    TArray<TSharedPtr<FJsonValue>> ConnectedTo;
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin) continue;
                        UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                        if (!LinkedNode) continue;
                        const FString* Nid = GuidToShortId.Find(LinkedNode->NodeGuid);
                        if (Nid) ConnectedTo.Add(MakeShared<FJsonValueString>(*Nid));
                    }
                    PinObj->SetArrayField(TEXT("connected_to"), ConnectedTo);

                    if (Pin->Direction == EGPD_Input)
                    {
                        PinsIn.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                    else
                    {
                        PinsOut.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                }

                if (PinsIn.Num() > 0)
                {
                    NodeObj->SetArrayField(TEXT("in"), PinsIn);
                }
                if (PinsOut.Num() > 0)
                {
                    NodeObj->SetArrayField(TEXT("out"), PinsOut);
                }
            }

            ResultNodes.Add(MakeShared<FJsonValueObject>(NodeObj));
            ++MatchCount;
        }

        if (MatchCount >= Limit)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetArrayField(TEXT("nodes"), ResultNodes);
    Data->SetNumberField(TEXT("count"), ResultNodes.Num());
    return WrapSuccess(Data);
}

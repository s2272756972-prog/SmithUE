// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEDebugCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/Breakpoint.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace SmithUEDebug
{
    FString NormalizeObjectPath(const FString& AssetPath)
    {
        if (AssetPath.IsEmpty() || AssetPath.Contains(TEXT(".")))
        {
            return AssetPath;
        }

        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        return AssetName.IsEmpty() ? AssetPath : FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    }

    UBlueprint* LoadBlueprintForDebug(const FString& BlueprintPath)
    {
        return BlueprintPath.IsEmpty() ? nullptr : LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(BlueprintPath));
    }

    IAssetRegistry& GetDebugAssetRegistry()
    {
        return FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    }

    bool GetAssetDataForPath(const FString& AssetPath, FAssetData& OutAssetData, FString& OutError)
    {
        if (AssetPath.IsEmpty())
        {
            OutError = TEXT("asset_path is required");
            return false;
        }

        IAssetRegistry& AssetRegistry = GetDebugAssetRegistry();
        FARFilter Filter;
        Filter.PackageNames.Add(FName(*AssetPath));

        TArray<FAssetData> FoundAssets;
        AssetRegistry.GetAssets(Filter, FoundAssets);
        if (FoundAssets.Num() == 0)
        {
            OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
            return false;
        }

        OutAssetData = FoundAssets[0];
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> NameArrayToJson(const TArray<FName>& Names)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(Names.Num());
        for (const FName& Name : Names)
        {
            Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
        }
        return Values;
    }

    FString PinDirectionToString(EEdGraphPinDirection Direction)
    {
        return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
    }

    FString DebugPinTypeToString(const FEdGraphPinType& PinType)
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

    bool IsPinWorthReporting(const UEdGraphPin* Pin)
    {
        if (!Pin || Pin->bHidden || Pin->LinkedTo.Num() > 0)
        {
            return false;
        }

        const FName& Category = Pin->PinType.PinCategory;
        return Category == TEXT("exec") || Category == TEXT("delegate") || Category == TEXT("object") ||
            Category == TEXT("interface") || Category == TEXT("class") || Category == TEXT("softobject") ||
            Category == TEXT("softclass") || Category == TEXT("struct") || Category == TEXT("array") ||
            Pin->Direction == EGPD_Output || Pin->GetDefaultAsString().IsEmpty();
    }

    void AppendBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
    {
        if (!Blueprint)
        {
            return;
        }

        Blueprint->GetAllGraphs(OutGraphs);
    }

    void AddCompilerMessagesFromGraphs(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutMessages, int32& OutErrorCount, int32& OutWarningCount)
    {
        TArray<UEdGraph*> Graphs;
        AppendBlueprintGraphs(Blueprint, Graphs);

        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node || !Node->bHasCompilerMessage || Node->ErrorMsg.IsEmpty())
                {
                    continue;
                }

                const bool bIsError = Node->ErrorType <= static_cast<int32>(EMessageSeverity::Error);
                if (bIsError)
                {
                    ++OutErrorCount;
                }
                else
                {
                    ++OutWarningCount;
                }

                TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
                Message->SetStringField(TEXT("severity"), bIsError ? TEXT("error") : TEXT("warning"));
                Message->SetStringField(TEXT("message"), Node->ErrorMsg);
                Message->SetStringField(TEXT("graph"), Graph->GetName());
                Message->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
                Message->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                OutMessages.Add(MakeShared<FJsonValueObject>(Message));
            }
        }
    }

    FTopLevelAssetPath ClassNameToClassPath(const FString& ClassName)
    {
        FString PackageName;
        FString AssetName;
        if (ClassName.Split(TEXT("."), &PackageName, &AssetName) && !PackageName.IsEmpty() && !AssetName.IsEmpty())
        {
            return FTopLevelAssetPath(FName(*PackageName), FName(*AssetName));
        }
        return FTopLevelAssetPath(TEXT("/Script/Engine"), FName(*ClassName));
    }

    int32 CountInvalidVariables(UBlueprint* Blueprint)
    {
        int32 InvalidCount = 0;
        if (!Blueprint)
        {
            return InvalidCount;
        }

        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            if (!FBlueprintEditorUtils::IsPinTypeValid(Variable.VarType))
            {
                ++InvalidCount;
            }
        }
        return InvalidCount;
    }

    TSharedPtr<FJsonObject> BuildDependencyNode(const FName& PackageName, IAssetRegistry& AssetRegistry, int32 Depth, TSet<FName>& Visited)
    {
        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("package_name"), PackageName.ToString());

        if (Depth <= 0)
        {
            Node->SetArrayField(TEXT("dependencies"), {});
            return Node;
        }

        if (Visited.Contains(PackageName))
        {
            Node->SetBoolField(TEXT("cycle"), true);
            Node->SetArrayField(TEXT("dependencies"), {});
            return Node;
        }

        Visited.Add(PackageName);

        TArray<FName> Dependencies;
        AssetRegistry.GetDependencies(PackageName, Dependencies);

        TArray<TSharedPtr<FJsonValue>> Children;
        Children.Reserve(Dependencies.Num());
        for (const FName& Dependency : Dependencies)
        {
            Children.Add(MakeShared<FJsonValueObject>(BuildDependencyNode(Dependency, AssetRegistry, Depth - 1, Visited)));
        }

        Visited.Remove(PackageName);
        Node->SetArrayField(TEXT("dependencies"), Children);
        return Node;
    }

    // Resolve graph_name + node_id → UEdGraphNode* inside a Blueprint.
    // node_id must be provided; graph_name is optional (searches all graphs when empty).
    UEdGraphNode* FindBreakpointNode(UBlueprint* Blueprint, const FString& GraphName, const FString& NodeId, FString& OutError)
    {
        if (NodeId.IsEmpty())
        {
            OutError = TEXT("node_id is required to identify the target node");
            return nullptr;
        }

        TArray<UEdGraph*> Graphs;
        AppendBlueprintGraphs(Blueprint, Graphs);

        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph)
            {
                continue;
            }
            if (!GraphName.IsEmpty() && !Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                continue;
            }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }
                if (Node->NodeGuid.ToString() == NodeId)
                {
                    return Node;
                }
            }
        }

        if (GraphName.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Node with GUID '%s' not found in blueprint"), *NodeId);
        }
        else
        {
            OutError = FString::Printf(TEXT("Node with GUID '%s' not found in graph '%s'"), *NodeId, *GraphName);
        }
        return nullptr;
    }
} // namespace SmithUEDebug

using namespace SmithUEDebug;

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEDebugCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_get_compile_errors"),
            TEXT("Analysis"),
            TEXT("Compile a Blueprint and return compiler errors and warnings"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true)
            }),
        &HandleBpGetCompileErrors);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_refresh_all_nodes"),
            TEXT("Analysis"),
            TEXT("Reconstruct all nodes in a Blueprint"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true)
            }),
        &HandleBpRefreshAllNodes);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_find_unconnected_pins"),
            TEXT("Analysis"),
            TEXT("Find unconnected Blueprint exec and data pins"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true)
            }),
        &HandleBpFindUnconnectedPins);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_fix_broken_references"),
            TEXT("Analysis"),
            TEXT("Remove non-existent Blueprint variable references and refresh nodes"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true)
            }),
        &HandleBpFixBrokenReferences);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("asset_get_references"),
            TEXT("Analysis"),
            TEXT("Get package dependencies for an asset"),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Asset package path"), true)
            }),
        &HandleAssetGetReferences);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("asset_get_referencers"),
            TEXT("Analysis"),
            TEXT("Get packages that reference an asset"),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Asset package path"), true)
            }),
        &HandleAssetGetReferencers);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("asset_find_orphans"),
            TEXT("Analysis"),
            TEXT("Find assets in a folder with no referencers"),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder path (default: /Game)")),
                FSmithUEToolParam(TEXT("include_types"), TEXT("array"), TEXT("Optional array of asset type names"))
            }),
        &HandleAssetFindOrphans);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("asset_get_dependency_tree"),
            TEXT("Analysis"),
            TEXT("Get a recursive asset dependency tree"),
            {
                FSmithUEToolParam(TEXT("asset_path"), TEXT("string"), TEXT("Asset package path"), true),
                FSmithUEToolParam(TEXT("depth"), TEXT("int"), TEXT("Maximum dependency depth (default: 2)"))
            }),
        &HandleAssetGetDependencyTree);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("asset_validate"),
            TEXT("Analysis"),
            TEXT("Validate that asset paths resolve and load without errors"),
            {
                FSmithUEToolParam(TEXT("asset_paths"), TEXT("array"), TEXT("Array of asset package paths"), true)
            }),
        &HandleAssetValidate);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("find_broken_assets"),
            TEXT("Analysis"),
            TEXT("Scan a content folder for BROKEN assets: (1) missing hard references — the asset depends on a /Game package that no longer exists (dangling ref that will null/error at runtime), and (2) redirectors — stale ObjectRedirector assets that should be fixed up. Fast: uses the Asset Registry graph (no asset loading) unless load_check=true, which additionally loads each candidate to catch packages that fail to load. Scope with folder_path; cap results with max. Returns the broken asset list with per-asset reasons + the specific missing packages."),
            {
                FSmithUEToolParam(TEXT("folder_path"), TEXT("string"), TEXT("Content folder to scan (default: /Game)"), false, TEXT("/Game")),
                FSmithUEToolParam(TEXT("load_check"), TEXT("bool"), TEXT("Also attempt to load each asset to catch load failures (slower). Default false."), false, TEXT("false")),
                FSmithUEToolParam(TEXT("max"), TEXT("int"), TEXT("Max broken assets to return (default 200)"), false, TEXT("200"))
            }),
        &HandleFindBrokenAssets);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("map_check_errors"),
            TEXT("Analysis"),
            TEXT("Run map check on the active editor world"),
            {}),
        &HandleMapCheckErrors);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_set_breakpoint"),
            TEXT("Debug"),
            TEXT("Set/enable a breakpoint on a Blueprint node by NodeGuid."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name (EventGraph / function name)")),
                FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID")),
                FSmithUEToolParam(TEXT("focus"), TEXT("boolean"), TEXT("Open the Blueprint editor and jump to the node after the operation (default true)"))
            }),
        &HandleBpSetBreakpoint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_clear_breakpoint"),
            TEXT("Debug"),
            TEXT("Remove a breakpoint from a Blueprint node by NodeGuid."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name (EventGraph / function name)")),
                FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID")),
                FSmithUEToolParam(TEXT("focus"), TEXT("boolean"), TEXT("Open the Blueprint editor and jump to the node after the operation (default true)"))
            }),
        &HandleBpClearBreakpoint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_list_breakpoints"),
            TEXT("Debug"),
            TEXT("List all breakpoints in a Blueprint with their graph, node GUID, title, and enabled state."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true)
            }),
        &HandleBpListBreakpoints);
}

// ---------------------------------------------------------------------------
// Command: bp_get_compile_errors
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpGetCompileErrors(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!Params->HasField(TEXT("bp_path")) && !Params->HasField(TEXT("blueprint_path")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: bp_path"));
    }

    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BlueprintPath))
        Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
    UBlueprint* Blueprint = LoadBlueprintForDebug(BlueprintPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    TArray<TSharedPtr<FJsonValue>> Messages;
    AddCompilerMessagesFromGraphs(Blueprint, Messages, ErrorCount, WarningCount);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BlueprintPath);
    Data->SetStringField(TEXT("status"), Blueprint->Status == BS_Error ? TEXT("error") : TEXT("ok"));
    Data->SetNumberField(TEXT("error_count"), ErrorCount);
    Data->SetNumberField(TEXT("warning_count"), WarningCount);
    Data->SetArrayField(TEXT("messages"), Messages);

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: bp_refresh_all_nodes
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpRefreshAllNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!Params->HasField(TEXT("bp_path")) && !Params->HasField(TEXT("blueprint_path")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: bp_path"));
    }

    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BlueprintPath))
        Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
    UBlueprint* Blueprint = LoadBlueprintForDebug(BlueprintPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRefreshAllNodes", "SmithUE: Refresh All Blueprint Nodes"));
    Blueprint->Modify();

    int32 GraphCount = 0;
    int32 NodeCount = 0;
    TArray<UEdGraph*> Graphs;
    AppendBlueprintGraphs(Blueprint, Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        ++GraphCount;
        Graph->Modify();
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            Node->Modify();
            Node->ReconstructNode();
            ++NodeCount;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BlueprintPath);
    Data->SetNumberField(TEXT("graph_count"), GraphCount);
    Data->SetNumberField(TEXT("nodes_refreshed"), NodeCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: bp_find_unconnected_pins
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpFindUnconnectedPins(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!Params->HasField(TEXT("bp_path")) && !Params->HasField(TEXT("blueprint_path")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: bp_path"));
    }

    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BlueprintPath))
        Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
    UBlueprint* Blueprint = LoadBlueprintForDebug(BlueprintPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> Pins;
    TArray<UEdGraph*> Graphs;
    AppendBlueprintGraphs(Blueprint, Graphs);

    for (UEdGraph* Graph : Graphs)
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!IsPinWorthReporting(Pin))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("graph"), Graph->GetName());
                PinObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
                PinObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
                PinObj->SetStringField(TEXT("pin_type"), SmithUEDebug::DebugPinTypeToString(Pin->PinType));
                PinObj->SetStringField(TEXT("default_value"), Pin->GetDefaultAsString());
                Pins.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BlueprintPath);
    Data->SetNumberField(TEXT("unconnected_pin_count"), Pins.Num());
    Data->SetArrayField(TEXT("pins"), Pins);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: bp_fix_broken_references
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpFixBrokenReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!Params->HasField(TEXT("bp_path")) && !Params->HasField(TEXT("blueprint_path")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: bp_path"));
    }

    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BlueprintPath))
        Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
    UBlueprint* Blueprint = LoadBlueprintForDebug(BlueprintPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpFixBrokenReferences", "SmithUE: Fix Broken Blueprint References"));
    Blueprint->Modify();

    const int32 InvalidVariablesBefore = CountInvalidVariables(Blueprint);
    FBlueprintEditorUtils::RefreshVariables(Blueprint);

    int32 NodesRefreshed = 0;
    TArray<UEdGraph*> Graphs;
    AppendBlueprintGraphs(Blueprint, Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        Graph->Modify();
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            Node->Modify();
            Node->ReconstructNode();
            ++NodesRefreshed;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const int32 InvalidVariablesAfter = CountInvalidVariables(Blueprint);

    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    TArray<TSharedPtr<FJsonValue>> Messages;
    AddCompilerMessagesFromGraphs(Blueprint, Messages, ErrorCount, WarningCount);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BlueprintPath);
    Data->SetNumberField(TEXT("invalid_variables_before"), InvalidVariablesBefore);
    Data->SetNumberField(TEXT("invalid_variables_after"), InvalidVariablesAfter);
    Data->SetNumberField(TEXT("nodes_refreshed"), NodesRefreshed);
    Data->SetNumberField(TEXT("error_count"), ErrorCount);
    Data->SetNumberField(TEXT("warning_count"), WarningCount);
    Data->SetArrayField(TEXT("compile_messages"), Messages);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: asset_get_references
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleAssetGetReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    FAssetData AssetData;
    if (!GetAssetDataForPath(AssetPath, AssetData, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    TArray<FName> Dependencies;
    SmithUEDebug::GetDebugAssetRegistry().GetDependencies(AssetData.PackageName, Dependencies);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetNumberField(TEXT("reference_count"), Dependencies.Num());
    Data->SetArrayField(TEXT("references"), NameArrayToJson(Dependencies));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: asset_get_referencers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleAssetGetReferencers(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    FAssetData AssetData;
    if (!GetAssetDataForPath(AssetPath, AssetData, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    TArray<FName> Referencers;
    SmithUEDebug::GetDebugAssetRegistry().GetReferencers(AssetData.PackageName, Referencers);
    Referencers.RemoveAll([&AssetData](const FName& Ref) { return Ref == AssetData.PackageName; });

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetNumberField(TEXT("referencer_count"), Referencers.Num());
    Data->SetArrayField(TEXT("referencers"), NameArrayToJson(Referencers));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: asset_find_orphans
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleAssetFindOrphans(const TSharedPtr<FJsonObject>& Params)
{
    FString FolderPath = TEXT("/Game");
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("folder_path"), FolderPath);
    }
    if (FolderPath.IsEmpty())
    {
        FolderPath = TEXT("/Game");
    }

    TSet<FName> IncludedTypes;
    const TArray<TSharedPtr<FJsonValue>>* IncludeTypes = nullptr;
    if (Params.IsValid() && Params->TryGetArrayField(TEXT("include_types"), IncludeTypes) && IncludeTypes)
    {
        for (const TSharedPtr<FJsonValue>& TypeValue : *IncludeTypes)
        {
            if (TypeValue.IsValid() && !TypeValue->AsString().IsEmpty())
            {
                IncludedTypes.Add(FName(*TypeValue->AsString()));
            }
        }
    }

    IAssetRegistry& AssetRegistry = SmithUEDebug::GetDebugAssetRegistry();
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;
    if (IncludedTypes.Num() > 0)
    {
        for (const FName& TypeName : IncludedTypes)
        {
            Filter.ClassPaths.Add(ClassNameToClassPath(TypeName.ToString()));
        }
        Filter.bRecursiveClasses = true;
    }

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Orphans;
    for (const FAssetData& AssetData : Assets)
    {
        TArray<FName> Referencers;
        AssetRegistry.GetReferencers(AssetData.PackageName, Referencers);
        Referencers.RemoveAll([&AssetData](const FName& Ref) { return Ref == AssetData.PackageName; });
        if (Referencers.Num() > 0)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Orphan = MakeShared<FJsonObject>();
        Orphan->SetStringField(TEXT("asset_path"), AssetData.PackageName.ToString());
        Orphan->SetStringField(TEXT("object_path"), AssetData.GetObjectPathString());
        Orphan->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
        Orphan->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
        Orphans.Add(MakeShared<FJsonValueObject>(Orphan));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("folder_path"), FolderPath);
    Data->SetNumberField(TEXT("asset_count"), Assets.Num());
    Data->SetNumberField(TEXT("orphan_count"), Orphans.Num());
    Data->SetArrayField(TEXT("orphans"), Orphans);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: asset_get_dependency_tree
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleAssetGetDependencyTree(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString AssetPath;
    Params->TryGetStringField(TEXT("asset_path"), AssetPath);

    double DepthValue = 2.0;
    Params->TryGetNumberField(TEXT("depth"), DepthValue);
    const int32 Depth = FMath::Clamp(static_cast<int32>(DepthValue), 0, 12);

    FAssetData AssetData;
    if (!GetAssetDataForPath(AssetPath, AssetData, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    TSet<FName> Visited;
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetNumberField(TEXT("depth"), Depth);
    Data->SetObjectField(TEXT("tree"), SmithUEDebug::BuildDependencyNode(AssetData.PackageName, SmithUEDebug::GetDebugAssetRegistry(), Depth, Visited));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: asset_validate
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleAssetValidate(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("asset_paths")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* AssetPaths = nullptr;
    if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPaths) || !AssetPaths || AssetPaths->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("asset_paths must be a non-empty array"));
    }

    int32 ValidCount = 0;
    TArray<TSharedPtr<FJsonValue>> Results;
    Results.Reserve(AssetPaths->Num());

    for (const TSharedPtr<FJsonValue>& PathValue : *AssetPaths)
    {
        const FString AssetPath = PathValue.IsValid() ? PathValue->AsString() : FString();
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);

        FAssetData AssetData;
        FString AssetError;
        if (!GetAssetDataForPath(AssetPath, AssetData, AssetError) || !AssetData.IsValid())
        {
            Result->SetBoolField(TEXT("valid"), false);
            Result->SetStringField(TEXT("error"), AssetError.IsEmpty() ? TEXT("Invalid asset data") : AssetError);
            Results.Add(MakeShared<FJsonValueObject>(Result));
            continue;
        }

        UObject* Asset = AssetData.GetAsset();
        if (!Asset)
        {
            Result->SetBoolField(TEXT("valid"), false);
            Result->SetStringField(TEXT("error"), TEXT("Asset data is valid, but asset failed to load"));
            Results.Add(MakeShared<FJsonValueObject>(Result));
            continue;
        }

        Result->SetBoolField(TEXT("valid"), true);
        Result->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
        Result->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
        ++ValidCount;
        Results.Add(MakeShared<FJsonValueObject>(Result));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("checked_count"), AssetPaths->Num());
    Data->SetNumberField(TEXT("valid_count"), ValidCount);
    Data->SetNumberField(TEXT("invalid_count"), AssetPaths->Num() - ValidCount);
    Data->SetArrayField(TEXT("results"), Results);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: find_broken_assets
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleFindBrokenAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString FolderPath = TEXT("/Game");
    bool bLoadCheck = false;
    double MaxD = 200.0;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("folder_path"), FolderPath);
        Params->TryGetBoolField(TEXT("load_check"), bLoadCheck);
        Params->TryGetNumberField(TEXT("max"), MaxD);
    }
    if (FolderPath.IsEmpty()) { FolderPath = TEXT("/Game"); }
    const int32 MaxResults = FMath::Clamp(static_cast<int32>(MaxD), 1, 5000);

    IAssetRegistry& AssetRegistry = SmithUEDebug::GetDebugAssetRegistry();
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Broken;
    int32 RedirectorCount = 0, MissingRefCount = 0, LoadFailCount = 0;

    for (const FAssetData& AssetData : Assets)
    {
        if (Broken.Num() >= MaxResults) { break; }

        TArray<FString> Reasons;
        TArray<TSharedPtr<FJsonValue>> MissingRefs;

        if (AssetData.IsRedirector())
        {
            Reasons.Add(TEXT("redirector"));
            ++RedirectorCount;
        }

        // Missing hard references: any /Game dependency package that has no assets in the registry.
        TArray<FName> Deps;
        AssetRegistry.GetDependencies(AssetData.PackageName, Deps, UE::AssetRegistry::EDependencyCategory::Package);
        for (const FName& Dep : Deps)
        {
            const FString DepStr = Dep.ToString();
            if (!DepStr.StartsWith(TEXT("/Game/"))) { continue; } // only high-signal project content
            if (Dep == AssetData.PackageName) { continue; }
            TArray<FAssetData> DepAssets;
            AssetRegistry.GetAssetsByPackageName(Dep, DepAssets, /*bIncludeOnlyOnDiskAssets*/ true);
            if (DepAssets.Num() == 0)
            {
                MissingRefs.Add(MakeShared<FJsonValueString>(DepStr));
            }
        }
        if (MissingRefs.Num() > 0)
        {
            Reasons.Add(TEXT("missing_references"));
            ++MissingRefCount;
        }

        // Optional deep check: actually load the asset to catch package/load failures.
        if (bLoadCheck && !AssetData.IsRedirector())
        {
            UObject* Obj = AssetData.GetAsset();
            if (!Obj)
            {
                Reasons.Add(TEXT("load_failed"));
                ++LoadFailCount;
            }
        }

        if (Reasons.Num() == 0) { continue; }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("asset_path"), AssetData.PackageName.ToString());
        Entry->SetStringField(TEXT("object_path"), AssetData.GetObjectPathString());
        Entry->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
        TArray<TSharedPtr<FJsonValue>> ReasonArr;
        for (const FString& R : Reasons) { ReasonArr.Add(MakeShared<FJsonValueString>(R)); }
        Entry->SetArrayField(TEXT("reasons"), ReasonArr);
        if (MissingRefs.Num() > 0) { Entry->SetArrayField(TEXT("missing_references"), MissingRefs); }
        Broken.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("folder_path"), FolderPath);
    Data->SetBoolField(TEXT("load_check"), bLoadCheck);
    Data->SetNumberField(TEXT("scanned_count"), Assets.Num());
    Data->SetNumberField(TEXT("broken_count"), Broken.Num());
    Data->SetNumberField(TEXT("redirector_count"), RedirectorCount);
    Data->SetNumberField(TEXT("missing_reference_count"), MissingRefCount);
    Data->SetNumberField(TEXT("load_failed_count"), LoadFailCount);
    Data->SetBoolField(TEXT("truncated"), Broken.Num() >= MaxResults);
    Data->SetArrayField(TEXT("broken_assets"), Broken);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleMapCheckErrors(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor not available"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Editor world not available"));
    }

    GEditor->Exec(World, TEXT("MAP CHECK"));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("world"), World->GetName());
    Data->SetStringField(TEXT("message"), TEXT("Map check completed. Open the Message Log (MapCheck category) in the editor for detailed results."));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: bp_set_breakpoint
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpSetBreakpoint(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);

    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    FString NodeId;
    Params->TryGetStringField(TEXT("node_id"), NodeId);

    UBlueprint* Blueprint = LoadBlueprintForDebug(BpPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    UEdGraphNode* Node = SmithUEDebug::FindBreakpointNode(Blueprint, GraphName, NodeId, Error);
    if (!Node)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

#if WITH_EDITOR
    FBlueprintBreakpoint* Existing = FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint);
    if (Existing)
    {
        FKismetDebugUtilities::SetBreakpointEnabled(*Existing, true);
    }
    else
    {
        FKismetDebugUtilities::CreateBreakpoint(Blueprint, Node, /*bIsEnabled=*/true);
        // breakpoint created enabled by CreateBreakpoint above
    }
    Blueprint->Modify();
    Blueprint->MarkPackageDirty();
#endif // WITH_EDITOR

#if WITH_EDITOR
    bool bFocus = true;
    Params->TryGetBoolField(TEXT("focus"), bFocus);
    if (bFocus && GEditor)
    {
        if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            AssetEditorSub->OpenEditorForAsset(Blueprint);
        }
        TSharedPtr<IBlueprintEditor> BpEditor =
            FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, /*bOpenEditor=*/true);
        if (BpEditor.IsValid())
        {
            BpEditor->JumpToHyperlink(Node, /*bRequestRename=*/false);
        }
    }
#endif // WITH_EDITOR

    UEdGraph* OwningGraph = Node->GetGraph();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("graph"), OwningGraph ? OwningGraph->GetName() : TEXT(""));
    Data->SetStringField(TEXT("node_id"), NodeId);
    Data->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Data->SetBoolField(TEXT("enabled"), true);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: bp_clear_breakpoint
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpClearBreakpoint(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);

    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    FString NodeId;
    Params->TryGetStringField(TEXT("node_id"), NodeId);

    UBlueprint* Blueprint = LoadBlueprintForDebug(BpPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    UEdGraphNode* Node = SmithUEDebug::FindBreakpointNode(Blueprint, GraphName, NodeId, Error);
    if (!Node)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    bool bWasPresent = false;
#if WITH_EDITOR
    if (FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint) != nullptr)
    {
        FKismetDebugUtilities::RemoveBreakpointFromNode(Node, Blueprint);
        Blueprint->Modify();
        Blueprint->MarkPackageDirty();
        bWasPresent = true;
    }
#endif // WITH_EDITOR

#if WITH_EDITOR
    bool bFocus = true;
    Params->TryGetBoolField(TEXT("focus"), bFocus);
    if (bFocus && GEditor)
    {
        if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            AssetEditorSub->OpenEditorForAsset(Blueprint);
        }
        TSharedPtr<IBlueprintEditor> BpEditor =
            FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, /*bOpenEditor=*/true);
        if (BpEditor.IsValid())
        {
            BpEditor->JumpToHyperlink(Node, /*bRequestRename=*/false);
        }
    }
#endif // WITH_EDITOR

    UEdGraph* OwningGraph = Node->GetGraph();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("graph"), OwningGraph ? OwningGraph->GetName() : TEXT(""));
    Data->SetStringField(TEXT("node_id"), NodeId);
    Data->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Data->SetBoolField(TEXT("was_present"), bWasPresent);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command: bp_list_breakpoints
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEDebugCommands::HandleBpListBreakpoints(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);

    UBlueprint* Blueprint = LoadBlueprintForDebug(BpPath);
    if (!Blueprint)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    TArray<TSharedPtr<FJsonValue>> BreakpointArray;
#if WITH_EDITOR
    FKismetDebugUtilities::ForeachBreakpoint(Blueprint, [&BreakpointArray](FBlueprintBreakpoint& Breakpoint)
    {
        UEdGraphNode* BPNode = Breakpoint.GetLocation();
        if (!BPNode)
        {
            return;
        }

        UEdGraph* OwningGraph = BPNode->GetGraph();
        TSharedPtr<FJsonObject> BPObj = MakeShared<FJsonObject>();
        BPObj->SetStringField(TEXT("graph"), OwningGraph ? OwningGraph->GetName() : TEXT(""));
        BPObj->SetStringField(TEXT("node_id"), BPNode->NodeGuid.ToString());
        BPObj->SetStringField(TEXT("node_title"), BPNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
        BPObj->SetBoolField(TEXT("enabled"), Breakpoint.IsEnabled());
        BreakpointArray.Add(MakeShared<FJsonValueObject>(BPObj));
    });
#endif // WITH_EDITOR

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetNumberField(TEXT("breakpoint_count"), BreakpointArray.Num());
    Data->SetArrayField(TEXT("breakpoints"), BreakpointArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

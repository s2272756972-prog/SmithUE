// Copyright 2026, 123dx-svg. MIT License.

#include "Blueprint/SmithUEBpAtomicAPI.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/SmithUEBpAtomicAPIHelpers.h"
#include "Components/ActorComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputKey.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "UObject/Package.h"
#include "Utils/SmithUECommonUtils.h"

using namespace SmithUEBpAtomicAPIHelpers;

void FSmithUEBpAtomicAPI::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(FSmithUEToolSchema(TEXT("bp_create"), TEXT("Blueprint"), TEXT("Create a new Blueprint asset"), { FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Blueprint asset name"), true), FSmithUEToolParam(TEXT("parent_class"), TEXT("string"), TEXT("Parent class name"), true), FSmithUEToolParam(TEXT("save_path"), TEXT("string"), TEXT("Destination content path"), true) }), &HandleBpCreate);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_function"), TEXT("Blueprint"), TEXT("Add a function graph to a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("New function name"), true), FSmithUEToolParam(TEXT("inputs"), TEXT("array"), TEXT("Optional input pin definitions"), false, FString(), TEXT("object")), FSmithUEToolParam(TEXT("outputs"), TEXT("array"), TEXT("Optional output pin definitions"), false, FString(), TEXT("object")) }), &HandleBpAddFunction);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_create_node"), TEXT("Blueprint"), TEXT("Create a node inside a Blueprint graph"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_class"), TEXT("string"), TEXT("Node class name"), true), FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("Optional {x,y} node position")) }), &HandleBpCreateNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_connect_pins"), TEXT("Blueprint"), TEXT("Connect two Blueprint node pins"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("source_node_id"), TEXT("string"), TEXT("Source node GUID"), true), FSmithUEToolParam(TEXT("source_pin"), TEXT("string"), TEXT("Source pin name"), true), FSmithUEToolParam(TEXT("target_node_id"), TEXT("string"), TEXT("Target node GUID"), true), FSmithUEToolParam(TEXT("target_pin"), TEXT("string"), TEXT("Target pin name"), true) }), &HandleBpConnectPins);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_pin_default"), TEXT("Blueprint"), TEXT("Set a Blueprint node pin default value"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"), true), FSmithUEToolParam(TEXT("pin_name"), TEXT("string"), TEXT("Pin name"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Default value string"), true) }), &HandleBpSetPinDefault);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_delete_node"), TEXT("Blueprint"), TEXT("Delete a node from a Blueprint graph"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"), true) }), &HandleBpDeleteNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_variable"), TEXT("Blueprint"), TEXT("Add a Blueprint member variable"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Variable name"), true), FSmithUEToolParam(TEXT("var_type"), TEXT("string"), TEXT("Variable type name"), true), FSmithUEToolParam(TEXT("default_value"), TEXT("string"), TEXT("Optional default value")), FSmithUEToolParam(TEXT("category"), TEXT("string"), TEXT("Optional category name")) }), &HandleBpAddVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_component"), TEXT("Blueprint"), TEXT("Add a component to a Blueprint SCS"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component_class"), TEXT("string"), TEXT("Component class name"), true), FSmithUEToolParam(TEXT("component_name"), TEXT("string"), TEXT("Component instance name"), true), FSmithUEToolParam(TEXT("static_mesh"), TEXT("string"), TEXT("Optional StaticMesh asset path for StaticMeshComponent"), false) }), &HandleBpAddComponent);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_component_property"), TEXT("Blueprint"), TEXT("Set a property on a Blueprint SCS or inherited component template"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component_name"), TEXT("string"), TEXT("Component name (SCS or inherited)"), true), FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name, or 'PostProcessMaterial' to add a blendable material"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Property value (string/number/bool), or material asset path for PostProcessMaterial"), true) }), &HandleBpSetComponentProperty);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_override_function"), TEXT("Blueprint"), TEXT("Override a parent class function in a Blueprint (creates proper override graph with correct signature)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Parent function name to override"), true) }), &HandleBpOverrideFunction);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_compile"), TEXT("Blueprint"), TEXT("Compile a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true) }), &HandleBpCompile);
}

UBlueprint* FSmithUEBpAtomicAPI::LoadBlueprint(const FString& BpPath)
{
	return BpPath.IsEmpty() ? nullptr : LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(BpPath));
}

UEdGraph* FSmithUEBpAtomicAPI::FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}
	return nullptr;
}

FString FSmithUEBpAtomicAPI::CreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeClass, FVector2D Position, const TSharedPtr<FJsonObject>& ExtraParams)
{
	if (!Blueprint || !Graph)
	{
		return FString();
	}
	UClass* NodeType = ResolveClassByName(NodeClass, UEdGraphNode::StaticClass(), TEXT('U'));
	if (!NodeType)
	{
		return FString();
	}
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeType);
	if (!NewNode)
	{
		return FString();
	}
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		FString FunctionName;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("function_name"), FunctionName))
		{
			return FString();
		}
		UFunction* Function = FindFunctionByName(FunctionName);
		if (!Function)
		{
			return FString();
		}
		CallNode->SetFromFunction(Function);
	}
	else if (UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(NewNode))
	{
		FString VariableName;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("variable_name"), VariableName)) { return FString(); }
		VariableGetNode->VariableReference.SetSelfMember(FName(*VariableName));
	}
	else if (UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(NewNode))
	{
		FString VariableName;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("variable_name"), VariableName)) { return FString(); }
		VariableSetNode->VariableReference.SetSelfMember(FName(*VariableName));
	}
	else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(NewNode))
	{
		FString MacroPath;
		if (!ExtraParams.IsValid() || !ExtraParams->TryGetStringField(TEXT("macro_path"), MacroPath)) { return FString(); }
		if (UEdGraph* MacroGraph = ResolveMacroGraph(MacroPath)) { MacroNode->SetMacroGraph(MacroGraph); } else { return FString(); }
	}
	else if (UK2Node_InputKey* InputKeyNode = Cast<UK2Node_InputKey>(NewNode))
	{
		FString KeyName;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("key"), KeyName))
		{
			InputKeyNode->InputKey = FKey(*KeyName);
		}
	}
	Graph->Modify();
	NewNode->SetFlags(RF_Transactional);
	NewNode->CreateNewGuid();
	NewNode->NodePosX = FMath::RoundToInt(Position.X);
	NewNode->NodePosY = FMath::RoundToInt(Position.Y);
	Graph->AddNode(NewNode, false, false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();
	NewNode->ReconstructNode();
	// Mark as modified (non-structural) — the caller is responsible for a single
	// CompileBlueprint at the end. Structural marking here would trigger a full
	// skeleton recompile + actor reinstancing for every single node creation,
	// corrupting FTickFunction vtables on live actors.
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return NewNode->NodeGuid.ToString();
}

bool FSmithUEBpAtomicAPI::ConnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	FString Error;
	return TryConnectPins(Blueprint, Graph, SourceNodeId, SourcePinName, TargetNodeId, TargetPinName, Error);
}

bool FSmithUEBpAtomicAPI::SetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value)
{
	FString Error;
	return TrySetPinDefault(Blueprint, Graph, NodeId, PinName, Value, Error);
}

bool FSmithUEBpAtomicAPI::CompileBlueprint(UBlueprint* Blueprint, TArray<FString>& OutErrors, bool bSkipGarbageCollection)
{
	OutErrors.Reset();
	if (!Blueprint)
	{
		OutErrors.Add(TEXT("Blueprint is null"));
		return false;
	}
	EBlueprintCompileOptions CompileFlags = EBlueprintCompileOptions::None;
	if (bSkipGarbageCollection)
	{
		CompileFlags |= EBlueprintCompileOptions::SkipGarbageCollection;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileFlags);
	if (Blueprint->Status == BS_Error)
	{
		// Collect actual error messages from graph nodes
		auto CollectNodeErrors = [&OutErrors](const TArray<TObjectPtr<UEdGraph>>& Graphs)
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && Node->bHasCompilerMessage && Node->ErrorType <= (int32)EMessageSeverity::Error)
					{
						OutErrors.Add(FString::Printf(TEXT("[%s] %s"),
							*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
							*Node->ErrorMsg));
					}
				}
			}
		};
		CollectNodeErrors(Blueprint->UbergraphPages);
		CollectNodeErrors(Blueprint->FunctionGraphs);
		if (OutErrors.Num() == 0)
		{
			OutErrors.Add(TEXT("Blueprint compilation failed (no specific error messages available)"));
		}
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCreate(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("parent_class"), TEXT("save_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name; FString ParentClassName; FString SavePath;
	Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("parent_class"), ParentClassName); Params->TryGetStringField(TEXT("save_path"), SavePath);
	UClass* ParentClass = ResolveClassByName(ParentClassName, UObject::StaticClass(), TEXT('A'));
	if (!ParentClass) { ParentClass = ResolveClassByName(ParentClassName, UObject::StaticClass(), TEXT('U')); }
	if (!ParentClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *ParentClassName)); }
	const FString CleanSavePath = SavePath.EndsWith(TEXT("/")) ? SavePath.LeftChop(1) : SavePath;
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *CleanSavePath, *Name);
	if (LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(PackagePath))) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint already exists at target path")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCreate", "SmithUE: Create Blueprint"));
	UPackage* Package = CreatePackage(*PackagePath);
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!NewBlueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create Blueprint")); }
	FAssetRegistryModule::AssetCreated(NewBlueprint);
	NewBlueprint->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), PackagePath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString FunctionName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("function_name"), FunctionName);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (FindGraph(Blueprint, FunctionName)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph already exists")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddFunction", "SmithUE: Add Blueprint Function"));
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, true, (UFunction*)nullptr);
	UK2Node_FunctionEntry* EntryNode = nullptr; UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes) { EntryNode = EntryNode ? EntryNode : Cast<UK2Node_FunctionEntry>(Node); ResultNode = ResultNode ? ResultNode : Cast<UK2Node_FunctionResult>(Node); }
	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr; const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
	Params->TryGetArrayField(TEXT("inputs"), Inputs); Params->TryGetArrayField(TEXT("outputs"), Outputs);
	if (Inputs && !EntryNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Function entry node not found")); }
	if (Outputs && Outputs->Num() > 0 && !ResultNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Function result node not found")); }
	if (Inputs)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Inputs)
		{
			const TSharedPtr<FJsonObject> PinObject = Value.IsValid() ? Value->AsObject() : nullptr;
			FString PinName; FString PinTypeName; FEdGraphPinType PinType;
			if (!GetNamedTypeField(PinObject, PinName, PinTypeName) || !ResolvePinType(PinTypeName, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid function input definition")); }
			EntryNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output);
		}
	}
	if (Outputs)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Outputs)
		{
			const TSharedPtr<FJsonObject> PinObject = Value.IsValid() ? Value->AsObject() : nullptr;
			FString PinName; FString PinTypeName; FEdGraphPinType PinType;
			if (!GetNamedTypeField(PinObject, PinName, PinTypeName) || !ResolvePinType(PinTypeName, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid function output definition")); }
			ResultNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Input);
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("graph_name"), FunctionName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCreateNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_class") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString NodeClass;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("node_class"), NodeClass);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCreateNode", "SmithUE: Create Blueprint Node"));
	const FString NodeId = CreateNode(Blueprint, Graph, NodeClass, GetPositionFromJson(Params), Params);
	return NodeId.IsEmpty() ? FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create node")) : MakeNodeResponse(NodeId);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("source_node_id"), TEXT("source_pin"), TEXT("target_node_id"), TEXT("target_pin") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString SourceNodeId; FString SourcePin; FString TargetNodeId; FString TargetPin;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId); Params->TryGetStringField(TEXT("source_pin"), SourcePin); Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId); Params->TryGetStringField(TEXT("target_pin"), TargetPin);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpConnectPins", "SmithUE: Connect Blueprint Pins"));
	if (!TryConnectPins(Blueprint, Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("connected"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetPinDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id"), TEXT("pin_name"), TEXT("value") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString NodeId; FString PinName; FString Value;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("node_id"), NodeId); Params->TryGetStringField(TEXT("pin_name"), PinName); Params->TryGetStringField(TEXT("value"), Value);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetPinDefault", "SmithUE: Set Blueprint Pin Default"));
	if (!TrySetPinDefault(Blueprint, Graph, NodeId, PinName, Value, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("updated"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpDeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("node_id") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString GraphName; FString NodeId;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("graph_name"), GraphName); Params->TryGetStringField(TEXT("node_id"), NodeId);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph not found")); }
	UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
	if (!Node) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Node not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpDeleteNode", "SmithUE: Delete Blueprint Node"));
	Node->BreakAllNodeLinks();
	Graph->RemoveNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("deleted"), true);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("var_name"), TEXT("var_type") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString VarName; FString VarTypeName; FString DefaultValue; FString Category;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("var_name"), VarName); Params->TryGetStringField(TEXT("var_type"), VarTypeName); Params->TryGetStringField(TEXT("default_value"), DefaultValue); Params->TryGetStringField(TEXT("category"), Category);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	FEdGraphPinType PinType;
	if (!ResolvePinType(VarTypeName, PinType)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Unsupported variable type")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddVariable", "SmithUE: Add Blueprint Variable"));
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType, DefaultValue)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to add variable")); }
	if (!Category.IsEmpty()) { FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*VarName), nullptr, FText::FromString(Category)); }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("var_name"), VarName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpAddComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component_class"), TEXT("component_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString ComponentClassName; FString ComponentName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("component_class"), ComponentClassName); Params->TryGetStringField(TEXT("component_name"), ComponentName);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (!Blueprint->SimpleConstructionScript) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript")); }
	UClass* ComponentClass = ResolveClassByName(ComponentClassName, UActorComponent::StaticClass(), TEXT('U'));
	if (!ComponentClass) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Component class not found")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddComponent", "SmithUE: Add Blueprint Component"));
	USCS_Node* SCSNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ComponentName));
	if (!SCSNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create component node")); }
	Blueprint->SimpleConstructionScript->AddNode(SCSNode);

	// Optional: set static mesh on StaticMeshComponent template
	FString StaticMeshPath;
	if (Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath) && !StaticMeshPath.IsEmpty())
	{
		if (UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(SCSNode->ComponentTemplate))
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
			if (Mesh) { MeshComp->SetStaticMesh(Mesh); }
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("component_name"), ComponentName);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_set_component_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component_name"), TEXT("property_name"), TEXT("value") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, ComponentName, PropertyName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("component_name"), ComponentName);
	Params->TryGetStringField(TEXT("property_name"), PropertyName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path"));
	}

	// --- Find component template ---
	UActorComponent* TargetComp = nullptr;

	// 1) Check SCS templates
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate)
			{
				FString NodeVarName = Node->GetVariableName().ToString();
				FString TemplateName = Node->ComponentTemplate->GetName();
				if (NodeVarName.Equals(ComponentName, ESearchCase::IgnoreCase) ||
					TemplateName.Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					TargetComp = Node->ComponentTemplate;
					break;
				}
			}
		}
	}

	// 2) Check CDO components (inherited from C++ parent)
	if (!TargetComp)
	{
		AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		if (CDO)
		{
			for (UActorComponent* Comp : CDO->GetComponents())
			{
				if (Comp && Comp->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					TargetComp = Comp;
					break;
				}
			}
		}
	}

	if (!TargetComp)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Component not found: '%s'"), *ComponentName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpSetCompProp", "SmithUE: Set BP Component Property"));
	Blueprint->Modify();
	TargetComp->Modify();

	// --- Special handling: PostProcessMaterial ---
	if (PropertyName.Equals(TEXT("PostProcessMaterial"), ESearchCase::IgnoreCase))
	{
		FString MaterialPath;
		Params->TryGetStringField(TEXT("value"), MaterialPath);

		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (!Material)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Material not found: '%s'"), *MaterialPath));
		}

		// Works for both UCameraComponent and UPostProcessComponent
		FPostProcessSettings* PPSettings = nullptr;
		float* BlendWeightPtr = nullptr;

		if (UCameraComponent* Camera = Cast<UCameraComponent>(TargetComp))
		{
			PPSettings = &Camera->PostProcessSettings;
			BlendWeightPtr = &Camera->PostProcessBlendWeight;
		}
		else if (UPostProcessComponent* PPComp = Cast<UPostProcessComponent>(TargetComp))
		{
			PPSettings = &PPComp->Settings;
			BlendWeightPtr = &PPComp->BlendWeight;
		}

		if (!PPSettings)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				TEXT("Component is not a CameraComponent or PostProcessComponent"));
		}

		FWeightedBlendable Entry;
		Entry.Weight = 1.0f;
		Entry.Object = Material;
		PPSettings->WeightedBlendables.Array.Add(Entry);

		if (BlendWeightPtr)
		{
			*BlendWeightPtr = 1.0f;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Blueprint->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("component_name"), ComponentName);
		Data->SetStringField(TEXT("property_name"), TEXT("PostProcessMaterial"));
		Data->SetStringField(TEXT("material_path"), MaterialPath);
		Data->SetNumberField(TEXT("blendable_count"), PPSettings->WeightedBlendables.Array.Num());
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	// --- General property setting ---
	FProperty* Prop = TargetComp->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property not found: '%s' on component '%s' (class %s)"),
				*PropertyName, *ComponentName, *TargetComp->GetClass()->GetName()));
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetComp);

	FString BeforeValue;
	Prop->ExportTextItem_Direct(BeforeValue, PropAddr, nullptr, TargetComp, PPF_None);

	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("value"));
	FString ValueStr;
	if (JsonValue.IsValid())
	{
		if (JsonValue->Type == EJson::String) ValueStr = JsonValue->AsString();
		else if (JsonValue->Type == EJson::Number) ValueStr = FString::SanitizeFloat(JsonValue->AsNumber());
		else if (JsonValue->Type == EJson::Boolean) ValueStr = JsonValue->AsBool() ? TEXT("True") : TEXT("False");
	}

	bool bSuccess = false;
	if (!ValueStr.IsEmpty())
	{
		const TCHAR* Result = Prop->ImportText(*ValueStr, PropAddr, 0, TargetComp);
		bSuccess = (Result != nullptr);
	}

	if (!bSuccess && Prop->IsA<FBoolProperty>())
	{
		CastField<FBoolProperty>(Prop)->SetPropertyValue(PropAddr, JsonValue.IsValid() && JsonValue->AsBool());
		bSuccess = true;
	}

	if (!bSuccess)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set property '%s' on component '%s'"),
				*PropertyName, *ComponentName));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();

	FString AfterValue;
	Prop->ExportTextItem_Direct(AfterValue, PropAddr, nullptr, TargetComp, PPF_None);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("component_name"), ComponentName);
	Data->SetStringField(TEXT("property_name"), PropertyName);
	Data->SetStringField(TEXT("before"), BeforeValue);
	Data->SetStringField(TEXT("after"), AfterValue);
	Data->SetBoolField(TEXT("changed"), BeforeValue != AfterValue);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_override_function
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpOverrideFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("function_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, FunctionName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("function_name"), FunctionName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path"));
	}

	// Check if override graph already exists
	if (FindGraph(Blueprint, FunctionName))
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Override graph already exists for '%s'"), *FunctionName));
	}

	// Find the parent function to override
	UClass* ParentClass = Blueprint->ParentClass;
	if (!ParentClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no parent class"));
	}

	UFunction* ParentFunction = ParentClass->FindFunctionByName(FName(*FunctionName));
	if (!ParentFunction)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Function '%s' not found in parent class '%s' or its ancestors"),
				*FunctionName, *ParentClass->GetName()));
	}

	// Verify the function is overridable (BlueprintNativeEvent or BlueprintImplementableEvent)
	if (!ParentFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Function '%s' is not a BlueprintEvent and cannot be overridden in Blueprint"),
				*FunctionName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpOverrideFunction", "SmithUE: Override Blueprint Function"));

	// Create the override graph using UClass* overload — this properly sets up:
	// - Entry node with FunctionReference pointing to parent class
	// - CallParentFunction node (calls Super)
	// - Result node with correct pin types from parent signature
	// - All exec/data pins auto-connected
	UClass* FunctionOwnerClass = ParentFunction->GetOwnerClass();
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/false, FunctionOwnerClass);

	// Find Entry/Result nodes created by the engine for response reporting
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (!EntryNode) { EntryNode = Cast<UK2Node_FunctionEntry>(Node); }
		if (!ResultNode) { ResultNode = Cast<UK2Node_FunctionResult>(Node); }
	}

	// Build response with function signature info
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("graph_name"), FunctionName);
	Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());

	// Report the pins on entry/result for caller reference
	if (EntryNode)
	{
		TArray<TSharedPtr<FJsonValue>> InputPins;
		for (UEdGraphPin* Pin : EntryNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				FString Type = Pin->PinType.PinCategory.ToString();
				if (Pin->PinType.PinSubCategoryObject != nullptr)
				{
					Type += TEXT("/");
					Type += Pin->PinType.PinSubCategoryObject->GetName();
				}
				PinObj->SetStringField(TEXT("type"), Type);
				InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
			}
		}
		if (InputPins.Num() > 0)
		{
			Data->SetArrayField(TEXT("inputs"), InputPins);
		}
	}
	if (ResultNode)
	{
		TArray<TSharedPtr<FJsonValue>> OutputPins;
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				FString Type = Pin->PinType.PinCategory.ToString();
				if (Pin->PinType.PinSubCategoryObject != nullptr)
				{
					Type += TEXT("/");
					Type += Pin->PinType.PinSubCategoryObject->GetName();
				}
				PinObj->SetStringField(TEXT("type"), Type);
				OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
			}
		}
		if (OutputPins.Num() > 0)
		{
			Data->SetArrayField(TEXT("outputs"), OutputPins);
		}
	}

	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCompile(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCompile", "SmithUE: Compile Blueprint"));
	TArray<FString> CompileErrors;
	const bool bCompiled = CompileBlueprint(Blueprint, CompileErrors);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("compiled"), bCompiled);
	Data->SetBoolField(TEXT("has_errors"), CompileErrors.Num() > 0);
	AppendJsonStringArray(Data, TEXT("errors"), CompileErrors);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

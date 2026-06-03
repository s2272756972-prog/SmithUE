// Copyright 2026, 123dx-svg. MIT License.

#include "Blueprint/SmithUEBpAtomicAPI.h"

#include "SmithUEModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/SmithUEBpAtomicAPIHelpers.h"
#include "Components/ActorComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Editor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputKey.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "InputAction.h"
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
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_function"), TEXT("Blueprint"), TEXT("Add a function graph to a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("New function name"), true), FSmithUEToolParam(TEXT("inputs"), TEXT("array"), TEXT("Optional input pin definitions"), false, FString(), TEXT("object")), FSmithUEToolParam(TEXT("outputs"), TEXT("array"), TEXT("Optional output pin definitions"), false, FString(), TEXT("object")) }), &HandleBpAddFunction);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_create_node"), TEXT("Blueprint"), TEXT("Create a node inside a Blueprint graph"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_class"), TEXT("string"), TEXT("Node class name"), true), FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("Optional {x,y} node position")), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Function name or 'ClassName::FunctionName' for K2Node_CallFunction nodes"), false), FSmithUEToolParam(TEXT("variable_name"), TEXT("string"), TEXT("Variable name for K2Node_VariableGet or K2Node_VariableSet nodes"), false), FSmithUEToolParam(TEXT("macro_path"), TEXT("string"), TEXT("Macro graph asset path for K2Node_MacroInstance nodes"), false), FSmithUEToolParam(TEXT("key"), TEXT("string"), TEXT("Input key name (e.g. 'W', 'Gamepad_LeftX') for K2Node_InputKey nodes"), false), FSmithUEToolParam(TEXT("input_action"), TEXT("string"), TEXT("InputAction asset path for K2Node_EnhancedInputAction nodes"), false), FSmithUEToolParam(TEXT("target_class"), TEXT("string"), TEXT("Target class path for K2Node_DynamicCast nodes"), false) }), &HandleBpCreateNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_connect_pins"), TEXT("Blueprint"), TEXT("Connect two Blueprint node pins"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("source_node_id"), TEXT("string"), TEXT("Source node GUID"), true), FSmithUEToolParam(TEXT("source_pin"), TEXT("string"), TEXT("Source pin name"), true), FSmithUEToolParam(TEXT("target_node_id"), TEXT("string"), TEXT("Target node GUID"), true), FSmithUEToolParam(TEXT("target_pin"), TEXT("string"), TEXT("Target pin name"), true) }), &HandleBpConnectPins);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_pin_default"), TEXT("Blueprint"), TEXT("Set a Blueprint node pin default value"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"), true), FSmithUEToolParam(TEXT("pin_name"), TEXT("string"), TEXT("Pin name"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Default value string"), true) }), &HandleBpSetPinDefault);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_delete_node"), TEXT("Blueprint"), TEXT("Delete a node from a Blueprint graph"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Target graph name"), true), FSmithUEToolParam(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"), true) }), &HandleBpDeleteNode);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_variable"), TEXT("Blueprint"), TEXT("Add a Blueprint member variable"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Variable name"), true), FSmithUEToolParam(TEXT("var_type"), TEXT("string"), TEXT("Variable type name"), true), FSmithUEToolParam(TEXT("default_value"), TEXT("string"), TEXT("Optional default value")), FSmithUEToolParam(TEXT("category"), TEXT("string"), TEXT("Optional category name")) }), &HandleBpAddVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_variable"), TEXT("Blueprint"), TEXT("Remove a Blueprint member variable by name"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("var_name"), TEXT("string"), TEXT("Variable name to remove"), true) }), &HandleBpRemoveVariable);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_add_component"), TEXT("Blueprint"), TEXT("Add a component to a Blueprint SCS"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component_class"), TEXT("string"), TEXT("Component class name"), true), FSmithUEToolParam(TEXT("component_name"), TEXT("string"), TEXT("Component instance name"), true), FSmithUEToolParam(TEXT("static_mesh"), TEXT("string"), TEXT("Optional StaticMesh asset path for StaticMeshComponent"), false), FSmithUEToolParam(TEXT("parent"), TEXT("string"), TEXT("Optional parent component name to attach to"), false) }), &HandleBpAddComponent);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_component"), TEXT("Blueprint"), TEXT("Remove a component from a Blueprint SCS"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component_name"), TEXT("string"), TEXT("Component instance name to remove"), true) }), &HandleBpRemoveComponent);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_set_component_property"), TEXT("Blueprint"), TEXT("Set a property on a Blueprint SCS or inherited component template"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("component_name"), TEXT("string"), TEXT("Component name (SCS or inherited)"), true), FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name, or 'PostProcessMaterial' to add a blendable material"), true), FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Property value (string/number/bool), or material asset path for PostProcessMaterial"), true) }), &HandleBpSetComponentProperty);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_override_function"), TEXT("Blueprint"), TEXT("Override a parent class function in a Blueprint (creates proper override graph with correct signature)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("function_name"), TEXT("string"), TEXT("Parent function name to override"), true) }), &HandleBpOverrideFunction);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_compile"), TEXT("Blueprint"), TEXT("Compile a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true) }), &HandleBpCompile);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_reparent"), TEXT("Blueprint"), TEXT("Change the parent class of a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("new_parent_class"), TEXT("string"), TEXT("New parent class name or Blueprint path"), true) }), &HandleBpReparent);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_copy_graph"), TEXT("Blueprint"), TEXT("Copy a function graph from one Blueprint to another"), { FSmithUEToolParam(TEXT("source_bp"), TEXT("string"), TEXT("Source Blueprint asset path"), true), FSmithUEToolParam(TEXT("target_bp"), TEXT("string"), TEXT("Target Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Function graph name to copy"), true), FSmithUEToolParam(TEXT("new_graph_name"), TEXT("string"), TEXT("Optional new name for the copied graph")), FSmithUEToolParam(TEXT("overwrite"), TEXT("boolean"), TEXT("If true, removes existing graph with same name before copying")) }), &HandleBpCopyGraph);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_remove_graph"), TEXT("Blueprint"), TEXT("Remove a function graph or ubergraph page from a Blueprint"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name to remove"), true) }), &HandleBpRemoveGraph);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_rename_graph"), TEXT("Blueprint"), TEXT("Rename a function graph or event graph page in a Blueprint (updates all internal call references)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true), FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Current graph name"), true), FSmithUEToolParam(TEXT("new_name"), TEXT("string"), TEXT("New graph name"), true) }), &HandleBpRenameGraph);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_fixup_self_references"), TEXT("Blueprint"), TEXT("Fix variable/function/component nodes to reference Self instead of a foreign parent class (use after bp_copy_graph across different class hierarchies)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true) }), &HandleBpFixupSelfReferences);
	Registry.Register(FSmithUEToolSchema(TEXT("bp_fix_local_var_scope"), TEXT("Blueprint"), TEXT("Fix stale local variable scope references in all function graphs (use after bp_rename_graph or bp_copy_graph when local variables show scope mismatch warnings)"), { FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true) }), &HandleBpFixLocalVarScope);
}

UBlueprint* FSmithUEBpAtomicAPI::LoadBlueprint(const FString& BpPath)
{
	if (BpPath.IsEmpty())
	{
		return nullptr;
	}

	if (BpPath.StartsWith(TEXT("level:"), ESearchCase::IgnoreCase))
	{
		const FString LevelPath = BpPath.Mid(6);
		if (LevelPath.Equals(TEXT("current"), ESearchCase::IgnoreCase))
		{
			if (!GEditor)
			{
				UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: GEditor is null for level:current"));
				return nullptr;
			}

			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			if (!EditorWorld || !EditorWorld->PersistentLevel)
			{
				UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: editor world or persistent level is null for level:current"));
				return nullptr;
			}

		UBlueprint* LevelBP = Cast<UBlueprint>(EditorWorld->PersistentLevel->GetLevelScriptBlueprint(true));
		if (!LevelBP)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: no level script blueprint found for level:current"));
		}
		return LevelBP;
	}

		const FString NormalizedPath = NormalizeObjectPath(LevelPath);
		UWorld* MapWorld = FindObject<UWorld>(nullptr, *NormalizedPath);
		if (!MapWorld || !MapWorld->PersistentLevel)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: level map not found in memory: %s"), *NormalizedPath);
			return nullptr;
		}

		UBlueprint* MapLevelBP = Cast<UBlueprint>(MapWorld->PersistentLevel->GetLevelScriptBlueprint(true));
		if (!MapLevelBP)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("LoadBlueprint failed: no level script blueprint found for map: %s"), *NormalizedPath);
		}
		return MapLevelBP;
	}

	return LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(BpPath));
}

UEdGraph* FSmithUEBpAtomicAPI::FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	// First pass: match by ObjectName (most common case)
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}
	// Second pass: match by FunctionEntry's FunctionReference name (handles cases where
	// ObjectName differs from the logical function name, e.g. after CloneGraph renaming)
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) { continue; }
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				FName FuncName = Entry->FunctionReference.GetMemberName();
				if (!FuncName.IsNone() && FuncName.ToString().Equals(GraphName, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
				break; // Only one FunctionEntry per graph
			}
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
	// Fallback: if NodeClass contains "::" and resolution failed, treat as K2Node_CallFunction shorthand
	if (!NodeType && NodeClass.Contains(TEXT("::")))
	{
		NodeType = UK2Node_CallFunction::StaticClass();
		if (ExtraParams.IsValid() && !ExtraParams->HasField(TEXT("function_name")))
		{
			const_cast<FJsonObject&>(*ExtraParams).SetStringField(TEXT("function_name"), NodeClass);
		}
		else if (!ExtraParams.IsValid())
		{
			// Can't set function_name without ExtraParams - fail
			return FString();
		}
	}
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
	else if (UK2Node_EnhancedInputAction* EIANode = Cast<UK2Node_EnhancedInputAction>(NewNode))
	{
		FString InputActionPath;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("input_action"), InputActionPath))
		{
			UInputAction* Action = LoadObject<UInputAction>(nullptr, *NormalizeObjectPath(InputActionPath));
			if (Action) { EIANode->InputAction = Action; }
		}
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		FString TargetClassName;
		if (ExtraParams.IsValid() && ExtraParams->TryGetStringField(TEXT("target_class"), TargetClassName))
		{
			UClass* TargetClass = ResolveClassByName(TargetClassName, UObject::StaticClass(), TEXT('U'));
			if (TargetClass) { CastNode->TargetType = TargetClass; }
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
	return TryConnectPins(Blueprint, Graph, FString(), SourceNodeId, SourcePinName, TargetNodeId, TargetPinName, Error);
}

bool FSmithUEBpAtomicAPI::SetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value)
{
	FString Error;
	return TrySetPinDefault(Blueprint, Graph, FString(), NodeId, PinName, Value, Error);
}

bool FSmithUEBpAtomicAPI::CompileBlueprint(UBlueprint* Blueprint, TArray<FString>& OutErrors, bool bSkipGarbageCollection)
{
	OutErrors.Reset();
	if (!Blueprint)
	{
		OutErrors.Add(TEXT("Blueprint is null"));
		return false;
	}
	// 编译时始终跳过 GC, 防止其他蓝图 SCS 引用的组件模板被提前销毁
	// (例如蓝图组件类在重编译时会触发 FTickFunction 纯虚函数崩溃)
	// Always skip GC during compile to prevent premature destruction of templates
	// referenced by other Blueprints' SCS (e.g., Blueprint component classes).
	EBlueprintCompileOptions CompileFlags = EBlueprintCompileOptions::SkipGarbageCollection;
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
	// 回退: 如果 parent_class 看起来像蓝图路径 (含 "/"), 加载蓝图并使用其 GeneratedClass
	// Fallback: if parent_class looks like a Blueprint path, load the Blueprint and use its GeneratedClass
	if (!ParentClass && ParentClassName.Contains(TEXT("/")))
	{
		FString BpPathToLoad = ParentClassName;
		// Strip _C suffix if user passed the generated class path (e.g. /Game/BP_BoxPawn.BP_BoxPawn_C)
		if (BpPathToLoad.EndsWith(TEXT("_C")))
		{
			// Convert "/Game/BP_BoxPawn.BP_BoxPawn_C" to "/Game/BP_BoxPawn"
			int32 DotIdx;
			if (BpPathToLoad.FindLastChar(TEXT('.'), DotIdx))
			{
				BpPathToLoad = BpPathToLoad.Left(DotIdx);
			}
		}
		if (UBlueprint* ParentBP = LoadBlueprint(BpPathToLoad))
		{
			ParentClass = ParentBP->GeneratedClass;
		}
	}
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
	// UE 5.x: void functions may not auto-create FunctionResult node. Create one if outputs are requested.
	if (Outputs && Outputs->Num() > 0 && !ResultNode)
	{
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*NewGraph);
		ResultNode = ResultCreator.CreateNode();
		if (!ResultNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create function result node")); }
		ResultNode->NodePosX = 800;
		ResultNode->NodePosY = 0;
		ResultCreator.Finalize();
		ResultNode->AllocateDefaultPins();
	}
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
	if (NodeId.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create node")); }
	FSmithUEToolRegistry::Get().NidSession.MarkStale(BpPath + TEXT("::") + GraphName);
	TSharedPtr<FJsonObject> Response = MakeNodeResponse(NodeId);
	Response->SetBoolField(TEXT("nid_stale"), true);
	return Response;
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
	const FString GraphPath = BpPath + TEXT("::") + GraphName;
	if (!TryConnectPins(Blueprint, Graph, GraphPath, SourceNodeId, SourcePin, TargetNodeId, TargetPin, Error))
	{
		if (TSharedPtr<FJsonObject> StructuredError = FSmithUECommonUtils::ParseJson(Error); StructuredError.IsValid())
		{
			return StructuredError;
		}
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
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
	const FString GraphPath = BpPath + TEXT("::") + GraphName;
	if (!TrySetPinDefault(Blueprint, Graph, GraphPath, NodeId, PinName, Value, Error))
	{
		if (TSharedPtr<FJsonObject> StructuredError = FSmithUECommonUtils::ParseJson(Error); StructuredError.IsValid())
		{
			return StructuredError;
		}
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
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
	FString ResolveError;
	UEdGraphNode* Node = ResolveNodeId(Graph, BpPath + TEXT("::") + GraphName, NodeId, ResolveError);
	if (!Node) { return FSmithUECommonUtils::CreateErrorResponse(ResolveError.IsEmpty() ? TEXT("Node not found") : ResolveError); }
	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpDeleteNode", "SmithUE: Delete Blueprint Node"));
	Node->BreakAllNodeLinks();
	Graph->RemoveNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FSmithUEToolRegistry::Get().NidSession.MarkStale(BpPath + TEXT("::") + GraphName);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("deleted"), true);
	Data->SetBoolField(TEXT("nid_stale"), true);
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

// ---------------------------------------------------------------------------
// bp_remove_variable
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("var_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, VarName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("var_name"), VarName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path"));
	}

	// Find the variable in NewVariables
	const FName VarFName(*VarName);
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < Blueprint->NewVariables.Num(); ++i)
	{
		if (Blueprint->NewVariables[i].VarName == VarFName)
		{
			FoundIndex = i;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Variable not found: '%s'"), *VarName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveVariable", "SmithUE: Remove Blueprint Variable"));
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarFName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("var_name"), VarName);
	Data->SetBoolField(TEXT("removed"), true);
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

	// 如果组件类来自蓝图, 确保添加前先编译该蓝图 (防止未编译导致崩溃)
	// If this is a Blueprint-generated class, ensure its Blueprint is compiled first
	if (UBlueprint* ComponentBP = Cast<UBlueprint>(ComponentClass->ClassGeneratedBy))
	{
		if (ComponentBP->Status == BS_Dirty || ComponentBP->Status == BS_Unknown)
		{
			FKismetEditorUtilities::CompileBlueprint(ComponentBP, EBlueprintCompileOptions::SkipGarbageCollection);
			// 编译后重新获取生成类
			// Re-resolve the class after compilation
			ComponentClass = ComponentBP->GeneratedClass;
			if (!ComponentClass) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Component Blueprint failed to compile")); }
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpAddComponent", "SmithUE: Add Blueprint Component"));
	USCS_Node* SCSNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ComponentName));
	if (!SCSNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create component node")); }

	// 安全措施: 禁用组件模板的 Tick, 防止重编译时触发纯虚函数崩溃
	// Safety: disable tick on the component template to prevent pure virtual crashes
	if (UActorComponent* Template = SCSNode->ComponentTemplate)
	{
		Template->PrimaryComponentTick.bCanEverTick = false;
		Template->PrimaryComponentTick.bStartWithTickEnabled = false;
	}

	// 可选: 挂载到父组件实现层级结构
	// Optional: attach to parent component
	FString ParentName;
	if (Params->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty())
	{
		USCS_Node* ParentNode = nullptr;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ParentName, ESearchCase::IgnoreCase))
			{
				ParentNode = Node;
				break;
			}
		}
		if (ParentNode) { ParentNode->AddChildNode(SCSNode); }
		else { Blueprint->SimpleConstructionScript->AddNode(SCSNode); }
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(SCSNode);
	}

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
// bp_remove_component
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("component_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BpPath; FString ComponentName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath); Params->TryGetStringField(TEXT("component_name"), ComponentName);
	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }
	if (!Blueprint->SimpleConstructionScript) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript")); }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: '%s'"), *ComponentName)); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveComponent", "SmithUE: Remove Blueprint Component"));

	// 移除前将子节点重新挂载到目标的父节点 (防止子组件成为孤儿)
	// Reparent children to the target's parent before removing
	for (USCS_Node* Child : TargetNode->ChildNodes)
	{
		if (!Child) { continue; }
		// Find parent of target node
		USCS_Node* ParentOfTarget = nullptr;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ChildNodes.Contains(TargetNode))
			{
				ParentOfTarget = Node;
				break;
			}
		}
		if (ParentOfTarget) { ParentOfTarget->AddChildNode(Child); }
		else { Blueprint->SimpleConstructionScript->AddNode(Child); }
	}
	TargetNode->ChildNodes.Empty();

	Blueprint->SimpleConstructionScript->RemoveNode(TargetNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("removed"), true);
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

	// --- Special handling: ChildActorClass on UChildActorComponent ---
	if (PropertyName.Equals(TEXT("ChildActorClass"), ESearchCase::IgnoreCase))
	{
		UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(TargetComp);
		if (!ChildActorComp)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Component '%s' is not a ChildActorComponent"), *ComponentName));
		}

		FString ClassPath;
		Params->TryGetStringField(TEXT("value"), ClassPath);

		// Try loading as a Blueprint asset first (most common case for BP classes)
		UClass* ChildClass = nullptr;
		UBlueprint* ChildBP = LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(ClassPath));
		if (ChildBP)
		{
			// Ensure the child BP is compiled
			if (ChildBP->Status == BS_Dirty || ChildBP->Status == BS_Unknown)
			{
				FKismetEditorUtilities::CompileBlueprint(ChildBP, EBlueprintCompileOptions::SkipGarbageCollection);
			}
			ChildClass = ChildBP->GeneratedClass;
		}
		else
		{
			// Try loading as a native class or already-generated class path
			ChildClass = LoadObject<UClass>(nullptr, *NormalizeObjectPath(ClassPath));
			if (!ChildClass)
			{
				// Try appending _C suffix for Blueprint GeneratedClass paths
				FString GeneratedClassPath = ClassPath;
				if (!GeneratedClassPath.EndsWith(TEXT("_C")))
				{
					GeneratedClassPath += TEXT("_C");
				}
				ChildClass = LoadObject<UClass>(nullptr, *NormalizeObjectPath(GeneratedClassPath));
			}
		}

		if (!ChildClass)
		{
			return FSmithUECommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Failed to resolve ChildActorClass: '%s'"), *ClassPath));
		}

		FString BeforeClass = ChildActorComp->GetChildActorClass() ? ChildActorComp->GetChildActorClass()->GetPathName() : TEXT("None");
		ChildActorComp->SetChildActorClass(ChildClass);
		FString AfterClass = ChildActorComp->GetChildActorClass() ? ChildActorComp->GetChildActorClass()->GetPathName() : TEXT("None");

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Blueprint->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("component_name"), ComponentName);
		Data->SetStringField(TEXT("property_name"), TEXT("ChildActorClass"));
		Data->SetStringField(TEXT("before"), BeforeClass);
		Data->SetStringField(TEXT("after"), AfterClass);
		Data->SetBoolField(TEXT("changed"), BeforeClass != AfterClass);
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
		const TCHAR* Result = Prop->ImportText_Direct(*ValueStr, PropAddr, TargetComp, 0);
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

// ---------------------------------------------------------------------------
// bp_reparent — Change the parent class of a Blueprint
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpReparent(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("new_parent_class") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, NewParentClassName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("new_parent_class"), NewParentClassName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	// Resolve the new parent class (try native class first, then Blueprint path)
	UClass* NewParentClass = ResolveClassByName(NewParentClassName, UObject::StaticClass(), TEXT('A'));
	if (!NewParentClass) { NewParentClass = ResolveClassByName(NewParentClassName, UObject::StaticClass(), TEXT('U')); }
	// Fallback: Blueprint path
	if (!NewParentClass && NewParentClassName.Contains(TEXT("/")))
	{
		FString BpPathToLoad = NewParentClassName;
		if (BpPathToLoad.EndsWith(TEXT("_C")))
		{
			int32 DotIdx;
			if (BpPathToLoad.FindLastChar(TEXT('.'), DotIdx)) { BpPathToLoad = BpPathToLoad.Left(DotIdx); }
		}
		if (UBlueprint* ParentBP = LoadBlueprint(BpPathToLoad))
		{
			NewParentClass = ParentBP->GeneratedClass;
		}
	}
	if (!NewParentClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("New parent class not found: %s"), *NewParentClassName));
	}

	// Validate: new parent must be compatible (Actor-based BP needs Actor parent, etc.)
	UClass* OldParentClass = Blueprint->ParentClass;
	if (NewParentClass == OldParentClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("New parent class is the same as current parent"));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpReparent", "SmithUE: Reparent Blueprint"));
	Blueprint->ParentClass = NewParentClass;
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetStringField(TEXT("old_parent"), OldParentClass ? OldParentClass->GetName() : TEXT("None"));
	Data->SetStringField(TEXT("new_parent"), NewParentClass->GetName());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// bp_copy_graph — Copy a function/event graph from one Blueprint to another
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpCopyGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("source_bp"), TEXT("target_bp"), TEXT("graph_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString SourceBpPath, TargetBpPath, GraphName, NewGraphName;
	Params->TryGetStringField(TEXT("source_bp"), SourceBpPath);
	Params->TryGetStringField(TEXT("target_bp"), TargetBpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("new_graph_name"), NewGraphName);
	if (NewGraphName.IsEmpty()) { NewGraphName = GraphName; }

	UBlueprint* SourceBP = LoadBlueprint(SourceBpPath);
	if (!SourceBP) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid source_bp path")); }
	UBlueprint* TargetBP = LoadBlueprint(TargetBpPath);
	if (!TargetBP) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid target_bp path")); }

	// Find source graph
	UEdGraph* SourceGraph = FindGraph(SourceBP, GraphName);
	if (!SourceGraph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found in source Blueprint"), *GraphName));
	}

	// Check if target already has a graph with this name
	bool bOverwrite = Params->HasField(TEXT("overwrite")) && Params->GetBoolField(TEXT("overwrite"));
	UEdGraph* ExistingGraph = FindGraph(TargetBP, NewGraphName);
	if (ExistingGraph && !bOverwrite)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' already exists in target Blueprint. Use overwrite=true to replace it."), *NewGraphName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpCopyGraph", "SmithUE: Copy Blueprint Graph"));

	// Determine graph type and clone accordingly
	bool bIsFunctionGraph = SourceBP->FunctionGraphs.Contains(SourceGraph);
	bool bIsUbergraph = SourceBP->UbergraphPages.Contains(SourceGraph);

	UEdGraph* ClonedGraph = nullptr;

	if (ExistingGraph && bOverwrite)
	{
		// OVERWRITE PATH: Preserve the existing graph object (maintains editor references)
		// and replace its CONTENTS with nodes from the source graph.
		// This avoids RemoveGraph re-outering the graph to the Package, which causes
		// FindBlueprintForNodeChecked assertions when the editor opens the graph.
		ExistingGraph->Modify();

		// 1. Clear all existing nodes from the target graph
		TArray<UEdGraphNode*> OldNodes = ExistingGraph->Nodes;
		for (UEdGraphNode* OldNode : OldNodes)
		{
			if (OldNode)
			{
				OldNode->Modify();
				OldNode->DestroyNode();
			}
		}
		ExistingGraph->Nodes.Empty();

		// 2. Export source graph's nodes to text (clipboard format preserves pin connections)
		TSet<UObject*> NodesToExport;
		for (UEdGraphNode* Node : SourceGraph->Nodes)
		{
			if (Node)
			{
				NodesToExport.Add(Node);
			}
		}
		FString ExportedText;
		FEdGraphUtilities::ExportNodesToText(NodesToExport, ExportedText);

		// 3. Import nodes into the existing graph (nodes get correct Outer = ExistingGraph)
		TSet<UEdGraphNode*> ImportedNodes;
		FEdGraphUtilities::ImportNodesFromText(ExistingGraph, ExportedText, ImportedNodes);

		ClonedGraph = ExistingGraph;
	}
	else
	{
		// NEW GRAPH PATH: No existing graph — clone normally
		ClonedGraph = FEdGraphUtilities::CloneGraph(SourceGraph, TargetBP, nullptr, false);
		if (!ClonedGraph)
		{
			return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to clone graph"));
		}

		// Ensure the cloned graph has the correct name (CloneGraph may auto-rename on collision)
		if (!ClonedGraph->GetName().Equals(NewGraphName, ESearchCase::CaseSensitive))
		{
			ClonedGraph->Rename(*NewGraphName, TargetBP, REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}

		// Add to the appropriate graph array in the target Blueprint
		if (bIsFunctionGraph)
		{
			TargetBP->FunctionGraphs.Add(ClonedGraph);
		}
		else if (bIsUbergraph)
		{
			TargetBP->UbergraphPages.Add(ClonedGraph);
		}
		else
		{
			// Default: treat as function graph
			TargetBP->FunctionGraphs.Add(ClonedGraph);
		}
	}

	// Verify the graph's Outer chain is correct (diagnostic ensure)
	ensure(ClonedGraph->GetOuter() == TargetBP);
	ensure(FBlueprintEditorUtils::FindBlueprintForGraph(ClonedGraph) == TargetBP);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBP);

	// Post-mark verification: ensure MarkBlueprintAsStructurallyModified didn't disrupt the Outer
	if (ClonedGraph->GetOuter() != TargetBP)
	{
		// Recovery: force re-outer the graph back to the Blueprint
		ClonedGraph->Rename(*ClonedGraph->GetName(), TargetBP, REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	}

	// Count nodes for response
	int32 NodeCount = ClonedGraph->Nodes.Num();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("graph_name"), NewGraphName);
	Data->SetNumberField(TEXT("node_count"), NodeCount);
	Data->SetStringField(TEXT("source_bp"), SourceBpPath);
	Data->SetStringField(TEXT("target_bp"), TargetBpPath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRemoveGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, GraphName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found in Blueprint"), *GraphName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRemoveGraph", "SmithUE: Remove Blueprint Graph"));

	Blueprint->FunctionGraphs.Remove(Graph);
	Blueprint->UbergraphPages.Remove(Graph);
	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_graph"), GraphName);
	Data->SetStringField(TEXT("bp_path"), BpPath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpRenameGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path"), TEXT("graph_name"), TEXT("new_name") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath, GraphName, NewName;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	Params->TryGetStringField(TEXT("new_name"), NewName);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found in Blueprint"), *GraphName));
	}

	// Check if new name already exists
	FName NewFName(*NewName);
	UEdGraph* Existing = FindGraph(Blueprint, NewName);
	if (Existing && Existing != Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("A graph named '%s' already exists"), *NewName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpRenameGraph", "SmithUE: Rename Blueprint Graph"));

	FName OldFName = Graph->GetFName();

	// Rename the graph UObject
	Graph->Rename(*NewName, Graph->GetOuter(), REN_DontCreateRedirectors);

	// Update the FunctionEntry node's FunctionReference if this is a function graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			FMemberReference& Ref = Entry->FunctionReference;
			if (Ref.GetMemberName() == OldFName)
			{
				Ref.SetSelfMember(NewFName);
			}
			break;
		}
	}

	// Update all K2Node_CallFunction nodes in this Blueprint that reference the old function name
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	int32 UpdatedCallers = 0;
	for (UEdGraph* OtherGraph : AllGraphs)
	{
		if (!OtherGraph) { continue; }
		for (UEdGraphNode* Node : OtherGraph->Nodes)
		{
			UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (!CallNode) { continue; }

			FMemberReference& FuncRef = CallNode->FunctionReference;
			if (FuncRef.GetMemberName() == OldFName)
			{
				// Check if it references Self or the generated class
				UClass* MemberParent = FuncRef.GetMemberParentClass();
				if (MemberParent == nullptr || MemberParent == Blueprint->SkeletonGeneratedClass || MemberParent == Blueprint->GeneratedClass)
				{
					FuncRef.SetSelfMember(NewFName);
					UpdatedCallers++;
				}
			}
		}
	}

	// Fix local variable scope references in the renamed graph
	// Local var Get/Set nodes store scope as a string (old graph name); update to new name.
	int32 FixedLocalVarScopes = 0;
	FString OldNameStr = OldFName.ToString();
	FString NewNameStr = NewFName.ToString();

	// Build GUID map from FunctionEntry's LocalVariables
	TMap<FName, FGuid> LocalVarGuids;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			for (const FBPVariableDescription& LocalVar : Entry->LocalVariables)
			{
				LocalVarGuids.Add(LocalVar.VarName, LocalVar.VarGuid);
			}
			break;
		}
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
		if (!VarNode) { continue; }
		FMemberReference& VarRef = VarNode->VariableReference;
		if (!VarRef.IsLocalScope()) { continue; }
		if (VarRef.GetMemberScopeName() == OldNameStr)
		{
			FName VarName = VarRef.GetMemberName();
			FGuid VarGuid = VarRef.GetMemberGuid();
			if (!VarGuid.IsValid())
			{
				if (FGuid* Found = LocalVarGuids.Find(VarName))
				{
					VarGuid = *Found;
				}
			}
			VarRef.SetLocalMember(VarName, NewNameStr, VarGuid);
			FixedLocalVarScopes++;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("old_name"), GraphName);
	Data->SetStringField(TEXT("new_name"), NewName);
	Data->SetNumberField(TEXT("updated_callers"), UpdatedCallers);
	Data->SetNumberField(TEXT("fixed_local_var_scopes"), FixedLocalVarScopes);
	Data->SetStringField(TEXT("bp_path"), BpPath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpFixupSelfReferences(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpFixupSelf", "SmithUE: Fixup Self References"));

	// STEP 1: Force skeleton regeneration so all variables and functions are available
	// This is critical because bp_add_variable and bp_copy_graph add entries without refreshing the skeleton
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	EBlueprintCompileOptions CompileOpts = EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::RegenerateSkeletonOnly;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOpts);

	UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass;
	if (!SkeletonClass) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Cannot get SkeletonGeneratedClass after regeneration")); }

	// Collect all function graph names in this Blueprint for function call fixup
	TSet<FName> LocalFunctionNames;
	for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
	{
		if (FuncGraph) { LocalFunctionNames.Add(FuncGraph->GetFName()); }
	}

	// Collect all variable names available via the skeleton class + SCS components + NewVariables
	TSet<FName> LocalVariableNames;
	for (TFieldIterator<FProperty> It(SkeletonClass); It; ++It)
	{
		LocalVariableNames.Add(It->GetFName());
	}
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		LocalVariableNames.Add(Var.VarName);
	}
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (SCSNode)
			{
				LocalVariableNames.Add(SCSNode->GetVariableName());
			}
		}
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	int32 FixedVarNodes = 0;
	int32 FixedFuncNodes = 0;
	int32 FixedEntryNodes = 0;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) { continue; }
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) { continue; }

		// Fix UK2Node_FunctionEntry - set FunctionReference to Self for NEW functions only.
		// After CloneGraph, entry nodes retain stale GUIDs from the source BP.
		// GetMemberParentClass() may return NULL (unresolvable GUID) or a foreign class.
		// SetSelfMember ensures pins reconstruct from the local skeleton.
		// BUT: skip override functions (e.g. UserConstructionScript) — calling SetSelfMember
		// on an override entry tells UE "I define this function" which conflicts with the parent.
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			FName GraphFName = Graph->GetFName();
			if (LocalFunctionNames.Contains(GraphFName))
			{
				// Check if this function is an override of a parent class function
				UFunction* ParentFunc = nullptr;
				if (UClass* SuperClass = SkeletonClass->GetSuperClass())
				{
					ParentFunc = SuperClass->FindFunctionByName(GraphFName);
				}
				if (!ParentFunc)
				{
					// Not an override — safe to fix
					EntryNode->FunctionReference.SetSelfMember(GraphFName);
					FixedEntryNodes++;
				}
			}
		}
		// Fix UK2Node_VariableGet
		if (UK2Node_VariableGet* VarGetNode = Cast<UK2Node_VariableGet>(Node))
		{
			FName VarName = VarGetNode->VariableReference.GetMemberName();
			if (LocalVariableNames.Contains(VarName) && !VarGetNode->VariableReference.IsLocalScope())
			{
				// ALWAYS force SetSelfMember for any variable get referencing a local variable.
				// After CloneGraph, self-context nodes retain stale GUIDs from the source BP.
				// This clears the GUID and forces name-based resolution.
				VarGetNode->VariableReference.SetSelfMember(VarName);
				FixedVarNodes++;
			}
		}
		// Fix UK2Node_VariableSet
		else if (UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node))
		{
			FName VarName = VarSetNode->VariableReference.GetMemberName();
			if (LocalVariableNames.Contains(VarName) && !VarSetNode->VariableReference.IsLocalScope())
			{
				VarSetNode->VariableReference.SetSelfMember(VarName);
				FixedVarNodes++;
			}
		}
		// Fix UK2Node_CallFunction targeting self functions
		else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			FName FuncName = CallNode->FunctionReference.GetMemberName();
			if (LocalFunctionNames.Contains(FuncName))
			{
				// ALWAYS force SetSelfMember for any call to a local function.
				// After CloneGraph, self-context nodes retain stale GUIDs from the source BP.
				// GetMemberParentClass() may resolve to SkeletonClass (appearing correct),
				// but the internal GUID won't match local function GUIDs, causing compile failures.
				// SetSelfMember clears the GUID and forces name-based resolution which always works.
				CallNode->FunctionReference.SetSelfMember(FuncName);
				FixedFuncNodes++;
			}
		}
		}
	}

	// STEP 2: Regenerate skeleton AGAIN after fixing references.
	// The first skeleton regen (before fixup) may have generated incorrect function signatures
	// because entry nodes still pointed to foreign classes. Now that entries are fixed,
	// regenerating the skeleton produces correct function signatures (including TMap parameters).
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOpts);

	// STEP 3: Now refresh all nodes with the CORRECT skeleton - this reconstructs pins properly
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetNumberField(TEXT("fixed_entry_nodes"), FixedEntryNodes);
	Data->SetNumberField(TEXT("fixed_variable_nodes"), FixedVarNodes);
	Data->SetNumberField(TEXT("fixed_function_nodes"), FixedFuncNodes);
	Data->SetNumberField(TEXT("total_fixed"), FixedEntryNodes + FixedVarNodes + FixedFuncNodes);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEBpAtomicAPI::HandleBpFixLocalVarScope(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bp_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString BpPath;
	Params->TryGetStringField(TEXT("bp_path"), BpPath);

	UBlueprint* Blueprint = LoadBlueprint(BpPath);
	if (!Blueprint) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid bp_path")); }

	const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "BpFixLocalVarScope", "SmithUE: Fix Local Variable Scopes"));

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	int32 TotalFixed = 0;
	TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) { continue; }

		FName GraphFName = Graph->GetFName();
		FString GraphNameStr = GraphFName.ToString();

		// Build GUID map from FunctionEntry's LocalVariables for this graph
		TMap<FName, FGuid> LocalVarGuids;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				for (const FBPVariableDescription& LocalVar : Entry->LocalVariables)
				{
					LocalVarGuids.Add(LocalVar.VarName, LocalVar.VarGuid);
				}
				break;
			}
		}
		if (LocalVarGuids.Num() == 0) { continue; } // No local vars declared here

		int32 FixedInGraph = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
			if (!VarNode) { continue; }
			FMemberReference& VarRef = VarNode->VariableReference;
			if (!VarRef.IsLocalScope()) { continue; }

			FName VarName = VarRef.GetMemberName();
			// Only fix if this variable is actually declared as local in THIS graph
			if (!LocalVarGuids.Contains(VarName)) { continue; }

			if (VarRef.GetMemberScopeName() != GraphNameStr)
			{
				FGuid VarGuid = VarRef.GetMemberGuid();
				if (!VarGuid.IsValid())
				{
					if (FGuid* Found = LocalVarGuids.Find(VarName))
					{
						VarGuid = *Found;
					}
				}
				VarRef.SetLocalMember(VarName, GraphNameStr, VarGuid);
				FixedInGraph++;
			}
		}

		if (FixedInGraph > 0)
		{
			Details->SetNumberField(GraphNameStr, FixedInGraph);
			TotalFixed += FixedInGraph;
		}
	}

	if (TotalFixed > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bp_path"), BpPath);
	Data->SetNumberField(TEXT("total_fixed"), TotalFixed);
	Data->SetObjectField(TEXT("details"), Details);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

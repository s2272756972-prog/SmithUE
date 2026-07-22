// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEPCGCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"
#include "PCGCommon.h"
#include "PCGVolume.h"
#include "PCGComponent.h"

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
namespace
{
	UWorld* GetPcgEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	void ReadVec(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FVector& InOut)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(Field, Obj) && Obj && Obj->IsValid())
		{
			double V = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("x"), V)) { InOut.X = V; }
			if ((*Obj)->TryGetNumberField(TEXT("y"), V)) { InOut.Y = V; }
			if ((*Obj)->TryGetNumberField(TEXT("z"), V)) { InOut.Z = V; }
		}
	}

	/** Resolve a UPCGSettings subclass by fuzzy name (e.g. "SurfaceSampler", "PCGSurfaceSamplerSettings"). */
	UClass* ResolvePcgSettingsClass(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		// 1) exact class-name match (with/without leading U), among UPCGSettings subclasses
		const TArray<FString> Exact = {
			Name,
			FString::Printf(TEXT("PCG%sSettings"), *Name),
			FString::Printf(TEXT("%sSettings"), *Name),
			FString::Printf(TEXT("UPCG%sSettings"), *Name)
		};
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->IsChildOf(UPCGSettings::StaticClass()) || C->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			const FString CN = C->GetName();
			for (const FString& Cand : Exact)
			{
				if (CN.Equals(Cand, ESearchCase::IgnoreCase))
				{
					return C;
				}
			}
		}
		// 2) substring fallback
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (C->IsChildOf(UPCGSettings::StaticClass()) && !C->HasAnyClassFlags(CLASS_Abstract) &&
				C->GetName().Contains(Name, ESearchCase::IgnoreCase))
			{
				return C;
			}
		}
		return nullptr;
	}

	/** Resolve a node reference: "input"/"output" for graph I/O, or an integer index into GetNodes(). */
	UPCGNode* ResolvePcgNode(UPCGGraph* Graph, const FString& Ref)
	{
		if (!Graph)
		{
			return nullptr;
		}
		if (Ref.Equals(TEXT("input"), ESearchCase::IgnoreCase))
		{
			return Graph->GetInputNode();
		}
		if (Ref.Equals(TEXT("output"), ESearchCase::IgnoreCase))
		{
			return Graph->GetOutputNode();
		}
		if (Ref.IsNumeric())
		{
			const int32 Index = FCString::Atoi(*Ref);
			const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
			if (Nodes.IsValidIndex(Index))
			{
				return Nodes[Index];
			}
		}
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> PinLabels(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const UPCGPin* Pin : Pins)
		{
			if (Pin)
			{
				Arr.Add(MakeShared<FJsonValueString>(Pin->Properties.Label.ToString()));
			}
		}
		return Arr;
	}
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEPCGCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(
		FSmithUEToolSchema(
			TEXT("create_pcg_graph"),
			TEXT("PCG"),
			TEXT("Create an empty PCG Graph asset (Procedural Content Generation). Read it back with read_pcg_graph; drive it in a level via spawn_pcg_volume. Returns already_exists=true (not an error) if the asset is already present."),
			{
				FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name (no extension)"), /*required*/ true),
				FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder, e.g. /Game/PCG"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandleCreatePcgGraph);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("read_pcg_graph"),
			TEXT("PCG"),
			TEXT("Read a PCG Graph asset: input/output nodes plus every inner node (with its index, title, class, and input/output pin labels) and all edges (from_node.pin -> to_node.pin). Use the node 'index' values with connect_pcg_nodes. Read-only."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path, e.g. /Game/PCG/MyGraph"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandleReadPcgGraph);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("add_pcg_node"),
			TEXT("PCG"),
			TEXT("Add an inner node to a PCG Graph by settings class (fuzzy name, e.g. 'SurfaceSampler', 'TransformPoints', 'StaticMeshSpawner', or full 'UPCGSurfaceSamplerSettings'). Returns the new node's index (use it with connect_pcg_nodes) plus its input/output pin labels. MUTATES the asset."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path"), /*required*/ true),
				FSmithUEToolParam(TEXT("settings_class"), TEXT("string"), TEXT("PCG settings class (fuzzy), e.g. SurfaceSampler / TransformPoints / StaticMeshSpawner / DensityFilter"), /*required*/ true)
					.SetExample(TEXT("SurfaceSampler"))
			}),
		&FSmithUEPCGCommands::HandleAddPcgNode);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("set_pcg_node_property"),
			TEXT("PCG"),
			TEXT("Set a property on a PCG node's Settings (e.g. SurfaceSampler PointsPerSquaredMeter, StaticMeshSpawner mesh). node = 'input'/'output' or an inner node index from read_pcg_graph. Use read_pcg_node to list settable properties + current values. MUTATES the asset."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path"), /*required*/ true),
				FSmithUEToolParam(TEXT("node"), TEXT("string"), TEXT("Node ref: inner node index (from read_pcg_graph)"), /*required*/ true),
				FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Property name on the node's Settings (see read_pcg_node)"), /*required*/ true),
				FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value as string (imported via property reflection, e.g. '250', 'true', '(X=100,Y=100,Z=50)')"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandleSetPcgNodeProperty);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("read_pcg_node"),
			TEXT("PCG"),
			TEXT("Read a PCG node's Settings details: node class + all EditAnywhere properties (name, type, current value). node = 'input'/'output' or an inner node index. Read-only."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path"), /*required*/ true),
				FSmithUEToolParam(TEXT("node"), TEXT("string"), TEXT("Node ref: 'input'/'output' or an inner node index"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandleReadPcgNode);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("connect_pcg_nodes"),
			TEXT("PCG"),
			TEXT("Connect two nodes in a PCG Graph. Node refs are 'input'/'output' (the graph's I/O nodes) or an inner node INDEX from read_pcg_graph. Pin labels default to the source's first output pin and the target's first input pin (usually 'Out'->'In'); the graph input node's output pin is 'Input' and the output node's input pin is 'Output'. MUTATES the asset."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path"), /*required*/ true),
				FSmithUEToolParam(TEXT("from_node"), TEXT("string"), TEXT("Source node: 'input' or an inner node index"), /*required*/ true),
				FSmithUEToolParam(TEXT("to_node"), TEXT("string"), TEXT("Target node: 'output' or an inner node index"), /*required*/ true),
				FSmithUEToolParam(TEXT("from_pin"), TEXT("string"), TEXT("Source output pin label (default: first output pin)"), /*required*/ false),
				FSmithUEToolParam(TEXT("to_pin"), TEXT("string"), TEXT("Target input pin label (default: first input pin)"), /*required*/ false)
			}),
		&FSmithUEPCGCommands::HandleConnectPcgNodes);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("find_pcg_graphs"),
			TEXT("PCG"),
			TEXT("List PCG Graph assets under /Game (optionally filtered by a name substring). Read-only."),
			{
				FSmithUEToolParam(TEXT("query"), TEXT("string"), TEXT("Optional case-insensitive name filter substring"), /*required*/ false)
			}),
		&FSmithUEPCGCommands::HandleFindPcgGraphs);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("spawn_pcg_volume"),
			TEXT("PCG"),
			TEXT("Spawn a PCG Volume actor in the current editor level, assign a PCG Graph to its PCG Component, and generate. MUTATES the level (spawns an actor). Needs an editor world (not during PIE)."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path to assign, e.g. /Game/PCG/MyGraph"), /*required*/ true),
				FSmithUEToolParam(TEXT("label"), TEXT("string"), TEXT("Actor label (default 'PCG_Volume')"), /*required*/ false),
				FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Spawn location {x,y,z} (default origin)"), /*required*/ false),
				FSmithUEToolParam(TEXT("scale"), TEXT("object"), TEXT("Actor scale {x,y,z} (default {20,20,5})"), /*required*/ false)
			}),
		&FSmithUEPCGCommands::HandleSpawnPcgVolume);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("pcg_generate"),
			TEXT("PCG"),
			TEXT("Force-regenerate PCG on an existing PCG Volume actor (by label) in the current editor level."),
			{
				FSmithUEToolParam(TEXT("actor"), TEXT("string"), TEXT("Actor label of the PCG Volume"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandlePcgGenerate);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleCreatePcgGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString Name, Path;
	Params->TryGetStringField(TEXT("name"), Name);
	Params->TryGetStringField(TEXT("path"), Path);

	Name.RemoveFromEnd(TEXT(".uasset"));
	if (!Path.StartsWith(TEXT("/"))) { Path = TEXT("/") + Path; }
	while (Path.EndsWith(TEXT("/"))) { Path.RemoveAt(Path.Len() - 1); }

	const FString PackagePath = Path + TEXT("/") + Name;

	if (UPCGGraph* Existing = LoadObject<UPCGGraph>(nullptr, *PackagePath))
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("already_exists"), true);
		Data->SetStringField(TEXT("name"), Name);
		Data->SetStringField(TEXT("path"), Existing->GetPathName());
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package '%s'"), *PackagePath));
	}

	UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *Name, RF_Public | RF_Standalone);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UPCGGraph"));
	}

	FAssetRegistryModule::AssetCreated(Graph);
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("already_exists"), false);
	Data->SetStringField(TEXT("name"), Name);
	Data->SetStringField(TEXT("path"), Graph->GetPathName());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleReadPcgGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString GraphPath;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s'"), *GraphPath));
	}

	auto NodeToJson = [](UPCGNode* Node, int32 Index) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), Index);
		Obj->SetStringField(TEXT("title"), Node ? Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString() : FString());
		const UPCGSettings* S = Node ? Node->GetSettings() : nullptr;
		Obj->SetStringField(TEXT("class"), S ? S->GetClass()->GetName() : FString());
		Obj->SetArrayField(TEXT("input_pins"), PinLabels(Node ? Node->GetInputPins() : TArray<TObjectPtr<UPCGPin>>()));
		Obj->SetArrayField(TEXT("output_pins"), PinLabels(Node ? Node->GetOutputPins() : TArray<TObjectPtr<UPCGPin>>()));
		return Obj;
	};

	const TArray<UPCGNode*>& Nodes = Graph->GetNodes();

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		NodesArr.Add(MakeShared<FJsonValueObject>(NodeToJson(Nodes[i], i)));
	}

	// Edges: walk every node's (and the input node's) output pins.
	TArray<TSharedPtr<FJsonValue>> EdgesArr;
	auto RefFor = [&](const UPCGNode* N) -> FString
	{
		if (N == Graph->GetInputNode()) { return TEXT("input"); }
		if (N == Graph->GetOutputNode()) { return TEXT("output"); }
		const int32 Idx = Nodes.IndexOfByKey(N);
		return Idx != INDEX_NONE ? FString::FromInt(Idx) : TEXT("?");
	};
	TArray<UPCGNode*> AllNodes = Nodes;
	AllNodes.Add(Graph->GetInputNode());
	for (UPCGNode* N : AllNodes)
	{
		if (!N) { continue; }
		for (const UPCGPin* OutPin : N->GetOutputPins())
		{
			if (!OutPin) { continue; }
			for (const UPCGEdge* Edge : OutPin->Edges)
			{
				if (!Edge) { continue; }
				const UPCGPin* OtherPin = Edge->GetOtherPin(OutPin);
				if (!OtherPin || !OtherPin->Node) { continue; }
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("from_node"), RefFor(N));
				E->SetStringField(TEXT("from_pin"), OutPin->Properties.Label.ToString());
				E->SetStringField(TEXT("to_node"), RefFor(OtherPin->Node));
				E->SetStringField(TEXT("to_pin"), OtherPin->Properties.Label.ToString());
				EdgesArr.Add(MakeShared<FJsonValueObject>(E));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Graph->GetName());
	Data->SetStringField(TEXT("path"), Graph->GetPathName());
	Data->SetNumberField(TEXT("node_count"), Nodes.Num());
	Data->SetBoolField(TEXT("has_input_node"), Graph->GetInputNode() != nullptr);
	Data->SetBoolField(TEXT("has_output_node"), Graph->GetOutputNode() != nullptr);
	// The graph's I/O nodes: connect FROM input node's output pins, TO output node's input pins.
	if (UPCGNode* InNode = Graph->GetInputNode())
	{
		TSharedPtr<FJsonObject> IO = MakeShared<FJsonObject>();
		IO->SetArrayField(TEXT("output_pins"), PinLabels(InNode->GetOutputPins()));
		Data->SetObjectField(TEXT("input_node"), IO);
	}
	if (UPCGNode* OutNode = Graph->GetOutputNode())
	{
		TSharedPtr<FJsonObject> IO = MakeShared<FJsonObject>();
		IO->SetArrayField(TEXT("input_pins"), PinLabels(OutNode->GetInputPins()));
		Data->SetObjectField(TEXT("output_node"), IO);
	}
	Data->SetArrayField(TEXT("nodes"), NodesArr);
	Data->SetArrayField(TEXT("edges"), EdgesArr);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleAddPcgNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path"), TEXT("settings_class") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString GraphPath, SettingsClassName;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);
	Params->TryGetStringField(TEXT("settings_class"), SettingsClassName);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s'"), *GraphPath));
	}

	UClass* SettingsClass = ResolvePcgSettingsClass(SettingsClassName);
	if (!SettingsClass)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown PCG settings class '%s'. Try a fuzzy name like 'SurfaceSampler', 'TransformPoints', 'StaticMeshSpawner', 'DensityFilter'."), *SettingsClassName));
	}

	UPCGSettings* DefaultSettings = nullptr;
	UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, DefaultSettings);
	if (!Node)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to add PCG node of type '%s'"), *SettingsClass->GetName()));
	}
	Graph->MarkPackageDirty();

	const int32 NewIndex = Graph->GetNodes().IndexOfByKey(Node);
#if WITH_EDITOR
	// Cascade left->right so nodes don't stack at (0,0). auto_layout_graph gives a
	// clean connection-aware layout afterwards.
	Node->SetNodePosition(300 + (NewIndex / 6) * 420, -200 + (NewIndex % 6) * 160);
#endif
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), NewIndex);
	Data->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
	Data->SetStringField(TEXT("class"), SettingsClass->GetName());
	Data->SetArrayField(TEXT("input_pins"), PinLabels(Node->GetInputPins()));
	Data->SetArrayField(TEXT("output_pins"), PinLabels(Node->GetOutputPins()));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleSetPcgNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path"), TEXT("node"), TEXT("property"), TEXT("value") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString GraphPath, NodeRef, PropertyName, Value;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);
	Params->TryGetStringField(TEXT("node"), NodeRef);
	Params->TryGetStringField(TEXT("property"), PropertyName);
	Params->TryGetStringField(TEXT("value"), Value);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s'"), *GraphPath)); }
	UPCGNode* Node = ResolvePcgNode(Graph, NodeRef);
	if (!Node) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not resolve node '%s' (use 'input'/'output' or an inner node index)"), *NodeRef)); }
	UPCGSettings* Settings = Node->GetSettings();
	if (!Settings) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Node has no Settings object")); }

	FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		TArray<FString> Editable;
		for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It) { if (It->HasAnyPropertyFlags(CPF_Edit)) { Editable.Add(It->GetName()); } }
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on %s. Editable properties: %s"), *PropertyName, *Settings->GetClass()->GetName(), *FString::Join(Editable, TEXT(", "))));
	}

	void* Addr = Prop->ContainerPtrToValuePtr<void>(Settings);
	Settings->Modify();
	const TCHAR* Result = Prop->ImportText_Direct(*Value, Addr, Settings, PPF_None);
	if (!Result)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s' to '%s' (check value format)"), *PropertyName, *Value));
	}
	// Value is applied on the settings object; persist via save_asset. (PostEditChangeProperty
	// is protected on UPCGSettings; PCG re-reads settings on generate.)
	Graph->MarkPackageDirty();

	FString After;
	Prop->ExportTextItem_Direct(After, Addr, nullptr, Settings, PPF_None);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("node"), NodeRef);
	Data->SetStringField(TEXT("property"), PropertyName);
	Data->SetStringField(TEXT("value"), After);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleReadPcgNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path"), TEXT("node") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString GraphPath, NodeRef;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);
	Params->TryGetStringField(TEXT("node"), NodeRef);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s'"), *GraphPath)); }
	UPCGNode* Node = ResolvePcgNode(Graph, NodeRef);
	if (!Node) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not resolve node '%s'"), *NodeRef)); }
	UPCGSettings* Settings = Node->GetSettings();
	if (!Settings) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Node has no Settings object")); }

	TArray<TSharedPtr<FJsonValue>> PropsArr;
	for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) { continue; }
		FString ValueStr;
		Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(Settings), nullptr, Settings, PPF_None);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Prop->GetName());
		P->SetStringField(TEXT("type"), Prop->GetCPPType());
		P->SetStringField(TEXT("value"), ValueStr);
		PropsArr.Add(MakeShared<FJsonValueObject>(P));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("node"), NodeRef);
	Data->SetStringField(TEXT("class"), Settings->GetClass()->GetName());
	Data->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
	Data->SetNumberField(TEXT("property_count"), PropsArr.Num());
	Data->SetArrayField(TEXT("properties"), PropsArr);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}


TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleConnectPcgNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path"), TEXT("from_node"), TEXT("to_node") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString GraphPath, FromRef, ToRef, FromPin, ToPin;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);
	Params->TryGetStringField(TEXT("from_node"), FromRef);
	Params->TryGetStringField(TEXT("to_node"), ToRef);
	Params->TryGetStringField(TEXT("from_pin"), FromPin);
	Params->TryGetStringField(TEXT("to_pin"), ToPin);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s'"), *GraphPath));
	}

	UPCGNode* From = ResolvePcgNode(Graph, FromRef);
	UPCGNode* To = ResolvePcgNode(Graph, ToRef);
	if (!From || !To)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve node(s): from='%s' to='%s'. Use 'input'/'output' or an inner node index from read_pcg_graph."), *FromRef, *ToRef));
	}

	// Default pins: source's first output, target's first input.
	if (FromPin.IsEmpty())
	{
		const TArray<TObjectPtr<UPCGPin>>& OutPins = From->GetOutputPins();
		if (OutPins.Num() > 0 && OutPins[0]) { FromPin = OutPins[0]->Properties.Label.ToString(); }
	}
	if (ToPin.IsEmpty())
	{
		const TArray<TObjectPtr<UPCGPin>>& InPins = To->GetInputPins();
		if (InPins.Num() > 0 && InPins[0]) { ToPin = InPins[0]->Properties.Label.ToString(); }
	}

	// Validate the pins exist (AddLabeledEdge's bool return means "broke other edges",
	// NOT success, so we cannot rely on it — pre-check instead).
	if (!From->GetOutputPin(FName(*FromPin)))
	{
		const TArray<TSharedPtr<FJsonValue>> Avail = PinLabels(From->GetOutputPins());
		FString Labels; for (const TSharedPtr<FJsonValue>& V : Avail) { Labels += (Labels.IsEmpty() ? TEXT("") : TEXT(", ")) + V->AsString(); }
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Source node '%s' has no output pin '%s'. Available output pins: %s"), *FromRef, *FromPin, *Labels));
	}
	if (!To->GetInputPin(FName(*ToPin)))
	{
		const TArray<TSharedPtr<FJsonValue>> Avail = PinLabels(To->GetInputPins());
		FString Labels; for (const TSharedPtr<FJsonValue>& V : Avail) { Labels += (Labels.IsEmpty() ? TEXT("") : TEXT(", ")) + V->AsString(); }
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Target node '%s' has no input pin '%s'. Available input pins: %s"), *ToRef, *ToPin, *Labels));
	}

	Graph->AddLabeledEdge(From, FName(*FromPin), To, FName(*ToPin)); // bool = "broke edges", not success
	Graph->MarkPackageDirty();

	// Confirm the edge now exists.
	bool bConnected = false;
	if (const UPCGPin* FP = From->GetOutputPin(FName(*FromPin)))
	{
		for (const UPCGEdge* Edge : FP->Edges)
		{
			const UPCGPin* Other = Edge ? Edge->GetOtherPin(FP) : nullptr;
			if (Other && Other->Node == To && Other->Properties.Label == FName(*ToPin)) { bConnected = true; break; }
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("connected"), bConnected);
	Data->SetStringField(TEXT("from_node"), FromRef);
	Data->SetStringField(TEXT("from_pin"), FromPin);
	Data->SetStringField(TEXT("to_node"), ToRef);
	Data->SetStringField(TEXT("to_pin"), ToPin);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleFindPcgGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (Params.IsValid()) { Params->TryGetStringField(TEXT("query"), Query); }

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/PCG"), TEXT("PCGGraph")));
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));

	TArray<FAssetData> Found;
	ARM.Get().GetAssets(Filter, Found);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& A : Found)
	{
		const FString AssetName = A.AssetName.ToString();
		if (!Query.IsEmpty() && !AssetName.Contains(Query, ESearchCase::IgnoreCase))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), AssetName);
		Obj->SetStringField(TEXT("path"), A.GetObjectPathString());
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("count"), Arr.Num());
	Data->SetArrayField(TEXT("graphs"), Arr);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleSpawnPcgVolume(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString GraphPath, Label;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);
	Params->TryGetStringField(TEXT("label"), Label);
	if (Label.IsEmpty()) { Label = TEXT("PCG_Volume"); }

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s' (create it with create_pcg_graph first)"), *GraphPath));
	}

	UWorld* World = GetPcgEditorWorld();
	if (!World)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available (not during PIE)"));
	}

	FVector Location(0.0, 0.0, 0.0);
	FVector Scale(20.0, 20.0, 5.0);
	ReadVec(Params, TEXT("location"), Location);
	ReadVec(Params, TEXT("scale"), Scale);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APCGVolume* Volume = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Volume)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to spawn APCGVolume"));
	}

	Volume->SetActorScale3D(Scale);
	Volume->SetActorLabel(Label);

	bool bGenerated = false;
	if (UPCGComponent* Comp = Volume->FindComponentByClass<UPCGComponent>())
	{
		Comp->SetGraph(Graph);
		Comp->Generate();
		bGenerated = true;
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("actor"), Volume->GetActorLabel());
	Data->SetStringField(TEXT("graph_path"), GraphPath);
	Data->SetBoolField(TEXT("generated"), bGenerated);
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Location.X);
	LocObj->SetNumberField(TEXT("y"), Location.Y);
	LocObj->SetNumberField(TEXT("z"), Location.Z);
	Data->SetObjectField(TEXT("location"), LocObj);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandlePcgGenerate(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("actor") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString TargetLabel;
	Params->TryGetStringField(TEXT("actor"), TargetLabel);

	UWorld* World = GetPcgEditorWorld();
	if (!World)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available (not during PIE)"));
	}

	int32 Hits = 0;
	for (TActorIterator<APCGVolume> It(World); It; ++It)
	{
		APCGVolume* Vol = *It;
		if (!Vol || !Vol->GetActorLabel().Equals(TargetLabel, ESearchCase::IgnoreCase)) { continue; }
		if (UPCGComponent* Comp = Vol->FindComponentByClass<UPCGComponent>())
		{
			Comp->Generate();
			Hits++;
		}
	}

	if (Hits == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No PCG Volume actor with label '%s' (spawn one with spawn_pcg_volume)"), *TargetLabel));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("result"), TEXT("generated"));
	Data->SetNumberField(TEXT("regenerated"), Hits);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

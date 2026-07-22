// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEAICommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

// Blackboard
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
// Behavior Tree
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"
// EQS
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQueryGraph.h"
#include "EnvironmentQueryGraphNode_Root.h"
#include "EnvironmentQueryGraphNode_Option.h"
#include "EnvironmentQueryGraphNode_Test.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EdGraphSchema_EnvironmentQuery.h"
// State Tree
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeFactory.h"
#include "Components/StateTreeComponentSchema.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeCompilerLog.h"

// ---------------------------------------------------------------------------
namespace
{
	UPackage* MakeAssetPackage(const FString& Path, const FString& Name, FString& OutPackagePath)
	{
		FString P = Path;
		if (!P.StartsWith(TEXT("/"))) { P = TEXT("/") + P; }
		while (P.EndsWith(TEXT("/"))) { P.RemoveAt(P.Len() - 1); }
		OutPackagePath = P + TEXT("/") + Name;
		return CreatePackage(*OutPackagePath);
	}

	UClass* ResolveBlackboardKeyType(const FString& Type)
	{
		const FString T = Type.ToLower();
		if (T == TEXT("bool"))    { return UBlackboardKeyType_Bool::StaticClass(); }
		if (T == TEXT("int") || T == TEXT("int32")) { return UBlackboardKeyType_Int::StaticClass(); }
		if (T == TEXT("float"))   { return UBlackboardKeyType_Float::StaticClass(); }
		if (T == TEXT("vector"))  { return UBlackboardKeyType_Vector::StaticClass(); }
		if (T == TEXT("rotator")) { return UBlackboardKeyType_Rotator::StaticClass(); }
		if (T == TEXT("object"))  { return UBlackboardKeyType_Object::StaticClass(); }
		if (T == TEXT("class"))   { return UBlackboardKeyType_Class::StaticClass(); }
		if (T == TEXT("string"))  { return UBlackboardKeyType_String::StaticClass(); }
		if (T == TEXT("name"))    { return UBlackboardKeyType_Name::StaticClass(); }
		return nullptr;
	}

	/** Resolve a concrete subclass of BaseClass by fuzzy name (exact, prefixed variants, then substring). */
	UClass* ResolveSubclassByName(UClass* BaseClass, const FString& Name, const TArray<FString>& Prefixes)
	{
		if (!BaseClass || Name.IsEmpty()) { return nullptr; }
		TArray<FString> Cands = { Name };
		for (const FString& Pre : Prefixes) { Cands.Add(Pre + Name); }
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->IsChildOf(BaseClass) || C->HasAnyClassFlags(CLASS_Abstract)) { continue; }
			for (const FString& Cand : Cands) { if (C->GetName().Equals(Cand, ESearchCase::IgnoreCase)) { return C; } }
		}
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (C->IsChildOf(BaseClass) && !C->HasAnyClassFlags(CLASS_Abstract) && C->GetName().Contains(Name, ESearchCase::IgnoreCase)) { return C; }
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
void FSmithUEAICommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	// ---- Blackboard ----
	Registry.Register(FSmithUEToolSchema(TEXT("create_blackboard"), TEXT("AI"),
		TEXT("Create a Blackboard Data asset (UBlackboardData). Add keys with blackboard_add_key; read with read_blackboard."),
		{ FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name (convention: BB_*)"), true),
		  FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder, e.g. /Game/AI"), true) }),
		&FSmithUEAICommands::HandleCreateBlackboard);

	Registry.Register(FSmithUEToolSchema(TEXT("blackboard_add_key"), TEXT("AI"),
		TEXT("Add a typed key to a Blackboard Data asset. MUTATES the asset; call save_asset to persist."),
		{ FSmithUEToolParam(TEXT("blackboard_path"), TEXT("string"), TEXT("Blackboard asset path"), true),
		  FSmithUEToolParam(TEXT("key_name"), TEXT("string"), TEXT("Key name"), true),
		  FSmithUEToolParam(TEXT("key_type"), TEXT("string"), TEXT("Key type"), true)
			.SetAllowedValues({ TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("vector"), TEXT("rotator"), TEXT("object"), TEXT("class"), TEXT("string"), TEXT("name") }) }),
		&FSmithUEAICommands::HandleBlackboardAddKey);

	Registry.Register(FSmithUEToolSchema(TEXT("read_blackboard"), TEXT("AI"),
		TEXT("Read a Blackboard Data asset: its keys (name + type) and parent. Read-only."),
		{ FSmithUEToolParam(TEXT("blackboard_path"), TEXT("string"), TEXT("Blackboard asset path"), true) }),
		&FSmithUEAICommands::HandleReadBlackboard);

	// ---- Behavior Tree ----
	Registry.Register(FSmithUEToolSchema(TEXT("create_behavior_tree"), TEXT("AI"),
		TEXT("Create a Behavior Tree asset (UBehaviorTree), optionally linking a Blackboard. Open it in the editor to author the node graph (root/composites/tasks)."),
		{ FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name (convention: BT_*)"), true),
		  FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder"), true),
		  FSmithUEToolParam(TEXT("blackboard_path"), TEXT("string"), TEXT("Optional Blackboard asset to link"), false) }),
		&FSmithUEAICommands::HandleCreateBehaviorTree);

	Registry.Register(FSmithUEToolSchema(TEXT("bt_set_blackboard"), TEXT("AI"),
		TEXT("Set/replace the Blackboard asset linked to a Behavior Tree. MUTATES the asset."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true),
		  FSmithUEToolParam(TEXT("blackboard_path"), TEXT("string"), TEXT("Blackboard asset path"), true) }),
		&FSmithUEAICommands::HandleBtSetBlackboard);

	Registry.Register(FSmithUEToolSchema(TEXT("read_behavior_tree"), TEXT("AI"),
		TEXT("Read a Behavior Tree asset: linked blackboard + root node class. Read-only."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true) }),
		&FSmithUEAICommands::HandleReadBehaviorTree);

	Registry.Register(FSmithUEToolSchema(TEXT("bt_add_node"), TEXT("AI"),
		TEXT("Add a composite or task node to a Behavior Tree and connect it under the root (or a parent composite node index). node_type: 'Selector'/'Sequence'/'SimpleParallel' (composite) or a task name ('Wait','MoveTo','RunEQSQuery',... resolves UBTTask_* by name). Builds the editor graph + rebuilds the runtime tree. MUTATES; call save_asset to persist. Returns the new node's graph index (use as parent for further nodes)."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true),
		  FSmithUEToolParam(TEXT("node_type"), TEXT("string"), TEXT("Selector/Sequence/SimpleParallel or a task name (Wait, MoveTo, ...)"), true).SetExample(TEXT("Sequence")),
		  FSmithUEToolParam(TEXT("parent"), TEXT("string"), TEXT("Parent composite node index (default: root). Tasks cannot be parents."), false) }),
		&FSmithUEAICommands::HandleBtAddNode);

	Registry.Register(FSmithUEToolSchema(TEXT("bt_read_node"), TEXT("AI"),
		TEXT("Read a Behavior Tree graph node's runtime instance: class + editable properties (name/type/value). node_index from bt_add_node / the graph. Read-only."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true),
		  FSmithUEToolParam(TEXT("node_index"), TEXT("int"), TEXT("Graph node index"), true) }),
		&FSmithUEAICommands::HandleBtReadNode);

	Registry.Register(FSmithUEToolSchema(TEXT("bt_set_node_property"), TEXT("AI"),
		TEXT("Set a property on a Behavior Tree node's runtime instance (e.g. a Wait task's WaitTime, a MoveTo's AcceptableRadius). Use bt_read_node to list editable properties. MUTATES; call save_asset to persist."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true),
		  FSmithUEToolParam(TEXT("node_index"), TEXT("int"), TEXT("Graph node index (from bt_add_node)"), true),
		  FSmithUEToolParam(TEXT("property"), TEXT("string"), TEXT("Property name on the node (see bt_read_node)"), true),
		  FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value as string (property-reflection import, e.g. '2.5', 'true')"), true) }),
		&FSmithUEAICommands::HandleBtSetNodeProperty);

	Registry.Register(FSmithUEToolSchema(TEXT("bt_add_decorator"), TEXT("AI"),
		TEXT("Attach a decorator (conditional) to a Behavior Tree node. decorator = fuzzy UBTDecorator subclass name (Blackboard, CompareBBEntries, Cooldown, TimeLimit, Loop, ...). node_index from bt_add_node. MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true),
		  FSmithUEToolParam(TEXT("node_index"), TEXT("int"), TEXT("Graph node index to attach the decorator to"), true),
		  FSmithUEToolParam(TEXT("decorator"), TEXT("string"), TEXT("Decorator class (fuzzy), e.g. Blackboard / Cooldown / Loop"), true).SetExample(TEXT("Blackboard")) }),
		&FSmithUEAICommands::HandleBtAddDecorator);

	Registry.Register(FSmithUEToolSchema(TEXT("bt_add_service"), TEXT("AI"),
		TEXT("Attach a service (periodic tick) to a Behavior Tree composite node. service = fuzzy UBTService subclass name (DefaultFocus, RunEQS, BlueprintBase, ...). node_index must be a composite. MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("bt_path"), TEXT("string"), TEXT("Behavior Tree asset path"), true),
		  FSmithUEToolParam(TEXT("node_index"), TEXT("int"), TEXT("Composite graph node index to attach the service to"), true),
		  FSmithUEToolParam(TEXT("service"), TEXT("string"), TEXT("Service class (fuzzy), e.g. DefaultFocus / RunEQS"), true).SetExample(TEXT("DefaultFocus")) }),
		&FSmithUEAICommands::HandleBtAddService);

	// ---- EQS ----
	Registry.Register(FSmithUEToolSchema(TEXT("create_eqs"), TEXT("AI"),
		TEXT("Create an Environment Query (EQS) asset (UEnvQuery). Open it in the editor to add generators/tests."),
		{ FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name (convention: EQS_*)"), true),
		  FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder"), true) }),
		&FSmithUEAICommands::HandleCreateEqs);

	Registry.Register(FSmithUEToolSchema(TEXT("read_eqs"), TEXT("AI"),
		TEXT("Read an EQS asset: option/generator count. Read-only."),
		{ FSmithUEToolParam(TEXT("eqs_path"), TEXT("string"), TEXT("EQS asset path"), true) }),
		&FSmithUEAICommands::HandleReadEqs);

	Registry.Register(FSmithUEToolSchema(TEXT("eqs_add_option"), TEXT("AI"),
		TEXT("Add a generator option to an EQS asset (builds the editor graph + rebuilds runtime Options). generator = fuzzy name of a UEnvQueryGenerator subclass (SimpleGrid, ActorsOfClass, OnCircle, Donut, Cone, CurrentLocation, ...). MUTATES; call save_asset to persist."),
		{ FSmithUEToolParam(TEXT("eqs_path"), TEXT("string"), TEXT("EQS asset path"), true),
		  FSmithUEToolParam(TEXT("generator"), TEXT("string"), TEXT("Generator class (fuzzy), e.g. SimpleGrid / ActorsOfClass / OnCircle"), true).SetExample(TEXT("SimpleGrid")) }),
		&FSmithUEAICommands::HandleEqsAddOption);

	Registry.Register(FSmithUEToolSchema(TEXT("eqs_add_test"), TEXT("AI"),
		TEXT("Add a scoring/filtering test to an EQS option (generator). test = fuzzy UEnvQueryTest subclass name (Distance, Dot, Trace, Pathfinding, Overlap, Project, ...). option = 0-based index among the option/generator nodes (default: last added). MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("eqs_path"), TEXT("string"), TEXT("EQS asset path"), true),
		  FSmithUEToolParam(TEXT("test"), TEXT("string"), TEXT("Test class (fuzzy), e.g. Distance / Dot / Trace / Pathfinding"), true).SetExample(TEXT("Distance")),
		  FSmithUEToolParam(TEXT("option"), TEXT("int"), TEXT("Option index to add the test to (default: last)"), false) }),
		&FSmithUEAICommands::HandleEqsAddTest);

	// ---- State Tree ----
	Registry.Register(FSmithUEToolSchema(TEXT("create_state_tree"), TEXT("AI"),
		TEXT("Create a State Tree asset (UStateTree) with the StateTree Component schema, a root state, and an initial compile. Open it in the editor to author states/tasks/transitions."),
		{ FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name (convention: ST_*)"), true),
		  FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder"), true) }),
		&FSmithUEAICommands::HandleCreateStateTree);

	Registry.Register(FSmithUEToolSchema(TEXT("read_state_tree"), TEXT("AI"),
		TEXT("Read a State Tree asset: schema + root/sub states. Read-only."),
		{ FSmithUEToolParam(TEXT("state_tree_path"), TEXT("string"), TEXT("State Tree asset path"), true) }),
		&FSmithUEAICommands::HandleReadStateTree);

	Registry.Register(FSmithUEToolSchema(TEXT("state_tree_add_state"), TEXT("AI"),
		TEXT("Add a child state to a State Tree (under the given parent state name, or the first root if omitted), then recompile. MUTATES + recompiles; call save_asset to persist."),
		{ FSmithUEToolParam(TEXT("state_tree_path"), TEXT("string"), TEXT("State Tree asset path"), true),
		  FSmithUEToolParam(TEXT("state_name"), TEXT("string"), TEXT("New state name"), true),
		  FSmithUEToolParam(TEXT("parent"), TEXT("string"), TEXT("Parent state name (default: first root state)"), false) }),
		&FSmithUEAICommands::HandleStateTreeAddState);

	Registry.Register(FSmithUEToolSchema(TEXT("state_tree_add_transition"), TEXT("AI"),
		TEXT("Add a transition from one state to another (GotoState) and recompile. from_state/to_state are state names. trigger: OnStateCompleted (default) / OnStateSucceeded / OnStateFailed. MUTATES + recompiles; call save_asset to persist."),
		{ FSmithUEToolParam(TEXT("state_tree_path"), TEXT("string"), TEXT("State Tree asset path"), true),
		  FSmithUEToolParam(TEXT("from_state"), TEXT("string"), TEXT("Source state name"), true),
		  FSmithUEToolParam(TEXT("to_state"), TEXT("string"), TEXT("Target state name"), true),
		  FSmithUEToolParam(TEXT("trigger"), TEXT("string"), TEXT("Transition trigger (default OnStateCompleted)"), false)
			.SetAllowedValues({ TEXT("OnStateCompleted"), TEXT("OnStateSucceeded"), TEXT("OnStateFailed") }) }),
		&FSmithUEAICommands::HandleStateTreeAddTransition);
}

// ---------------------------------------------------------------------------
// Blackboard
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUEAICommands::HandleCreateBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name, Path; Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("path"), Path);
	FString PackagePath; UPackage* Package = MakeAssetPackage(Path, Name, PackagePath);
	if (LoadObject<UBlackboardData>(nullptr, *PackagePath)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Blackboard already exists at target path")); }
	UBlackboardData* BB = NewObject<UBlackboardData>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!BB) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UBlackboardData")); }
	FAssetRegistryModule::AssetCreated(BB);
	BB->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blackboard_path"), PackagePath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBlackboardAddKey(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("blackboard_path"), TEXT("key_name"), TEXT("key_type") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BbPath, KeyName, KeyType;
	Params->TryGetStringField(TEXT("blackboard_path"), BbPath); Params->TryGetStringField(TEXT("key_name"), KeyName); Params->TryGetStringField(TEXT("key_type"), KeyType);
	UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BbPath);
	if (!BB) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blackboard not found: '%s'"), *BbPath)); }
	UClass* KeyClass = ResolveBlackboardKeyType(KeyType);
	if (!KeyClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown key_type '%s' (bool/int/float/vector/rotator/object/class/string/name)"), *KeyType)); }

	const FName KeyFName(*KeyName);
	for (const FBlackboardEntry& E : BB->Keys) { if (E.EntryName == KeyFName) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Key '%s' already exists"), *KeyName)); } }

	FBlackboardEntry Entry;
	Entry.EntryName = KeyFName;
	Entry.KeyType = NewObject<UBlackboardKeyType>(BB, KeyClass);
	BB->Keys.Add(Entry);
	BB->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("key_name"), KeyName);
	Data->SetStringField(TEXT("key_type"), KeyClass->GetName());
	Data->SetNumberField(TEXT("key_count"), BB->Keys.Num());
	Data->SetStringField(TEXT("note"), TEXT("Key added in-memory; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleReadBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("blackboard_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BbPath; Params->TryGetStringField(TEXT("blackboard_path"), BbPath);
	UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BbPath);
	if (!BB) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blackboard not found: '%s'"), *BbPath)); }
	TArray<TSharedPtr<FJsonValue>> Keys;
	for (const FBlackboardEntry& E : BB->Keys)
	{
		TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
		K->SetStringField(TEXT("name"), E.EntryName.ToString());
		K->SetStringField(TEXT("type"), E.KeyType ? E.KeyType->GetClass()->GetName() : TEXT("None"));
		Keys.Add(MakeShared<FJsonValueObject>(K));
	}
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), BB->GetName());
	Data->SetStringField(TEXT("parent"), BB->Parent ? BB->Parent->GetPathName() : FString());
	Data->SetNumberField(TEXT("key_count"), Keys.Num());
	Data->SetArrayField(TEXT("keys"), Keys);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Behavior Tree
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUEAICommands::HandleCreateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name, Path, BbPath;
	Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("path"), Path); Params->TryGetStringField(TEXT("blackboard_path"), BbPath);
	FString PackagePath; UPackage* Package = MakeAssetPackage(Path, Name, PackagePath);
	if (LoadObject<UBehaviorTree>(nullptr, *PackagePath)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Behavior Tree already exists at target path")); }
	UBehaviorTree* BT = NewObject<UBehaviorTree>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UBehaviorTree")); }
	if (!BbPath.IsEmpty())
	{
		if (UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BbPath)) { BT->BlackboardAsset = BB; }
	}
	FAssetRegistryModule::AssetCreated(BT);
	BT->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bt_path"), PackagePath);
	Data->SetStringField(TEXT("blackboard"), BT->BlackboardAsset ? BT->BlackboardAsset->GetPathName() : FString());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBtSetBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path"), TEXT("blackboard_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath, BbPath; Params->TryGetStringField(TEXT("bt_path"), BtPath); Params->TryGetStringField(TEXT("blackboard_path"), BbPath);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }
	UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BbPath);
	if (!BB) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blackboard not found: '%s'"), *BbPath)); }
	BT->BlackboardAsset = BB;
	BT->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("bt_path"), BtPath);
	Data->SetStringField(TEXT("blackboard"), BB->GetPathName());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleReadBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath; Params->TryGetStringField(TEXT("bt_path"), BtPath);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), BT->GetName());
	Data->SetStringField(TEXT("blackboard"), BT->BlackboardAsset ? BT->BlackboardAsset->GetPathName() : FString());
	Data->SetStringField(TEXT("root_node"), BT->RootNode ? BT->RootNode->GetClass()->GetName() : TEXT("None (author in editor)"));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

namespace
{
	UClass* ResolveBTNodeClass(const FString& Name, bool& bOutComposite)
	{
		const FString L = Name.ToLower();
		if (L == TEXT("selector")) { bOutComposite = true; return UBTComposite_Selector::StaticClass(); }
		if (L == TEXT("sequence")) { bOutComposite = true; return UBTComposite_Sequence::StaticClass(); }
		if (L == TEXT("simpleparallel") || L == TEXT("parallel")) { bOutComposite = true; return UBTComposite_SimpleParallel::StaticClass(); }
		// Task: resolve a concrete UBTTaskNode subclass by fuzzy name.
		const TArray<FString> Cands = { Name, FString::Printf(TEXT("BTTask_%s"), *Name), FString::Printf(TEXT("UBTTask_%s"), *Name) };
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->IsChildOf(UBTTaskNode::StaticClass()) || C->HasAnyClassFlags(CLASS_Abstract)) { continue; }
			for (const FString& Cand : Cands) { if (C->GetName().Equals(Cand, ESearchCase::IgnoreCase)) { bOutComposite = false; return C; } }
		}
		return nullptr;
	}

	UBehaviorTreeGraph* EnsureBTGraph(UBehaviorTree* BT)
	{
		UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
		if (!Graph)
		{
			BT->BTGraph = FBlueprintEditorUtils::CreateNewGraph(BT, TEXT("Behavior Tree"), UBehaviorTreeGraph::StaticClass(), UEdGraphSchema_BehaviorTree::StaticClass());
			Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
			if (Graph)
			{
				const UEdGraphSchema* Schema = Graph->GetSchema();
				if (Schema) { Schema->CreateDefaultNodesForGraph(*Graph); } // spawns the Root node
				Graph->OnCreated();
			}
		}
		return Graph;
	}
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBtAddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path"), TEXT("node_type") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath, NodeType, ParentRef;
	Params->TryGetStringField(TEXT("bt_path"), BtPath); Params->TryGetStringField(TEXT("node_type"), NodeType); Params->TryGetStringField(TEXT("parent"), ParentRef);

	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }

	bool bIsComposite = false;
	UClass* NodeClass = ResolveBTNodeClass(NodeType, bIsComposite);
	if (!NodeClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown node_type '%s' (use Selector/Sequence/SimpleParallel, or a task name like Wait/MoveTo)"), *NodeType)); }

	UBehaviorTreeGraph* Graph = EnsureBTGraph(BT);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create/get the Behavior Tree graph")); }

	// Resolve parent graph node: by index, else the Root node.
	UBehaviorTreeGraphNode* ParentNode = nullptr;
	if (!ParentRef.IsEmpty() && ParentRef.IsNumeric())
	{
		const int32 Idx = FCString::Atoi(*ParentRef);
		if (Graph->Nodes.IsValidIndex(Idx)) { ParentNode = Cast<UBehaviorTreeGraphNode>(Graph->Nodes[Idx]); }
	}
	if (!ParentNode)
	{
		for (UEdGraphNode* N : Graph->Nodes) { if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(N)) { ParentNode = Root; break; } }
	}
	if (!ParentNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Could not resolve a parent graph node (no root?)")); }

	// Create the graph node + its runtime NodeInstance.
	UBehaviorTreeGraphNode* NewGraphNode = nullptr;
	if (bIsComposite)
	{
		FGraphNodeCreator<UBehaviorTreeGraphNode_Composite> Creator(*Graph);
		UBehaviorTreeGraphNode_Composite* GN = Creator.CreateNode();
		GN->NodeInstance = NewObject<UBTCompositeNode>(BT, NodeClass);
		NewGraphNode = GN;
		Creator.Finalize();
	}
	else
	{
		FGraphNodeCreator<UBehaviorTreeGraphNode_Task> Creator(*Graph);
		UBehaviorTreeGraphNode_Task* GN = Creator.CreateNode();
		GN->NodeInstance = NewObject<UBTTaskNode>(BT, NodeClass);
		NewGraphNode = GN;
		Creator.Finalize();
	}
	if (!NewGraphNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create graph node")); }

	// Cascade position below the parent.
	NewGraphNode->NodePosX = ParentNode->NodePosX + (Graph->Nodes.Num() % 5) * 40;
	NewGraphNode->NodePosY = ParentNode->NodePosY + 150;

	// Connect parent output -> new node input.
	UEdGraphPin* ParentOut = ParentNode->GetOutputPin();
	UEdGraphPin* ChildIn = NewGraphNode->GetInputPin();
	if (ParentOut && ChildIn)
	{
		ParentOut->MakeLinkTo(ChildIn);
	}
	else
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Parent has no output pin or child has no input pin (task nodes cannot be parents)"));
	}

	// Rebuild the runtime tree from the graph.
	Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
	Graph->MarkPackageDirty();
	BT->MarkPackageDirty();

	const int32 NewIndex = Graph->Nodes.IndexOfByKey(NewGraphNode);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("node_index"), NewIndex);
	Data->SetStringField(TEXT("node_class"), NodeClass->GetName());
	Data->SetBoolField(TEXT("is_composite"), bIsComposite);
	Data->SetStringField(TEXT("parent"), ParentRef.IsEmpty() ? TEXT("root") : ParentRef);
	Data->SetStringField(TEXT("note"), TEXT("Node added + runtime tree rebuilt; call save_asset to persist. Use node_index as parent for children (composites only)."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// EQS
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBtReadNode(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path"), TEXT("node_index") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath; int32 NodeIndex = -1;
	Params->TryGetStringField(TEXT("bt_path"), BtPath); Params->TryGetNumberField(TEXT("node_index"), NodeIndex);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph || !Graph->Nodes.IsValidIndex(NodeIndex)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid node_index (open/author the BT graph first)")); }
	UBehaviorTreeGraphNode* GN = Cast<UBehaviorTreeGraphNode>(Graph->Nodes[NodeIndex]);
	UObject* Instance = GN ? GN->NodeInstance : nullptr;
	if (!Instance) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph node has no runtime instance")); }

	TArray<TSharedPtr<FJsonValue>> PropsArr;
	for (TFieldIterator<FProperty> It(Instance->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) { continue; }
		FString ValueStr;
		Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(Instance), nullptr, Instance, PPF_None);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Prop->GetName());
		P->SetStringField(TEXT("type"), Prop->GetCPPType());
		P->SetStringField(TEXT("value"), ValueStr);
		PropsArr.Add(MakeShared<FJsonValueObject>(P));
	}
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("node_index"), NodeIndex);
	Data->SetStringField(TEXT("class"), Instance->GetClass()->GetName());
	Data->SetNumberField(TEXT("property_count"), PropsArr.Num());
	Data->SetArrayField(TEXT("properties"), PropsArr);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBtSetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path"), TEXT("node_index"), TEXT("property"), TEXT("value") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath, PropertyName, Value; int32 NodeIndex = -1;
	Params->TryGetStringField(TEXT("bt_path"), BtPath); Params->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Params->TryGetStringField(TEXT("property"), PropertyName); Params->TryGetStringField(TEXT("value"), Value);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph || !Graph->Nodes.IsValidIndex(NodeIndex)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid node_index")); }
	UBehaviorTreeGraphNode* GN = Cast<UBehaviorTreeGraphNode>(Graph->Nodes[NodeIndex]);
	UObject* Instance = GN ? GN->NodeInstance : nullptr;
	if (!Instance) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Graph node has no runtime instance")); }

	FProperty* Prop = Instance->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		TArray<FString> Editable;
		for (TFieldIterator<FProperty> It(Instance->GetClass()); It; ++It) { if (It->HasAnyPropertyFlags(CPF_Edit)) { Editable.Add(It->GetName()); } }
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found on %s. Editable: %s"), *PropertyName, *Instance->GetClass()->GetName(), *FString::Join(Editable, TEXT(", "))));
	}
	void* Addr = Prop->ContainerPtrToValuePtr<void>(Instance);
	Instance->Modify();
	const TCHAR* Result = Prop->ImportText_Direct(*Value, Addr, Instance, PPF_None);
	if (!Result) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s' to '%s'"), *PropertyName, *Value)); }
	Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
	BT->MarkPackageDirty();

	FString After; Prop->ExportTextItem_Direct(After, Addr, nullptr, Instance, PPF_None);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("node_index"), NodeIndex);
	Data->SetStringField(TEXT("property"), PropertyName);
	Data->SetStringField(TEXT("value"), After);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBtAddDecorator(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path"), TEXT("node_index"), TEXT("decorator") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath, DecoratorName; int32 NodeIndex = -1;
	Params->TryGetStringField(TEXT("bt_path"), BtPath); Params->TryGetNumberField(TEXT("node_index"), NodeIndex); Params->TryGetStringField(TEXT("decorator"), DecoratorName);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph || !Graph->Nodes.IsValidIndex(NodeIndex)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid node_index (author the BT graph first)")); }
	UBehaviorTreeGraphNode* ParentNode = Cast<UBehaviorTreeGraphNode>(Graph->Nodes[NodeIndex]);
	if (!ParentNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("node_index is not a Behavior Tree node")); }
	UClass* DecClass = ResolveSubclassByName(UBTDecorator::StaticClass(), DecoratorName, { TEXT("BTDecorator_"), TEXT("UBTDecorator_") });
	if (!DecClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown decorator '%s' (try Blackboard/Cooldown/Loop/TimeLimit)"), *DecoratorName)); }

	UBehaviorTreeGraphNode_Decorator* DecNode = NewObject<UBehaviorTreeGraphNode_Decorator>(Graph);
	DecNode->NodeInstance = NewObject<UBTDecorator>(BT, DecClass);
	ParentNode->AddSubNode(DecNode, Graph);
	Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("node_index"), NodeIndex);
	Data->SetStringField(TEXT("decorator"), DecClass->GetName());
	Data->SetNumberField(TEXT("decorator_count"), ParentNode->SubNodes.Num());
	Data->SetStringField(TEXT("note"), TEXT("Decorator attached + runtime rebuilt; call save_asset. Set its properties with bt_set_node_property on the decorator's graph index."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleBtAddService(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("bt_path"), TEXT("node_index"), TEXT("service") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString BtPath, ServiceName; int32 NodeIndex = -1;
	Params->TryGetStringField(TEXT("bt_path"), BtPath); Params->TryGetNumberField(TEXT("node_index"), NodeIndex); Params->TryGetStringField(TEXT("service"), ServiceName);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!BT) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Behavior Tree not found: '%s'"), *BtPath)); }
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph || !Graph->Nodes.IsValidIndex(NodeIndex)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid node_index")); }
	UBehaviorTreeGraphNode* ParentNode = Cast<UBehaviorTreeGraphNode>(Graph->Nodes[NodeIndex]);
	if (!ParentNode) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("node_index is not a Behavior Tree node")); }
	UClass* SvcClass = ResolveSubclassByName(UBTService::StaticClass(), ServiceName, { TEXT("BTService_"), TEXT("UBTService_") });
	if (!SvcClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown service '%s' (try DefaultFocus/RunEQS/BlueprintBase)"), *ServiceName)); }

	UBehaviorTreeGraphNode_Service* SvcNode = NewObject<UBehaviorTreeGraphNode_Service>(Graph);
	SvcNode->NodeInstance = NewObject<UBTService>(BT, SvcClass);
	ParentNode->AddSubNode(SvcNode, Graph);
	Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("node_index"), NodeIndex);
	Data->SetStringField(TEXT("service"), SvcClass->GetName());
	Data->SetStringField(TEXT("note"), TEXT("Service attached + runtime rebuilt; call save_asset."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// EQS impl
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUEAICommands::HandleCreateEqs(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name, Path; Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("path"), Path);
	FString PackagePath; UPackage* Package = MakeAssetPackage(Path, Name, PackagePath);
	if (LoadObject<UEnvQuery>(nullptr, *PackagePath)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("EQS already exists at target path")); }
	UEnvQuery* Query = NewObject<UEnvQuery>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!Query) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UEnvQuery")); }
	FAssetRegistryModule::AssetCreated(Query);
	Query->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("eqs_path"), PackagePath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleReadEqs(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("eqs_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString EqsPath; Params->TryGetStringField(TEXT("eqs_path"), EqsPath);
	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *EqsPath);
	if (!Query) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("EQS not found: '%s'"), *EqsPath)); }
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Query->GetName());
	Data->SetNumberField(TEXT("option_count"), Query->GetOptions().Num());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleEqsAddOption(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("eqs_path"), TEXT("generator") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString EqsPath, GeneratorName;
	Params->TryGetStringField(TEXT("eqs_path"), EqsPath); Params->TryGetStringField(TEXT("generator"), GeneratorName);
	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *EqsPath);
	if (!Query) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("EQS not found: '%s'"), *EqsPath)); }

	// Resolve generator class (UEnvQueryGenerator subclass, fuzzy name).
	UClass* GenClass = nullptr;
	const TArray<FString> Cands = { GeneratorName, FString::Printf(TEXT("EnvQueryGenerator_%s"), *GeneratorName), FString::Printf(TEXT("UEnvQueryGenerator_%s"), *GeneratorName) };
	for (TObjectIterator<UClass> It; It && !GenClass; ++It)
	{
		UClass* C = *It;
		if (!C->IsChildOf(UEnvQueryGenerator::StaticClass()) || C->HasAnyClassFlags(CLASS_Abstract)) { continue; }
		for (const FString& Cand : Cands) { if (C->GetName().Equals(Cand, ESearchCase::IgnoreCase)) { GenClass = C; break; } }
	}
	if (!GenClass)
	{
		for (TObjectIterator<UClass> It; It && !GenClass; ++It)
		{
			UClass* C = *It;
			if (C->IsChildOf(UEnvQueryGenerator::StaticClass()) && !C->HasAnyClassFlags(CLASS_Abstract) && C->GetName().Contains(GeneratorName, ESearchCase::IgnoreCase)) { GenClass = C; }
		}
	}
	if (!GenClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown generator '%s' (try SimpleGrid/ActorsOfClass/OnCircle/Donut/Cone)"), *GeneratorName)); }

	// Ensure the EQS editor graph exists with a Root node.
	UEnvironmentQueryGraph* Graph = Cast<UEnvironmentQueryGraph>(Query->EdGraph);
	if (!Graph)
	{
		Query->EdGraph = FBlueprintEditorUtils::CreateNewGraph(Query, TEXT("EnvQuery"), UEnvironmentQueryGraph::StaticClass(), UEdGraphSchema_EnvironmentQuery::StaticClass());
		Graph = Cast<UEnvironmentQueryGraph>(Query->EdGraph);
		if (Graph)
		{
			const UEdGraphSchema* Schema = Graph->GetSchema();
			if (Schema) { Schema->CreateDefaultNodesForGraph(*Graph); }
			Graph->OnCreated();
		}
	}
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create/get the EQS graph")); }

	UEnvironmentQueryGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* N : Graph->Nodes) { if ((Root = Cast<UEnvironmentQueryGraphNode_Root>(N)) != nullptr) { break; } }

	FGraphNodeCreator<UEnvironmentQueryGraphNode_Option> Creator(*Graph);
	UEnvironmentQueryGraphNode_Option* OptNode = Creator.CreateNode();
	// The option graph node's NodeInstance is a UEnvQueryOption wrapping the generator
	// (UEnvironmentQueryGraph::UpdateAsset reads OptionInstance->Generator).
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
	Option->Generator = NewObject<UEnvQueryGenerator>(Query, GenClass);
	Option->Generator->UpdateNodeVersion();
	Option->Generator->SetFlags(RF_Transactional);
	OptNode->NodeInstance = Option;
	if (Root) { OptNode->NodePosX = Root->NodePosX; OptNode->NodePosY = Root->NodePosY + 150 + (Graph->Nodes.Num() % 5) * 30; }
	Creator.Finalize();

	if (Root)
	{
		UEdGraphPin* RootOut = Root->GetOutputPin();
		UEdGraphPin* OptIn = OptNode->GetInputPin();
		if (RootOut && OptIn) { RootOut->MakeLinkTo(OptIn); }
	}

	Graph->UpdateAsset();
	Query->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("generator"), GenClass->GetName());
	Data->SetNumberField(TEXT("option_count"), Query->GetOptions().Num());
	Data->SetStringField(TEXT("note"), TEXT("Option added + runtime rebuilt; call save_asset. Author tests on the option in the editor."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleEqsAddTest(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("eqs_path"), TEXT("test") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString EqsPath, TestName; int32 OptionIndex = -1;
	Params->TryGetStringField(TEXT("eqs_path"), EqsPath); Params->TryGetStringField(TEXT("test"), TestName);
	Params->TryGetNumberField(TEXT("option"), OptionIndex);
	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *EqsPath);
	if (!Query) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("EQS not found: '%s'"), *EqsPath)); }
	UEnvironmentQueryGraph* Graph = Cast<UEnvironmentQueryGraph>(Query->EdGraph);
	if (!Graph) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("EQS has no graph — add a generator option first (eqs_add_option)")); }

	// Collect the option (generator) graph nodes.
	TArray<UEnvironmentQueryGraphNode_Option*> Options;
	for (UEdGraphNode* N : Graph->Nodes) { if (UEnvironmentQueryGraphNode_Option* O = Cast<UEnvironmentQueryGraphNode_Option>(N)) { Options.Add(O); } }
	if (Options.Num() == 0) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("No generator option to add a test to (call eqs_add_option first)")); }
	UEnvironmentQueryGraphNode_Option* Option = (OptionIndex >= 0 && Options.IsValidIndex(OptionIndex)) ? Options[OptionIndex] : Options.Last();

	UClass* TestClass = ResolveSubclassByName(UEnvQueryTest::StaticClass(), TestName, { TEXT("EnvQueryTest_"), TEXT("UEnvQueryTest_") });
	if (!TestClass) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown test '%s' (try Distance/Dot/Trace/Pathfinding/Overlap/Project)"), *TestName)); }

	UEnvironmentQueryGraphNode_Test* TestNode = NewObject<UEnvironmentQueryGraphNode_Test>(Graph);
	TestNode->NodeInstance = NewObject<UEnvQueryTest>(Query, TestClass);
	Option->AddSubNode(TestNode, Graph);
	Graph->UpdateAsset();
	Query->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("test"), TestClass->GetName());
	Data->SetNumberField(TEXT("option"), Options.IndexOfByKey(Option));
	Data->SetNumberField(TEXT("test_count"), Option->SubNodes.Num());
	Data->SetStringField(TEXT("note"), TEXT("Test added to the option + runtime rebuilt; call save_asset."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// State Tree
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUEAICommands::HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString Name, Path; Params->TryGetStringField(TEXT("name"), Name); Params->TryGetStringField(TEXT("path"), Path);
	FString PackagePath; UPackage* Package = MakeAssetPackage(Path, Name, PackagePath);
	if (LoadObject<UStateTree>(nullptr, *PackagePath)) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("State Tree already exists at target path")); }

	// Reuse the engine factory (sets EditorData + schema + root state + initial compile).
	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	Factory->SetSchemaClass(UStateTreeComponentSchema::StaticClass());
	UObject* Obj = Factory->FactoryCreateNew(UStateTree::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
	UStateTree* ST = Cast<UStateTree>(Obj);
	if (!ST) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create/compile UStateTree (schema/compile error)")); }
	FAssetRegistryModule::AssetCreated(ST);
	ST->MarkPackageDirty();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_tree_path"), PackagePath);
	Data->SetStringField(TEXT("schema"), TEXT("StateTreeComponentSchema"));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleReadStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("state_tree_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString StPath; Params->TryGetStringField(TEXT("state_tree_path"), StPath);
	UStateTree* ST = LoadObject<UStateTree>(nullptr, *StPath);
	if (!ST) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("State Tree not found: '%s'"), *StPath)); }
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), ST->GetName());
	TArray<TSharedPtr<FJsonValue>> States;
	if (UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(ST->EditorData))
	{
		Data->SetStringField(TEXT("schema"), EditorData->Schema ? EditorData->Schema->GetClass()->GetName() : TEXT("None"));
		for (const UStateTreeState* Root : EditorData->SubTrees)
		{
			if (!Root) { continue; }
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Root->Name.ToString());
			S->SetNumberField(TEXT("children"), Root->Children.Num());
			States.Add(MakeShared<FJsonValueObject>(S));
		}
	}
	Data->SetArrayField(TEXT("root_states"), States);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

namespace
{
	UStateTreeState* FindStateByName(UStateTreeState* State, const FName& Name)
	{
		if (!State) { return nullptr; }
		if (State->Name == Name) { return State; }
		for (UStateTreeState* Child : State->Children)
		{
			if (UStateTreeState* Found = FindStateByName(Child, Name)) { return Found; }
		}
		return nullptr;
	}
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleStateTreeAddState(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("state_tree_path"), TEXT("state_name") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString StPath, StateName, ParentName;
	Params->TryGetStringField(TEXT("state_tree_path"), StPath); Params->TryGetStringField(TEXT("state_name"), StateName); Params->TryGetStringField(TEXT("parent"), ParentName);
	UStateTree* ST = LoadObject<UStateTree>(nullptr, *StPath);
	if (!ST) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("State Tree not found: '%s'"), *StPath)); }
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(ST->EditorData);
	if (!EditorData) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("State Tree has no EditorData")); }

	// Resolve parent: named state (searched under all roots) or the first root.
	UStateTreeState* Parent = nullptr;
	if (!ParentName.IsEmpty())
	{
		const FName PName(*ParentName);
		for (UStateTreeState* Root : EditorData->SubTrees) { if ((Parent = FindStateByName(Root, PName)) != nullptr) { break; } }
		if (!Parent) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent state '%s' not found"), *ParentName)); }
	}
	else
	{
		if (EditorData->SubTrees.Num() > 0) { Parent = EditorData->SubTrees[0]; }
		else { Parent = &EditorData->AddRootState(); }
	}

	EditorData->Modify();
	UStateTreeState& NewState = Parent->AddChildState(FName(*StateName));

	FStateTreeCompilerLog Log;
	const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(ST, Log);
	ST->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_name"), NewState.Name.ToString());
	Data->SetStringField(TEXT("parent"), Parent->Name.ToString());
	Data->SetBoolField(TEXT("compiled"), bCompiled);
	Data->SetStringField(TEXT("note"), TEXT("State added + recompiled in-memory; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAICommands::HandleStateTreeAddTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("state_tree_path"), TEXT("from_state"), TEXT("to_state") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString StPath, FromName, ToName, TriggerStr;
	Params->TryGetStringField(TEXT("state_tree_path"), StPath); Params->TryGetStringField(TEXT("from_state"), FromName);
	Params->TryGetStringField(TEXT("to_state"), ToName); Params->TryGetStringField(TEXT("trigger"), TriggerStr);
	UStateTree* ST = LoadObject<UStateTree>(nullptr, *StPath);
	if (!ST) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("State Tree not found: '%s'"), *StPath)); }
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(ST->EditorData);
	if (!EditorData) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("State Tree has no EditorData")); }

	UStateTreeState* From = nullptr; UStateTreeState* To = nullptr;
	const FName FromF(*FromName), ToF(*ToName);
	for (UStateTreeState* Root : EditorData->SubTrees)
	{
		if (!From) { From = FindStateByName(Root, FromF); }
		if (!To)   { To   = FindStateByName(Root, ToF); }
	}
	if (!From) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source state '%s' not found"), *FromName)); }
	if (!To)   { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target state '%s' not found"), *ToName)); }

	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	if (TriggerStr.Equals(TEXT("OnStateSucceeded"), ESearchCase::IgnoreCase)) { Trigger = EStateTreeTransitionTrigger::OnStateSucceeded; }
	else if (TriggerStr.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase)) { Trigger = EStateTreeTransitionTrigger::OnStateFailed; }

	From->Modify();
	From->AddTransition(Trigger, EStateTreeTransitionType::GotoState, To);

	FStateTreeCompilerLog Log;
	const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(ST, Log);
	ST->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("from_state"), FromName);
	Data->SetStringField(TEXT("to_state"), ToName);
	Data->SetStringField(TEXT("trigger"), TriggerStr.IsEmpty() ? TEXT("OnStateCompleted") : TriggerStr);
	Data->SetBoolField(TEXT("compiled"), bCompiled);
	Data->SetStringField(TEXT("note"), TEXT("Transition added + recompiled; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

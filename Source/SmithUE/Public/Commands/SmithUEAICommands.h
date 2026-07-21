// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * AI authoring tools: Blackboard, Behavior Tree, EQS, State Tree.
 * Requires AIModule (+ editor modules AIGraph/BehaviorTreeEditor/EnvironmentQueryEditor/
 * StateTreeEditorModule/GameplayStateTreeModule) and the StateTree/GameplayStateTree plugins.
 */
class FSmithUEAICommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	// Blackboard
	static TSharedPtr<FJsonObject> HandleCreateBlackboard(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBlackboardAddKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadBlackboard(const TSharedPtr<FJsonObject>& Params);
	// Behavior Tree
	static TSharedPtr<FJsonObject> HandleCreateBehaviorTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBtSetBlackboard(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadBehaviorTree(const TSharedPtr<FJsonObject>& Params);
	// EQS
	static TSharedPtr<FJsonObject> HandleCreateEqs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadEqs(const TSharedPtr<FJsonObject>& Params);
	// State Tree
	static TSharedPtr<FJsonObject> HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadStateTree(const TSharedPtr<FJsonObject>& Params);
};

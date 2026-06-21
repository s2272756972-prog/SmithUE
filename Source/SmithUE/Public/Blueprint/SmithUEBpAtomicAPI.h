#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FSmithUEToolRegistry;
class UBlueprint;
class UEdGraph;

class FSmithUEBpAtomicAPI
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

	static UBlueprint* LoadBlueprint(const FString& BpPath);
	static UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName);
	static FString CreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeClass, FVector2D Position, const TSharedPtr<FJsonObject>& ExtraParams);
	static bool ConnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName);
	static bool DisconnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName);
	static bool SetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value);
	static bool CompileBlueprint(UBlueprint* Blueprint, TArray<FString>& OutErrors, bool bSkipGarbageCollection = false);

private:
	static TSharedPtr<FJsonObject> HandleBpCreate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpAddFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpCreateNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpConnectPins(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpDisconnectPins(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpSetPinDefault(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpDeleteNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpAddVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpRemoveVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpAddComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpRemoveComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpSetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpBulkSetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpSetComponentCollision(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpCompile(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpOverrideFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpReparent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpCopyGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpRemoveGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpRenameGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpFixupSelfReferences(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpFixLocalVarScope(const TSharedPtr<FJsonObject>& Params);
};

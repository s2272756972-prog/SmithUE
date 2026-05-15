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
	static bool SetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value);
	static bool CompileBlueprint(UBlueprint* Blueprint, TArray<FString>& OutErrors, bool bSkipGarbageCollection = false);

private:
	static TSharedPtr<FJsonObject> HandleBpCreate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpAddFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpCreateNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpConnectPins(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpSetPinDefault(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpDeleteNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpAddVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpAddComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpSetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleBpCompile(const TSharedPtr<FJsonObject>& Params);
};

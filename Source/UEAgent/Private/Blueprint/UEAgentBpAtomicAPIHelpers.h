#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class FJsonObject;
class UBlueprint;
class UClass;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UFunction;

namespace UEAgentBpAtomicAPIHelpers
{
	FString NormalizeObjectPath(const FString& AssetPath);
	UClass* ResolveClassByName(const FString& ClassName, UClass* RequiredBaseClass, TCHAR ExpectedPrefix);
	FVector2D GetPositionFromJson(const TSharedPtr<FJsonObject>& Params);
	bool ResolvePinType(const FString& TypeName, FEdGraphPinType& OutPinType);
	UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidString);
	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName);
	UFunction* FindFunctionByName(const FString& FunctionPath);
	UEdGraph* ResolveMacroGraph(const FString& MacroPath);
	bool TryConnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName, FString& OutError);
	bool TrySetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value, FString& OutError);
	TSharedPtr<FJsonObject> MakeNodeResponse(const FString& NodeId);
	bool GetNamedTypeField(const TSharedPtr<FJsonObject>& ParamObject, FString& OutName, FString& OutType);
	void AppendJsonStringArray(TSharedPtr<FJsonObject> Data, const TCHAR* FieldName, const TArray<FString>& Strings);
}

// Copyright 2026, 123dx-svg. MIT License.

#include "Blueprint/UEAgentBpAtomicAPIHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "Utils/UEAgentCommonUtils.h"

namespace
{
	UClass* ResolveClassByNameInternal(const FString& Name, UClass* RequiredBaseClass, TCHAR ExpectedPrefix)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}

		const FString NamesToTry[] = { Name, ExpectedPrefix ? FString::Printf(TEXT("%c%s"), ExpectedPrefix, *Name) : FString() };
		for (const FString& Candidate : NamesToTry)
		{
			if (Candidate.IsEmpty())
			{
				continue;
			}

			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class || Class->HasAnyFlags(RF_ClassDefaultObject))
				{
					continue;
				}

				if (Class->GetName() == Candidate && (!RequiredBaseClass || Class->IsChildOf(RequiredBaseClass)))
				{
					return Class;
				}
			}
		}

		return nullptr;
	}
}

namespace UEAgentBpAtomicAPIHelpers
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

	UClass* ResolveClassByName(const FString& ClassName, UClass* RequiredBaseClass, TCHAR ExpectedPrefix)
	{
		return ResolveClassByNameInternal(ClassName, RequiredBaseClass, ExpectedPrefix);
	}

	FVector2D GetPositionFromJson(const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonObject>* PositionObject = nullptr;
		if (!Params.IsValid() || !Params->TryGetObjectField(TEXT("position"), PositionObject) || !PositionObject || !PositionObject->IsValid())
		{
			return FVector2D::ZeroVector;
		}

		double X = 0.0;
		double Y = 0.0;
		(*PositionObject)->TryGetNumberField(TEXT("x"), X);
		(*PositionObject)->TryGetNumberField(TEXT("y"), Y);
		return FVector2D(X, Y);
	}

	bool ResolvePinType(const FString& TypeName, FEdGraphPinType& OutPinType)
	{
		const FString Normalized = TypeName.TrimStartAndEnd();
		if (Normalized.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true; }
		if (Normalized.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("int32"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int; return true; }
		if (Normalized.Equals(TEXT("float"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Float; return true; }
		if (Normalized.Equals(TEXT("FString"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("string"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_String; return true; }
		if (Normalized.Equals(TEXT("FName"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("name"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name; return true; }
		if (Normalized.Equals(TEXT("FVector"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get(); return true; }
		if (Normalized.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase)) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get(); return true; }
		return false;
	}

	UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidString)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString().Equals(GuidString, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}

		return nullptr;
	}

	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) || Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UFunction* FindFunctionByName(const FString& FunctionPath)
	{
		FString ClassName;
		FString FunctionName;
		if (FunctionPath.Split(TEXT("::"), &ClassName, &FunctionName))
		{
			if (UClass* OwnerClass = ResolveClassByName(ClassName, UObject::StaticClass(), TEXT('U')))
			{
				return OwnerClass->FindFunctionByName(FName(*FunctionName));
			}
		}

		for (TObjectIterator<UFunction> It; It; ++It)
		{
			UFunction* Function = *It;
			if (Function && Function->GetName().Equals(FunctionPath, ESearchCase::IgnoreCase))
			{
				return Function;
			}
		}

		return nullptr;
	}

	UEdGraph* ResolveMacroGraph(const FString& MacroPath)
	{
		if (MacroPath.IsEmpty())
		{
			return nullptr;
		}

		if (UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *NormalizeObjectPath(MacroPath)))
		{
			return MacroGraph;
		}

		if (UBlueprint* MacroBlueprint = LoadObject<UBlueprint>(nullptr, *NormalizeObjectPath(MacroPath)))
		{
			return MacroBlueprint->MacroGraphs.Num() > 0 ? MacroBlueprint->MacroGraphs[0] : nullptr;
		}

		return nullptr;
	}

	bool TryConnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName, FString& OutError)
	{
		UEdGraphNode* SourceNode = FindNodeByGuid(Graph, SourceNodeId);
		UEdGraphNode* TargetNode = FindNodeByGuid(Graph, TargetNodeId);
		if (!SourceNode || !TargetNode) { OutError = TEXT("Source or target node not found"); return false; }

		UEdGraphPin* SourcePin = FindPin(SourceNode, SourcePinName);
		UEdGraphPin* TargetPin = FindPin(TargetNode, TargetPinName);
		if (!SourcePin || !TargetPin) { OutError = TEXT("Source or target pin not found"); return false; }

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Schema || !Schema->TryCreateConnection(SourcePin, TargetPin)) { OutError = TEXT("Schema rejected pin connection"); return false; }

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		OutError.Reset();
		return true;
	}

	bool TrySetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& NodeId, const FString& PinName, const FString& Value, FString& OutError)
	{
		UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
		if (!Node) { OutError = TEXT("Node not found"); return false; }

		UEdGraphPin* Pin = FindPin(Node, PinName);
		if (!Pin) { OutError = TEXT("Pin not found"); return false; }

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Schema) { OutError = TEXT("K2 schema unavailable"); return false; }

		Pin->Modify();
		Schema->TrySetDefaultValue(*Pin, Value);
		if (Pin->DefaultValue != Value && Pin->GetDefaultAsString() != Value) { OutError = TEXT("Failed to set pin default value"); return false; }

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		OutError.Reset();
		return true;
	}

	TSharedPtr<FJsonObject> MakeNodeResponse(const FString& NodeId)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_id"), NodeId);
		return FUEAgentCommonUtils::CreateSuccessResponse(Data);
	}

	bool GetNamedTypeField(const TSharedPtr<FJsonObject>& ParamObject, FString& OutName, FString& OutType)
	{
		return ParamObject.IsValid() && ParamObject->TryGetStringField(TEXT("name"), OutName) && ParamObject->TryGetStringField(TEXT("type"), OutType) && !OutName.IsEmpty() && !OutType.IsEmpty();
	}

	void AppendJsonStringArray(TSharedPtr<FJsonObject> Data, const TCHAR* FieldName, const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		JsonValues.Reserve(Strings.Num());
		for (const FString& StringValue : Strings)
		{
			JsonValues.Add(MakeShared<FJsonValueString>(StringValue));
		}
		Data->SetArrayField(FieldName, JsonValues);
	}
}

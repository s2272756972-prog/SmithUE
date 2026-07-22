// Copyright 2026, 123dx-svg. MIT License.

#include "SmithUEBpAtomicAPIHelpers.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "Utils/SmithUECommonUtils.h"

namespace
{
	UClass* ResolveClassByNameInternal(const FString& Name, UClass* RequiredBaseClass, TCHAR ExpectedPrefix)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}

		// Path-based resolution (handles Blueprint asset paths like /Game/Path/BP_Foo.BP_Foo_C).
		// Must run BEFORE the short-name TObjectIterator scan: the short name of a Blueprint
		// generated class (e.g. "ABP_Truck_Lift_C") will never match an asset path string.
		if (Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")))
		{
			// 1. Direct UClass load — covers native class paths and already-_C generated class paths.
			if (UClass* C = LoadObject<UClass>(nullptr, *Name))
			{
				if (!RequiredBaseClass || C->IsChildOf(RequiredBaseClass))
				{
					return C;
				}
			}
			// 2. Blueprint asset path (no _C suffix): load UBlueprint and return its GeneratedClass.
			if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Name))
			{
				if (BP->GeneratedClass && (!RequiredBaseClass || BP->GeneratedClass->IsChildOf(RequiredBaseClass)))
				{
					return BP->GeneratedClass;
				}
			}
			// 3. User passed the BP package path without _C — append it and retry.
			if (!Name.EndsWith(TEXT("_C")))
			{
				if (UClass* C = LoadObject<UClass>(nullptr, *(Name + TEXT("_C"))))
				{
					if (!RequiredBaseClass || C->IsChildOf(RequiredBaseClass))
					{
						return C;
					}
				}
			}
		}

		// Fallback: short-name scan over all loaded UClasses (with optional prefix guess).
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

namespace SmithUEBpAtomicAPIHelpers
{
	void AppendJsonStringArray(TSharedPtr<FJsonObject> Data, const TCHAR* FieldName, const TArray<FString>& Strings);

	static bool IsStructPinTypeWithName(const UEdGraphPin* Pin, const TCHAR* TypeName)
	{
		if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			return false;
		}

		const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get();
		return SubCategoryObject && SubCategoryObject->GetName().Contains(TypeName, ESearchCase::IgnoreCase);
	}

	static bool ParseNumberLikeString(const FString& Text, double& OutValue)
	{
		return LexTryParseString(OutValue, *Text.TrimStartAndEnd());
	}

	static bool GetJsonNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, double& OutValue)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		if (Object->TryGetNumberField(FieldName, OutValue))
		{
			return true;
		}

		FString StringValue;
		return Object->TryGetStringField(FieldName, StringValue) && ParseNumberLikeString(StringValue, OutValue);
	}

	static bool TryParseJsonObject(const FString& Input, TSharedPtr<FJsonObject>& OutObject)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	static bool TryParseNamedVectorLike(const FString& Input, bool bIsRotator, double& OutA, double& OutB, double& OutC)
	{
		TArray<FString> Pairs;
		Input.ParseIntoArray(Pairs, TEXT(","), true);

		bool bFoundAny = false;
		for (FString Pair : Pairs)
		{
			Pair = Pair.TrimStartAndEnd();
			FString Key;
			FString Value;
			if (!Pair.Split(TEXT("="), &Key, &Value))
			{
				return false;
			}

			Key = Key.TrimStartAndEnd();
			Value = Value.TrimStartAndEnd();

			double NumericValue = 0.0;
			if (!ParseNumberLikeString(Value, NumericValue))
			{
				return false;
			}

			if (bIsRotator)
			{
				if (Key.Equals(TEXT("Pitch"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("P"), ESearchCase::IgnoreCase))
				{
					OutA = NumericValue;
					bFoundAny = true;
				}
				else if (Key.Equals(TEXT("Yaw"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("Y"), ESearchCase::IgnoreCase))
				{
					OutB = NumericValue;
					bFoundAny = true;
				}
				else if (Key.Equals(TEXT("Roll"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("R"), ESearchCase::IgnoreCase))
				{
					OutC = NumericValue;
					bFoundAny = true;
				}
				else
				{
					return false;
				}
			}
			else
			{
				if (Key.Equals(TEXT("X"), ESearchCase::IgnoreCase))
				{
					OutA = NumericValue;
					bFoundAny = true;
				}
				else if (Key.Equals(TEXT("Y"), ESearchCase::IgnoreCase))
				{
					OutB = NumericValue;
					bFoundAny = true;
				}
				else if (Key.Equals(TEXT("Z"), ESearchCase::IgnoreCase))
				{
					OutC = NumericValue;
					bFoundAny = true;
				}
				else
				{
					return false;
				}
			}
		}

		return bFoundAny;
	}

	static bool TryParseCommaVectorLike(const FString& Input, double& OutA, double& OutB, double& OutC)
	{
		TArray<FString> Parts;
		Input.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3)
		{
			return false;
		}

		return ParseNumberLikeString(Parts[0].TrimStartAndEnd(), OutA)
			&& ParseNumberLikeString(Parts[1].TrimStartAndEnd(), OutB)
			&& ParseNumberLikeString(Parts[2].TrimStartAndEnd(), OutC);
	}

	FString NormalizeStructPinValue(UEdGraphPin* Pin, const FString& Input)
	{
		if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			return Input;
		}

		const bool bIsRotator = IsStructPinTypeWithName(Pin, TEXT("Rotator"));
		const bool bIsVector = IsStructPinTypeWithName(Pin, TEXT("Vector"));
		if (!bIsRotator && !bIsVector)
		{
			return Input;
		}

		if (bIsRotator)
		{
			FRotator RotatorValue;
			if (RotatorValue.InitFromString(Input))
			{
				return Input;
			}

			double A = 0.0;
			double B = 0.0;
			double C = 0.0;

			TSharedPtr<FJsonObject> JsonObject;
			if (TryParseJsonObject(Input, JsonObject))
			{
				if (GetJsonNumberField(JsonObject, TEXT("Pitch"), A) || GetJsonNumberField(JsonObject, TEXT("P"), A))
				{
				}
				if (GetJsonNumberField(JsonObject, TEXT("Yaw"), B) || GetJsonNumberField(JsonObject, TEXT("Y"), B))
				{
				}
				if (GetJsonNumberField(JsonObject, TEXT("Roll"), C) || GetJsonNumberField(JsonObject, TEXT("R"), C))
				{
				}
				return FString::Printf(TEXT("P=%f,Y=%f,R=%f"), A, B, C);
			}

			FString ValueToParse = Input.TrimStartAndEnd();
			if (ValueToParse.StartsWith(TEXT("(")) && ValueToParse.EndsWith(TEXT(")")))
			{
				ValueToParse = ValueToParse.Mid(1, ValueToParse.Len() - 2).TrimStartAndEnd();
			}

			if (ValueToParse.Contains(TEXT("=")))
			{
				if (TryParseNamedVectorLike(ValueToParse, true, A, B, C))
				{
					return FString::Printf(TEXT("P=%f,Y=%f,R=%f"), A, B, C);
				}
			}
			else if (TryParseCommaVectorLike(ValueToParse, A, B, C))
			{
				return FString::Printf(TEXT("P=%f,Y=%f,R=%f"), A, B, C);
			}

			return Input;
		}

		FVector VectorValue;
		if (VectorValue.InitFromString(Input))
		{
			return Input;
		}

		double A = 0.0;
		double B = 0.0;
		double C = 0.0;

		TSharedPtr<FJsonObject> JsonObject;
		if (TryParseJsonObject(Input, JsonObject))
		{
			GetJsonNumberField(JsonObject, TEXT("X"), A);
			GetJsonNumberField(JsonObject, TEXT("Y"), B);
			GetJsonNumberField(JsonObject, TEXT("Z"), C);
			return FString::Printf(TEXT("X=%f,Y=%f,Z=%f"), A, B, C);
		}

		FString ValueToParse = Input.TrimStartAndEnd();
		if (ValueToParse.StartsWith(TEXT("(")) && ValueToParse.EndsWith(TEXT(")")))
		{
			ValueToParse = ValueToParse.Mid(1, ValueToParse.Len() - 2).TrimStartAndEnd();
		}

		if (ValueToParse.Contains(TEXT("=")))
		{
			if (TryParseNamedVectorLike(ValueToParse, false, A, B, C))
			{
				return FString::Printf(TEXT("X=%f,Y=%f,Z=%f"), A, B, C);
			}
		}
		else if (TryParseCommaVectorLike(ValueToParse, A, B, C))
		{
			return FString::Printf(TEXT("X=%f,Y=%f,Z=%f"), A, B, C);
		}

		return Input;
	}

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

	FVector2D ComputeCascadeNodePosition(UEdGraph* Graph)
	{
		// Empty graph -> origin. Otherwise place the new node just right of the
		// existing bounding box, staggered vertically so repeated no-position
		// creations don't perfectly overlap. Each new node becomes the rightmost,
		// so successive nodes cascade left-to-right (readable, non-overlapping).
		if (!Graph || Graph->Nodes.Num() == 0)
		{
			return FVector2D::ZeroVector;
		}

		int32 MaxX = MIN_int32;
		int32 MinY = MAX_int32;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			MaxX = FMath::Max(MaxX, Node->NodePosX);
			MinY = FMath::Min(MinY, Node->NodePosY);
		}
		if (MaxX == MIN_int32)
		{
			return FVector2D::ZeroVector;
		}

		const int32 Stagger = (Graph->Nodes.Num() % 8) * 96; // avoid identical Y in the same column
		return FVector2D(static_cast<double>(MaxX) + 360.0, static_cast<double>(MinY) + Stagger);
	}

	// Internal: resolve a single (non-container) type name to PinCategory/SubCategoryObject
	static bool ResolveSingleType(const FString& Normalized, FEdGraphPinType& OutPinType)
	{
		// --- Primitives ---
		if (Normalized.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (Normalized.Equals(TEXT("byte"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("uint8"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			return true;
		}
		if (Normalized.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		if (Normalized.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
			return true;
		}
		if (Normalized.Equals(TEXT("float"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Float;
			return true;
		}
		if (Normalized.Equals(TEXT("double"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		// UE5 precision-agnostic real types (matches BP editor's "Float" which is actually real:double)
		if (Normalized.Equals(TEXT("real:double"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("real double"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = TEXT("double");
			return true;
		}
		if (Normalized.Equals(TEXT("real:float"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("real float"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = TEXT("float");
			return true;
		}
		if (Normalized.Equals(TEXT("real"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = TEXT("double");
			return true;
		}
		if (Normalized.Equals(TEXT("FString"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("string"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (Normalized.Equals(TEXT("FName"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("name"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (Normalized.Equals(TEXT("FText"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("text"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}

		// --- Common struct types (accept with or without the leading 'F' for convenience) ---
		if (Normalized.Equals(TEXT("FVector"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			return true;
		}
		if (Normalized.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			return true;
		}
		if (Normalized.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("transform"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			return true;
		}
		if (Normalized.Equals(TEXT("FLinearColor"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("color"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("linearcolor"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
			return true;
		}
		if (Normalized.Equals(TEXT("FVector2D"), ESearchCase::IgnoreCase) || Normalized.Equals(TEXT("vector2d"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
			return true;
		}
		if (Normalized.Equals(TEXT("FGuid"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FGuid>::Get();
			return true;
		}

		// --- Fallback struct: try to find UScriptStruct by name (e.g. "FMyCustomStruct") ---
		if (Normalized.StartsWith(TEXT("F")) && Normalized.Len() > 1)
		{
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				UScriptStruct* Struct = *It;
				if (Struct && Struct->GetName().Equals(Normalized, ESearchCase::IgnoreCase))
				{
					OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
					OutPinType.PinSubCategoryObject = Struct;
					return true;
				}
			}
		}

		// --- Soft object reference: "TSoftObjectPtr<ClassName>" or "SoftObject<ClassName>" ---
		{
			FString InnerType;
			if (Normalized.StartsWith(TEXT("TSoftObjectPtr<")) && Normalized.EndsWith(TEXT(">")))
			{
				InnerType = Normalized.Mid(15, Normalized.Len() - 16).TrimStartAndEnd();
			}
			else if (Normalized.StartsWith(TEXT("SoftObject<")) && Normalized.EndsWith(TEXT(">")))
			{
				InnerType = Normalized.Mid(11, Normalized.Len() - 12).TrimStartAndEnd();
			}
			if (!InnerType.IsEmpty())
			{
				// Strip pointer suffix
				InnerType.RemoveFromEnd(TEXT("*"));
				UClass* ResolvedClass = ResolveClassByName(InnerType, UObject::StaticClass(), TEXT('U'));
				if (!ResolvedClass) { ResolvedClass = ResolveClassByName(InnerType, UObject::StaticClass(), TEXT('A')); }
				if (ResolvedClass)
				{
					OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
					OutPinType.PinSubCategoryObject = ResolvedClass;
					return true;
				}
			}
		}

		// --- Class reference: "TSubclassOf<ClassName>" or "SubclassOf<ClassName>" ---
		{
			FString InnerType;
			if (Normalized.StartsWith(TEXT("TSubclassOf<")) && Normalized.EndsWith(TEXT(">")))
			{
				InnerType = Normalized.Mid(12, Normalized.Len() - 13).TrimStartAndEnd();
			}
			else if (Normalized.StartsWith(TEXT("SubclassOf<")) && Normalized.EndsWith(TEXT(">")))
			{
				InnerType = Normalized.Mid(11, Normalized.Len() - 12).TrimStartAndEnd();
			}
			if (!InnerType.IsEmpty())
			{
				InnerType.RemoveFromEnd(TEXT("*"));
				UClass* ResolvedClass = ResolveClassByName(InnerType, UObject::StaticClass(), TEXT('U'));
				if (!ResolvedClass) { ResolvedClass = ResolveClassByName(InnerType, UObject::StaticClass(), TEXT('A')); }
				if (ResolvedClass)
				{
					OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
					OutPinType.PinSubCategoryObject = ResolvedClass;
					return true;
				}
			}
		}

		// --- Object reference: "ClassName*" or "UClassName" or "AClassName" ---
		{
			FString ObjTypeName = Normalized;
			ObjTypeName.RemoveFromEnd(TEXT("*"));
			ObjTypeName = ObjTypeName.TrimStartAndEnd();
			if (!ObjTypeName.IsEmpty())
			{
				UClass* ResolvedClass = ResolveClassByName(ObjTypeName, UObject::StaticClass(), TEXT('U'));
				if (!ResolvedClass) { ResolvedClass = ResolveClassByName(ObjTypeName, UObject::StaticClass(), TEXT('A')); }
				// Also try loading as Blueprint-generated class (e.g. "/Game/Path/BP_MyActor")
				if (!ResolvedClass && ObjTypeName.Contains(TEXT("/")))
				{
					FString BpPathToLoad = ObjTypeName;
					if (BpPathToLoad.EndsWith(TEXT("_C")))
					{
						int32 DotIdx;
						if (BpPathToLoad.FindLastChar(TEXT('.'), DotIdx)) { BpPathToLoad = BpPathToLoad.Left(DotIdx); }
					}
					FString FullPath = BpPathToLoad;
					if (!FullPath.Contains(TEXT(".")))
					{
						FString AssetName = FPackageName::GetLongPackageAssetName(FullPath);
						FullPath = FString::Printf(TEXT("%s.%s"), *BpPathToLoad, *AssetName);
					}
					if (UBlueprint* LoadedBP = LoadObject<UBlueprint>(nullptr, *FullPath))
					{
						ResolvedClass = LoadedBP->GeneratedClass;
					}
				}
				if (ResolvedClass)
				{
					OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
					OutPinType.PinSubCategoryObject = ResolvedClass;
					return true;
				}
			}
		}

		return false;
	}

	bool ResolvePinType(const FString& TypeName, FEdGraphPinType& OutPinType)
	{
		FString Normalized = TypeName.TrimStartAndEnd();

		// --- Container types: TArray<InnerType> or InnerType[] ---
		if (Normalized.StartsWith(TEXT("TArray<")) && Normalized.EndsWith(TEXT(">")))
		{
			FString InnerType = Normalized.Mid(7, Normalized.Len() - 8).TrimStartAndEnd();
			OutPinType.ContainerType = EPinContainerType::Array;
			return ResolveSingleType(InnerType, OutPinType);
		}
		if (Normalized.EndsWith(TEXT("[]")))
		{
			FString InnerType = Normalized.LeftChop(2).TrimStartAndEnd();
			OutPinType.ContainerType = EPinContainerType::Array;
			return ResolveSingleType(InnerType, OutPinType);
		}

		// --- Container types: TSet<InnerType> ---
		if (Normalized.StartsWith(TEXT("TSet<")) && Normalized.EndsWith(TEXT(">")))
		{
			FString InnerType = Normalized.Mid(5, Normalized.Len() - 6).TrimStartAndEnd();
			OutPinType.ContainerType = EPinContainerType::Set;
			return ResolveSingleType(InnerType, OutPinType);
		}

		// --- Container types: TMap<KeyType, ValueType> ---
		if (Normalized.StartsWith(TEXT("TMap<")) && Normalized.EndsWith(TEXT(">")))
		{
			FString MapInner = Normalized.Mid(5, Normalized.Len() - 6).TrimStartAndEnd();
			// Find the separating comma (handle nested templates by counting angle brackets)
			int32 SplitIdx = INDEX_NONE;
			int32 Depth = 0;
			for (int32 i = 0; i < MapInner.Len(); ++i)
			{
				TCHAR Ch = MapInner[i];
				if (Ch == TEXT('<')) { ++Depth; }
				else if (Ch == TEXT('>')) { --Depth; }
				else if (Ch == TEXT(',') && Depth == 0) { SplitIdx = i; break; }
			}
			if (SplitIdx == INDEX_NONE) { return false; }
			FString KeyType = MapInner.Left(SplitIdx).TrimStartAndEnd();
			FString ValueType = MapInner.Mid(SplitIdx + 1).TrimStartAndEnd();

			OutPinType.ContainerType = EPinContainerType::Map;
			// Resolve key type into OutPinType (PinCategory, PinSubCategoryObject)
			if (!ResolveSingleType(KeyType, OutPinType)) { return false; }
			// Resolve value type into PinValueType
			FEdGraphPinType ValuePinType;
			if (!ResolveSingleType(ValueType, ValuePinType)) { return false; }
			OutPinType.PinValueType.TerminalCategory = ValuePinType.PinCategory;
			OutPinType.PinValueType.TerminalSubCategory = ValuePinType.PinSubCategory;
			OutPinType.PinValueType.TerminalSubCategoryObject = ValuePinType.PinSubCategoryObject;
			return true;
		}

		// --- Single (non-container) type ---
		return ResolveSingleType(Normalized, OutPinType);
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

	UEdGraphNode* ResolveNodeId(UEdGraph* Graph, const FString& GraphPath, const FString& NodeIdStr, FString& OutError)
	{
		// Detect N-id format: "N" followed by digits only
		if (NodeIdStr.StartsWith(TEXT("N")) && NodeIdStr.Mid(1).IsNumeric())
		{
			bool bIsStale = false;
			FGuid Guid = FSmithUEToolRegistry::Get().NidSession.ResolveNid(GraphPath, NodeIdStr, bIsStale);
			if (bIsStale)
			{
				OutError = TEXT("N-id map is stale (graph was modified since last bp_describe_graph). Call bp_describe_graph to refresh.");
				return nullptr;
			}
			if (!Guid.IsValid())
			{
				OutError = FString::Printf(TEXT("N-id %s not found in session. Call bp_describe_graph first."), *NodeIdStr);
				return nullptr;
			}
			return FindNodeByGuid(Graph, Guid.ToString(EGuidFormats::Digits));
		}
		// Fall through to GUID format (backward compat)
		return FindNodeByGuid(Graph, NodeIdStr);
	}

	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, TArray<FString>* OutSuggestions)
	{
		if (!Node)
		{
			return nullptr;
		}

		if (OutSuggestions)
		{
			OutSuggestions->Reset();
		}

		static const TMap<FString, FString> PinAliasTable = []()
		{
			TMap<FString, FString> Table;
			Table.Add(TEXT("true"), TEXT("then"));
			Table.Add(TEXT("false"), TEXT("else"));
			Table.Add(TEXT("exec"), TEXT("execute"));
			Table.Add(TEXT("execute"), TEXT("execute"));
			Table.Add(TEXT("return value"), TEXT("ReturnValue"));
			Table.Add(TEXT("return"), TEXT("ReturnValue"));
			Table.Add(TEXT("target"), TEXT("self"));
			Table.Add(TEXT("self"), TEXT("self"));
			return Table;
		}();

		auto AppendSuggestions = [OutSuggestions](const TArray<UEdGraphPin*>& Matches)
		{
			if (!OutSuggestions)
			{
				return;
			}

			for (UEdGraphPin* Match : Matches)
			{
				if (Match)
				{
					OutSuggestions->AddUnique(Match->PinName.ToString());
				}
			}
		};

		enum class EPinMatchResult : uint8
		{
			NoMatch,
			UniqueMatch,
			Ambiguous,
		};

		auto CollectMatches = [&](auto&& Predicate, UEdGraphPin*& OutPin) -> EPinMatchResult
		{
			TArray<UEdGraphPin*> Matches;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Predicate(Pin))
				{
					Matches.Add(Pin);
				}
			}

			if (Matches.Num() == 1)
			{
				OutPin = Matches[0];
				return EPinMatchResult::UniqueMatch;
			}

			if (Matches.Num() > 1)
			{
				AppendSuggestions(Matches);
				return EPinMatchResult::Ambiguous;
			}

			OutPin = nullptr;
			return EPinMatchResult::NoMatch;
		};

		// First pass: exact PinName match.
		UEdGraphPin* MatchedPin = nullptr;
		if (CollectMatches([&](UEdGraphPin* Candidate)
			{
				return Candidate->PinName.ToString() == PinName;
		}, MatchedPin) == EPinMatchResult::UniqueMatch)
		{
			return MatchedPin;
		}
		else if (MatchedPin == nullptr && OutSuggestions && !OutSuggestions->IsEmpty())
		{
			return nullptr;
		}

		// Second pass: case-insensitive PinName match.
		MatchedPin = nullptr;
		if (CollectMatches([&](UEdGraphPin* Candidate)
			{
				return Candidate->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase);
		}, MatchedPin) == EPinMatchResult::UniqueMatch)
		{
			return MatchedPin;
		}
		else if (MatchedPin == nullptr && OutSuggestions && !OutSuggestions->IsEmpty())
		{
			return nullptr;
		}

		// Third pass: case-insensitive DisplayName match.
		MatchedPin = nullptr;
		if (CollectMatches([&](UEdGraphPin* Candidate)
			{
				return Candidate->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase);
		}, MatchedPin) == EPinMatchResult::UniqueMatch)
		{
			return MatchedPin;
		}
		else if (MatchedPin == nullptr && OutSuggestions && !OutSuggestions->IsEmpty())
		{
			return nullptr;
		}

		// Fourth pass: alias table lookup.
		if (const FString* AliasedPinName = PinAliasTable.Find(PinName.ToLower()))
		{
			MatchedPin = nullptr;
			if (CollectMatches([&](UEdGraphPin* Candidate)
				{
					return Candidate->PinName.ToString() == *AliasedPinName;
				}, MatchedPin) == EPinMatchResult::UniqueMatch)
			{
				return MatchedPin;
			}

			else if (MatchedPin == nullptr && OutSuggestions && !OutSuggestions->IsEmpty())
			{
				return nullptr;
			}

			MatchedPin = nullptr;
			if (CollectMatches([&](UEdGraphPin* Candidate)
				{
					return Candidate->PinName.ToString().Equals(*AliasedPinName, ESearchCase::IgnoreCase);
				}, MatchedPin) == EPinMatchResult::UniqueMatch)
			{
				return MatchedPin;
			}
			else if (MatchedPin == nullptr && OutSuggestions && !OutSuggestions->IsEmpty())
			{
				return nullptr;
			}

			MatchedPin = nullptr;
			if (CollectMatches([&](UEdGraphPin* Candidate)
				{
					return Candidate->GetDisplayName().ToString().Equals(*AliasedPinName, ESearchCase::IgnoreCase);
				}, MatchedPin) == EPinMatchResult::UniqueMatch)
			{
				return MatchedPin;
			}
			else if (MatchedPin == nullptr && OutSuggestions && !OutSuggestions->IsEmpty())
			{
				return nullptr;
			}
		}

		return nullptr;
	}

	FString GetPinTypeString(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("<null>");
		}

		FString TypeString = Pin->PinType.PinCategory.ToString();
		if (const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get())
		{
			TypeString += TEXT(":");
			TypeString += SubCategoryObject->GetName();
		}

		return TypeString;
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

	bool TryConnectPins(UBlueprint* Blueprint, UEdGraph* Graph, const FString& GraphPath, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName, FString& OutError)
	{
		UEdGraphNode* SourceNode = ResolveNodeId(Graph, GraphPath, SourceNodeId, OutError);
		if (!SourceNode && !OutError.IsEmpty()) { return false; }
		UEdGraphNode* TargetNode = ResolveNodeId(Graph, GraphPath, TargetNodeId, OutError);
		if (!TargetNode && !OutError.IsEmpty()) { return false; }
		if (!SourceNode || !TargetNode) { OutError = TEXT("Source or target node not found"); return false; }

		TArray<FString> SourceSuggestions;
		UEdGraphPin* SourcePin = FindPin(SourceNode, SourcePinName, &SourceSuggestions);
		if (!SourcePin)
		{
			if (SourceSuggestions.Num() > 0)
			{
				TSharedPtr<FJsonObject> Err = FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Pin '%s' is ambiguous on node %s. Did you mean one of the suggestions?"),
						*SourcePinName, *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				AppendJsonStringArray(Err, TEXT("suggestions"), SourceSuggestions);
				Err->SetStringField(TEXT("hint"), TEXT("Use exact pin name from suggestions list"));
				OutError = FSmithUECommonUtils::SerializeJson(Err);
				return false;
			}
			OutError = TEXT("Source or target pin not found");
			return false;
		}

		TArray<FString> TargetSuggestions;
		UEdGraphPin* TargetPin = FindPin(TargetNode, TargetPinName, &TargetSuggestions);
		if (!TargetPin)
		{
			if (TargetSuggestions.Num() > 0)
			{
				TSharedPtr<FJsonObject> Err = FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Pin '%s' is ambiguous on node %s. Did you mean one of the suggestions?"),
						*TargetPinName, *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				AppendJsonStringArray(Err, TEXT("suggestions"), TargetSuggestions);
				Err->SetStringField(TEXT("hint"), TEXT("Use exact pin name from suggestions list"));
				OutError = FSmithUECommonUtils::SerializeJson(Err);
				return false;
			}
			OutError = TEXT("Source or target pin not found");
			return false;
		}

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Schema)
		{
			OutError = TEXT("Schema rejected pin connection (unexpected)");
			return false;
		}

		const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (Response.Response != CONNECT_RESPONSE_MAKE)
		{
			OutError = FString::Printf(TEXT("Pin connection rejected: %s. Source pin '%s' type: %s. Target pin '%s' type: %s"),
				*Response.Message.ToString(),
				*SourcePin->PinName.ToString(),
				*GetPinTypeString(SourcePin),
				*TargetPin->PinName.ToString(),
				*GetPinTypeString(TargetPin));
			return false;
		}

		if (!Schema->TryCreateConnection(SourcePin, TargetPin)) { OutError = TEXT("Schema rejected pin connection (unexpected)"); return false; }

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		OutError.Reset();
		return true;
	}

	bool TrySetPinDefault(UBlueprint* Blueprint, UEdGraph* Graph, const FString& GraphPath, const FString& NodeId, const FString& PinName, const FString& Value, FString& OutError)
	{
		UEdGraphNode* Node = ResolveNodeId(Graph, GraphPath, NodeId, OutError);
		if (!Node) { if (OutError.IsEmpty()) { OutError = TEXT("Node not found"); } return false; }

		TArray<FString> Suggestions;
		UEdGraphPin* Pin = FindPin(Node, PinName, &Suggestions);
		if (!Pin)
		{
			if (Suggestions.Num() > 0)
			{
				TSharedPtr<FJsonObject> Err = FSmithUECommonUtils::CreateErrorResponse(
					FString::Printf(TEXT("Pin '%s' is ambiguous on node %s. Did you mean one of the suggestions?"),
						*PinName, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				AppendJsonStringArray(Err, TEXT("suggestions"), Suggestions);
				Err->SetStringField(TEXT("hint"), TEXT("Use exact pin name from suggestions list"));
				OutError = FSmithUECommonUtils::SerializeJson(Err);
				return false;
			}
			OutError = TEXT("Pin not found");
			return false;
		}

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Schema) { OutError = TEXT("K2 schema unavailable"); return false; }

		const FString NormalizedValue = NormalizeStructPinValue(Pin, Value);

		Pin->Modify();
		Schema->TrySetDefaultValue(*Pin, NormalizedValue);
		if (Pin->DefaultValue != NormalizedValue && Pin->GetDefaultAsString() != NormalizedValue) { OutError = TEXT("Failed to set pin default value"); return false; }

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		OutError.Reset();
		return true;
	}

	TSharedPtr<FJsonObject> MakeNodeResponse(const FString& NodeId)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_id"), NodeId);
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
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

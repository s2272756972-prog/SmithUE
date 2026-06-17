// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEBlueprintCommands.h"
#include "Blueprint/SmithUEBpAtomicAPI.h"
#include "Blueprint/SmithUEBpAtomicAPIHelpers.h"
#include "Blueprint/SmithUEBpCompiler.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"

#include <initializer_list>

namespace
{
    FString CompileStatusToString(EBlueprintStatus Status)
    {
        switch (Status)
        {
        case BS_UpToDate: return TEXT("up_to_date");
        case BS_Dirty:    return TEXT("dirty");
        case BS_Error:    return TEXT("error");
        default:          return TEXT("unknown");
        }
    }

    FString PinTypeToString(const FEdGraphPinType& PinType)
    {
        FString Type = PinType.PinCategory.ToString();
		if (!PinType.PinSubCategory.IsNone())
        {
            Type += TEXT(":");
            Type += PinType.PinSubCategory.ToString();
        }
        if (PinType.PinSubCategoryObject != nullptr)
        {
            Type += TEXT("/");
            Type += PinType.PinSubCategoryObject->GetName();
        }
        // Wrap with container type if applicable
        if (PinType.ContainerType == EPinContainerType::Array)
        {
            Type = FString::Printf(TEXT("TArray<%s>"), *Type);
        }
        else if (PinType.ContainerType == EPinContainerType::Set)
        {
            Type = FString::Printf(TEXT("TSet<%s>"), *Type);
        }
        else if (PinType.ContainerType == EPinContainerType::Map)
        {
            // Build value type string
            FString ValueType = PinType.PinValueType.TerminalCategory.ToString();
            if (!PinType.PinValueType.TerminalSubCategory.IsNone())
            {
                ValueType += TEXT(":");
                ValueType += PinType.PinValueType.TerminalSubCategory.ToString();
            }
            if (PinType.PinValueType.TerminalSubCategoryObject != nullptr)
            {
                ValueType += TEXT("/");
                ValueType += PinType.PinValueType.TerminalSubCategoryObject->GetName();
            }
            Type = FString::Printf(TEXT("TMap<%s, %s>"), *Type, *ValueType);
        }
        return Type;
    }

	TSharedPtr<FJsonObject> MakeErrResp(const FString& Message)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Message);
    }

    TSharedPtr<FJsonObject> NodeToJson(UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

        TArray<TSharedPtr<FJsonValue>> Inputs;
        TArray<TSharedPtr<FJsonValue>> Outputs;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            if (Pin->Direction == EGPD_Input)
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("pin_type"), PinTypeToString(Pin->PinType));
                PinObj->SetStringField(TEXT("default_value"), Pin->GetDefaultAsString());

                if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0])
                {
                    UEdGraphPin* LinkedPin = Pin->LinkedTo[0];
                    UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                    if (!LinkedNode)
                    {
                        continue;
                    }
                    PinObj->SetStringField(
                        TEXT("connected_to"),
                        FString::Printf(TEXT("%s.%s"),
                            *LinkedNode->NodeGuid.ToString(),
                            *LinkedPin->PinName.ToString()));
                }

                Inputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
            else
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("pin_type"), PinTypeToString(Pin->PinType));

                TArray<TSharedPtr<FJsonValue>> Connections;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin || !LinkedPin->GetOwningNode())
                    {
                        continue;
                    }
                    Connections.Add(MakeShared<FJsonValueString>(
                        FString::Printf(TEXT("%s.%s"),
                            *LinkedPin->GetOwningNode()->NodeGuid.ToString(),
                            *LinkedPin->PinName.ToString())));
                }
                PinObj->SetArrayField(TEXT("connections"), Connections);
                Outputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }

        NodeObj->SetArrayField(TEXT("inputs"), Inputs);
        NodeObj->SetArrayField(TEXT("outputs"), Outputs);
        return NodeObj;
    }

    enum class ESmithUEMemberKind : uint8
    {
        Functions,
        Variables,
        Macros,
        Delegates,
        Interfaces
    };

    struct FSmithUEClassMemberBucket
    {
        int32 FunctionCount = 0;
        int32 VariableCount = 0;
        int32 MacroCount = 0;
        int32 DelegateCount = 0;
        int32 InterfaceCount = 0;

        TArray<TSharedPtr<FJsonValue>> Functions;
        TArray<TSharedPtr<FJsonValue>> Variables;
        TArray<TSharedPtr<FJsonValue>> Macros;
        TArray<TSharedPtr<FJsonValue>> Delegates;
        TArray<TSharedPtr<FJsonValue>> Interfaces;
    };

    TSet<ESmithUEMemberKind> ParseMemberKinds(const FString& KindsParam)
    {
        TSet<ESmithUEMemberKind> Kinds;
        if (KindsParam.IsEmpty())
        {
            Kinds.Add(ESmithUEMemberKind::Functions);
            Kinds.Add(ESmithUEMemberKind::Variables);
            Kinds.Add(ESmithUEMemberKind::Macros);
            Kinds.Add(ESmithUEMemberKind::Delegates);
            Kinds.Add(ESmithUEMemberKind::Interfaces);
            return Kinds;
        }

        TArray<FString> Parts;
        KindsParam.ParseIntoArray(Parts, TEXT(","), true);
        for (FString Part : Parts)
        {
            Part.TrimStartAndEndInline();
            Part.ToLowerInline();
            if (Part == TEXT("functions") || Part == TEXT("function"))
            {
                Kinds.Add(ESmithUEMemberKind::Functions);
            }
            else if (Part == TEXT("variables") || Part == TEXT("variable"))
            {
                Kinds.Add(ESmithUEMemberKind::Variables);
            }
            else if (Part == TEXT("macros") || Part == TEXT("macro"))
            {
                Kinds.Add(ESmithUEMemberKind::Macros);
            }
            else if (Part == TEXT("delegates") || Part == TEXT("delegate"))
            {
                Kinds.Add(ESmithUEMemberKind::Delegates);
            }
            else if (Part == TEXT("interfaces") || Part == TEXT("interface"))
            {
                Kinds.Add(ESmithUEMemberKind::Interfaces);
            }
        }
        return Kinds;
    }

    enum class ESmithUEComponentPropGroup : uint8
    {
        Mobility,
        Transform,
        Physics,
        Rendering,
        Mesh,
        Collision
    };

    TSet<ESmithUEComponentPropGroup> ParseComponentPropGroups(const FString& PropsParam)
    {
        TSet<ESmithUEComponentPropGroup> Groups;
        if (PropsParam.IsEmpty())
        {
            Groups.Add(ESmithUEComponentPropGroup::Mobility);
            Groups.Add(ESmithUEComponentPropGroup::Transform);
            Groups.Add(ESmithUEComponentPropGroup::Physics);
            Groups.Add(ESmithUEComponentPropGroup::Rendering);
            Groups.Add(ESmithUEComponentPropGroup::Mesh);
            Groups.Add(ESmithUEComponentPropGroup::Collision);
            return Groups;
        }

        TArray<FString> Parts;
        PropsParam.ParseIntoArray(Parts, TEXT(","), true);
        for (FString Part : Parts)
        {
            Part.TrimStartAndEndInline();
            Part.ToLowerInline();
            if (Part == TEXT("mobility"))
            {
                Groups.Add(ESmithUEComponentPropGroup::Mobility);
            }
            else if (Part == TEXT("transform"))
            {
                Groups.Add(ESmithUEComponentPropGroup::Transform);
            }
            else if (Part == TEXT("physics"))
            {
                Groups.Add(ESmithUEComponentPropGroup::Physics);
            }
            else if (Part == TEXT("rendering") || Part == TEXT("visibility") || Part == TEXT("materials"))
            {
                Groups.Add(ESmithUEComponentPropGroup::Rendering);
            }
            else if (Part == TEXT("mesh"))
            {
                Groups.Add(ESmithUEComponentPropGroup::Mesh);
            }
            else if (Part == TEXT("collision"))
            {
                Groups.Add(ESmithUEComponentPropGroup::Collision);
            }
        }
        return Groups;
    }

    bool WantsComponentGroup(const TSet<ESmithUEComponentPropGroup>& Groups, ESmithUEComponentPropGroup Group)
    {
        return Groups.Contains(Group);
    }

    bool ComponentNameMatchesFilter(const FString& ComponentName, const FString& ComponentFilter)
    {
        return ComponentFilter.IsEmpty() || ComponentName.Equals(ComponentFilter, ESearchCase::IgnoreCase);
    }

    TSharedPtr<FJsonObject> ComponentDetailsToJson(
        UActorComponent* Comp,
        const FString& Name,
        const FString& Source,
        const TSet<ESmithUEComponentPropGroup>& Groups)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Name);
        Obj->SetStringField(TEXT("class"), Comp ? Comp->GetClass()->GetName() : TEXT("None"));
        Obj->SetStringField(TEXT("source"), Source);

        if (!Comp)
        {
            return Obj;
        }

        if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
        {
            if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Mobility))
            {
                Obj->SetStringField(
                    TEXT("mobility"),
                    StaticEnum<EComponentMobility::Type>()->GetNameStringByValue(static_cast<int64>(SceneComp->Mobility)));
            }

            if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Transform))
            {
                TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
                Transform->SetStringField(TEXT("location"), SceneComp->GetRelativeLocation().ToString());
                Transform->SetStringField(TEXT("rotation"), SceneComp->GetRelativeRotation().ToString());
                Transform->SetStringField(TEXT("scale"), SceneComp->GetRelativeScale3D().ToString());
                Obj->SetObjectField(TEXT("transform"), Transform);

                TSharedPtr<FJsonObject> Absolute = MakeShared<FJsonObject>();
                Absolute->SetBoolField(TEXT("location"), SceneComp->IsUsingAbsoluteLocation());
                Absolute->SetBoolField(TEXT("rotation"), SceneComp->IsUsingAbsoluteRotation());
                Absolute->SetBoolField(TEXT("scale"), SceneComp->IsUsingAbsoluteScale());
                Obj->SetObjectField(TEXT("absolute"), Absolute);
            }

            if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Rendering))
            {
                TSharedPtr<FJsonObject> Visibility = MakeShared<FJsonObject>();
                Visibility->SetBoolField(TEXT("visible"), SceneComp->GetVisibleFlag());
                Visibility->SetBoolField(TEXT("hidden_in_game"), SceneComp->bHiddenInGame);
                Obj->SetObjectField(TEXT("visibility"), Visibility);
            }
        }

        if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(Comp))
        {
            if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Physics))
            {
                TSharedPtr<FJsonObject> Physics = MakeShared<FJsonObject>();
                Physics->SetBoolField(TEXT("simulate_physics"), PrimitiveComp->BodyInstance.bSimulatePhysics);
                Physics->SetBoolField(TEXT("enable_gravity"), PrimitiveComp->BodyInstance.bEnableGravity);
                Obj->SetObjectField(TEXT("physics"), Physics);
            }

            if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Collision))
            {
                TSharedPtr<FJsonObject> Collision = MakeShared<FJsonObject>();
                Collision->SetStringField(TEXT("profile"), PrimitiveComp->GetCollisionProfileName().ToString());
                Collision->SetStringField(
                    TEXT("enabled"),
                    StaticEnum<ECollisionEnabled::Type>()->GetNameStringByValue(static_cast<int64>(PrimitiveComp->GetCollisionEnabled())));
                Obj->SetObjectField(TEXT("collision"), Collision);
            }

            if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Rendering))
            {
                TArray<TSharedPtr<FJsonValue>> Materials;
                for (int32 Index = 0; Index < PrimitiveComp->GetNumMaterials(); ++Index)
                {
                    if (UMaterialInterface* Material = PrimitiveComp->GetMaterial(Index))
                    {
                        Materials.Add(MakeShared<FJsonValueString>(Material->GetPathName()));
                    }
                }
                if (Materials.Num() > 0)
                {
                    Obj->SetArrayField(TEXT("materials"), Materials);
                }
            }
        }

        if (WantsComponentGroup(Groups, ESmithUEComponentPropGroup::Mesh))
        {
            if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp))
            {
                if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
                {
                    Obj->SetStringField(TEXT("mesh"), StaticMesh->GetPathName());
                }
            }
            else if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Comp))
            {
                if (USkeletalMesh* SkeletalMesh = SkeletalMeshComp->GetSkeletalMeshAsset())
                {
                    Obj->SetStringField(TEXT("mesh"), SkeletalMesh->GetPathName());
                }
            }
        }

        return Obj;
    }

    TSharedPtr<FJsonValue> MakeCompactOrFullEntry(const FString& Name, bool bFull, const TSharedPtr<FJsonObject>& FullObj)
    {
        if (!bFull)
        {
            return MakeShared<FJsonValueString>(Name);
        }
        FullObj->SetStringField(TEXT("name"), Name);
        return MakeShared<FJsonValueObject>(FullObj);
    }

    bool TryAddLimited(TArray<TSharedPtr<FJsonValue>>& Target, const TSharedPtr<FJsonValue>& Value, int32 Limit, int32& Added, bool& bTruncated)
    {
        if (Added >= Limit)
        {
            bTruncated = true;
            return false;
        }
        Target.Add(Value);
        ++Added;
        return true;
    }

    FString FunctionSignatureToString(UFunction* Function)
    {
        if (!Function)
        {
            return TEXT("");
        }

        TArray<FString> Params;
        FString ReturnType = TEXT("void");
        for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Parm))
            {
                continue;
            }

            const FString PropType = Prop->GetCPPType();
            if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ReturnType = PropType;
            }
            else
            {
                Params.Add(FString::Printf(TEXT("%s %s"), *PropType, *Prop->GetName()));
            }
        }

        return FString::Printf(TEXT("%s %s(%s)"), *ReturnType, *Function->GetName(), *FString::Join(Params, TEXT(", ")));
    }

    TArray<TSharedPtr<FJsonValue>> FunctionFlagsToJson(UFunction* Function)
    {
        TArray<TSharedPtr<FJsonValue>> Flags;
        if (!Function)
        {
            return Flags;
        }
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintCallable")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintPure")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_Net))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Net")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Event")));
        }
        return Flags;
    }

    TArray<TSharedPtr<FJsonValue>> PropertyFlagsToJson(FProperty* Property)
    {
        TArray<TSharedPtr<FJsonValue>> Flags;
        if (!Property)
        {
            return Flags;
        }
        if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintReadOnly")));
        }
        if (Property->HasAnyPropertyFlags(CPF_Net))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Replicated")));
        }
        if (Property->HasAnyPropertyFlags(CPF_Edit))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("EditAnywhere")));
        }
        return Flags;
    }

    bool IsDelegateProperty(FProperty* Property)
    {
        return CastField<FMulticastDelegateProperty>(Property) != nullptr || CastField<FDelegateProperty>(Property) != nullptr;
    }

    TSharedPtr<FJsonValue> MakeFunctionMemberJson(UFunction* Function, bool bFull)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (bFull)
        {
            Obj->SetStringField(TEXT("signature"), FunctionSignatureToString(Function));
            Obj->SetArrayField(TEXT("flags"), FunctionFlagsToJson(Function));
        }
        return MakeCompactOrFullEntry(Function ? Function->GetName() : TEXT("None"), bFull, Obj);
    }

    TSharedPtr<FJsonValue> MakeVariableMemberJson(FProperty* Property, bool bFull)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (bFull && Property)
        {
            Obj->SetStringField(TEXT("type"), Property->GetCPPType());
            Obj->SetArrayField(TEXT("flags"), PropertyFlagsToJson(Property));
        }
        return MakeCompactOrFullEntry(Property ? Property->GetName() : TEXT("None"), bFull, Obj);
    }

    TSharedPtr<FJsonValue> MakeDelegateMemberJson(const FString& Name, bool bFull, bool bMulticast, const FString& Signature = TEXT(""))
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (bFull)
        {
            Obj->SetStringField(TEXT("kind"), bMulticast ? TEXT("multicast") : TEXT("single"));
            if (!Signature.IsEmpty())
            {
                Obj->SetStringField(TEXT("signature"), Signature);
            }
        }
        return MakeCompactOrFullEntry(Name, bFull, Obj);
    }

    TSharedPtr<FJsonValue> MakeNamedGraphMemberJson(UEdGraph* Graph, bool bFull)
    {
        const FString Name = Graph ? Graph->GetName() : TEXT("None");
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (bFull && Graph)
        {
            Obj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        }
        return MakeCompactOrFullEntry(Name, bFull, Obj);
    }

    UClass* ResolveNativeClass(const FString& ClassName)
    {
        TArray<FString> Candidates;
        Candidates.Add(ClassName);
        if (ClassName.Len() > 1 && (ClassName[0] == TEXT('A') || ClassName[0] == TEXT('U')))
        {
            Candidates.Add(ClassName.Mid(1));
        }
        else
        {
            Candidates.Add(FString::Printf(TEXT("A%s"), *ClassName));
            Candidates.Add(FString::Printf(TEXT("U%s"), *ClassName));
        }

        for (const FString& Candidate : Candidates)
        {
            if (UClass* FoundClass = UClass::TryFindTypeSlow<UClass>(*Candidate))
            {
                return FoundClass;
            }
        }
        return nullptr;
    }

    bool ClassNameMatchesScope(UClass* OwnerClass, const FString& OwnerFilter)
    {
        return OwnerFilter.IsEmpty() || (OwnerClass && OwnerClass->GetName() == OwnerFilter);
    }

    TArray<UEdGraph*> GetSmithUEBlueprintGraphs(UBlueprint* BP)
    {
        TArray<UEdGraph*> Graphs;
        if (!BP)
        {
            return Graphs;
        }

        auto AppendGraphs = [&Graphs](const TArray<UEdGraph*>& Source)
        {
            for (UEdGraph* Graph : Source)
            {
                if (Graph)
                {
                    Graphs.AddUnique(Graph);
                }
            }
        };

        AppendGraphs(BP->FunctionGraphs);
        AppendGraphs(BP->UbergraphPages);
        AppendGraphs(BP->MacroGraphs);
        return Graphs;
    }

    FString BlueprintPinDirectionToString(EEdGraphPinDirection Direction)
    {
        return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
    }

    bool IsExecPin(const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
    }

    bool IsPinRequiredForHealthCheck(const UEdGraphPin* Pin)
    {
        if (!Pin || Pin->bHidden || Pin->LinkedTo.Num() > 0)
        {
            return false;
        }

        const FName& Category = Pin->PinType.PinCategory;
        return Category == UEdGraphSchema_K2::PC_Exec ||
            Category == UEdGraphSchema_K2::PC_Delegate ||
            Category == UEdGraphSchema_K2::PC_Object ||
            Category == UEdGraphSchema_K2::PC_Interface ||
            Category == UEdGraphSchema_K2::PC_Class ||
            Category == UEdGraphSchema_K2::PC_SoftObject ||
            Category == UEdGraphSchema_K2::PC_SoftClass ||
            Category == UEdGraphSchema_K2::PC_Struct ||
            Pin->Direction == EGPD_Output ||
            Pin->GetDefaultAsString().IsEmpty();
    }

    TSet<FString> ParseNameFilter(const FString& Param, std::initializer_list<const TCHAR*> Defaults)
    {
        TSet<FString> Result;
        if (Param.IsEmpty())
        {
            for (const TCHAR* Item : Defaults)
            {
                Result.Add(FString(Item));
            }
            return Result;
        }

        TArray<FString> Parts;
        Param.ParseIntoArray(Parts, TEXT(","), true);
        for (FString Part : Parts)
        {
            Part.TrimStartAndEndInline();
            Part.ToLowerInline();
            if (!Part.IsEmpty())
            {
                Result.Add(Part);
            }
        }
        return Result;
    }

    void AddCompilerMessagesFromBlueprint(UBlueprint* BP, TArray<TSharedPtr<FJsonValue>>& OutMessages, int32& OutErrorCount, int32& OutWarningCount, int32 Limit)
    {
        int32 Added = 0;
        for (UEdGraph* Graph : GetSmithUEBlueprintGraphs(BP))
        {
            if (!Graph)
            {
                continue;
            }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node || !Node->bHasCompilerMessage || Node->ErrorMsg.IsEmpty())
                {
                    continue;
                }

                const bool bIsError = Node->ErrorType <= static_cast<int32>(EMessageSeverity::Error);
                if (bIsError)
                {
                    ++OutErrorCount;
                }
                else
                {
                    ++OutWarningCount;
                }

                if (Added >= Limit)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
                Message->SetStringField(TEXT("severity"), bIsError ? TEXT("error") : TEXT("warning"));
                Message->SetStringField(TEXT("message"), Node->ErrorMsg);
                Message->SetStringField(TEXT("graph"), Graph->GetName());
                Message->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                OutMessages.Add(MakeShared<FJsonValueObject>(Message));
                ++Added;
            }
        }
    }

    TSharedPtr<FJsonObject> MakeCountedItemsObject(int32 Count, const TArray<TSharedPtr<FJsonValue>>& Items)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("count"), Count);
        Obj->SetArrayField(TEXT("items"), Items);
        return Obj;
    }

    TSharedPtr<FJsonObject> MakeNamedDiffEntry(const FString& Name, const FString& A, const FString& B)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Name);
        Obj->SetStringField(TEXT("a"), A);
        Obj->SetStringField(TEXT("b"), B);
        return Obj;
    }

    TSharedPtr<FJsonObject> DiffNameMaps(const TMap<FString, FString>& A, const TMap<FString, FString>& B)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> OnlyA;
        TArray<TSharedPtr<FJsonValue>> OnlyB;
        TArray<TSharedPtr<FJsonValue>> Differs;

        for (const TPair<FString, FString>& Pair : A)
        {
            if (const FString* BValue = B.Find(Pair.Key))
            {
                if (*BValue != Pair.Value)
                {
                    Differs.Add(MakeShared<FJsonValueObject>(MakeNamedDiffEntry(Pair.Key, Pair.Value, *BValue)));
                }
            }
            else
            {
                OnlyA.Add(MakeShared<FJsonValueString>(Pair.Key));
            }
        }

        for (const TPair<FString, FString>& Pair : B)
        {
            if (!A.Contains(Pair.Key))
            {
                OnlyB.Add(MakeShared<FJsonValueString>(Pair.Key));
            }
        }

        Obj->SetArrayField(TEXT("only_in_a"), OnlyA);
        Obj->SetArrayField(TEXT("only_in_b"), OnlyB);
        Obj->SetArrayField(TEXT("differs"), Differs);
        return Obj;
    }

    TSharedPtr<FJsonObject> DiffNameSets(const TSet<FString>& A, const TSet<FString>& B)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> OnlyA;
        TArray<TSharedPtr<FJsonValue>> OnlyB;
        TArray<TSharedPtr<FJsonValue>> Differs;

        for (const FString& Name : A)
        {
            if (!B.Contains(Name))
            {
                OnlyA.Add(MakeShared<FJsonValueString>(Name));
            }
        }
        for (const FString& Name : B)
        {
            if (!A.Contains(Name))
            {
                OnlyB.Add(MakeShared<FJsonValueString>(Name));
            }
        }

        Obj->SetArrayField(TEXT("only_in_a"), OnlyA);
        Obj->SetArrayField(TEXT("only_in_b"), OnlyB);
        Obj->SetArrayField(TEXT("differs"), Differs);
        return Obj;
    }

    TMap<FString, FString> CollectComponentMap(UBlueprint* BP)
    {
        TMap<FString, FString> Components;
        if (BP && BP->SimpleConstructionScript)
        {
            for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
            {
                if (Node)
                {
                    Components.Add(Node->GetVariableName().ToString(), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None"));
                }
            }
        }
        return Components;
    }

    TMap<FString, FString> CollectVariableMap(UBlueprint* BP)
    {
        TMap<FString, FString> Variables;
        if (BP)
        {
            for (const FBPVariableDescription& Var : BP->NewVariables)
            {
                Variables.Add(Var.VarName.ToString(), PinTypeToString(Var.VarType));
            }
        }
        return Variables;
    }

    TSet<FString> CollectGraphNameSet(const TArray<UEdGraph*>& Graphs)
    {
        TSet<FString> Names;
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph)
            {
                Names.Add(Graph->GetName());
            }
        }
        return Names;
    }

    TSet<FString> CollectInterfaceSet(UBlueprint* BP)
    {
        TSet<FString> Interfaces;
        if (BP)
        {
            for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
            {
                if (Iface.Interface)
                {
                    Interfaces.Add(Iface.Interface->GetName());
                }
            }
        }
        return Interfaces;
    }

    TSet<FString> CollectOverrideSet(UBlueprint* BP)
    {
        TSet<FString> Overrides;
        if (!BP)
        {
            return Overrides;
        }

        for (UEdGraph* Graph : BP->FunctionGraphs)
        {
            if (!Graph)
            {
                continue;
            }
            UFunction* OverrideFunction = nullptr;
            if (FBlueprintEditorUtils::GetOverrideFunctionClass(BP, Graph->GetFName(), &OverrideFunction) && OverrideFunction)
            {
                Overrides.Add(Graph->GetName());
            }
        }

        for (UEdGraph* Graph : BP->UbergraphPages)
        {
            if (!Graph)
            {
                continue;
            }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
                {
                    if (EventNode->bOverrideFunction && EventNode->EventReference.GetMemberName() != NAME_None)
                    {
                        Overrides.Add(EventNode->EventReference.GetMemberName().ToString());
                    }
                }
            }
        }
        return Overrides;
    }

    UEdGraph* FindSmithUEGraphByName(UBlueprint* BP, const FString& GraphName)
    {
        for (UEdGraph* Graph : GetSmithUEBlueprintGraphs(BP))
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                return Graph;
            }
        }
        return nullptr;
    }

    UEdGraphNode* FindTraceNode(UEdGraph* Graph, const FString& NodeQuery)
    {
        if (!Graph || NodeQuery.IsEmpty())
        {
            return nullptr;
        }

        FGuid QueryGuid;
        const bool bHasGuid = FGuid::Parse(NodeQuery, QueryGuid);
        const FString QueryLower = NodeQuery.ToLower();
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            if (bHasGuid && Node->NodeGuid == QueryGuid)
            {
                return Node;
            }
            if (Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().ToLower().Contains(QueryLower))
            {
                return Node;
            }
        }
        return nullptr;
    }

    TSharedPtr<FJsonObject> BuildTraceTree(UEdGraphPin* Pin, bool bDownstream, int32 Depth, int32 MaxDepth, TSet<FString>& Visited, bool& bTruncated)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (!Pin || !Pin->GetOwningNode())
        {
            return Obj;
        }

        UEdGraphNode* Node = Pin->GetOwningNode();
        Obj->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        Obj->SetStringField(TEXT("pin"), Pin->PinName.ToString());
        Obj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

        TArray<TSharedPtr<FJsonValue>> Children;
        if (Depth >= MaxDepth)
        {
            if (Pin->LinkedTo.Num() > 0)
            {
                bTruncated = true;
            }
        }
        else
        {
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin || !LinkedPin->GetOwningNode() || IsExecPin(LinkedPin))
                {
                    continue;
                }
                const FString VisitKey = LinkedPin->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + LinkedPin->PinName.ToString();
                if (Visited.Contains(VisitKey))
                {
                    bTruncated = true;
                    continue;
                }
                Visited.Add(VisitKey);
                Children.Add(MakeShared<FJsonValueObject>(BuildTraceTree(LinkedPin, bDownstream, Depth + 1, MaxDepth, Visited, bTruncated)));
            }
        }

        Obj->SetArrayField(bDownstream ? TEXT("targets") : TEXT("sources"), Children);
        return Obj;
    }

    TSharedPtr<FJsonObject> WrapSuccess(TSharedPtr<FJsonObject> Data)
    {
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }
}

void FSmithUEBlueprintCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_get_summary"),
            TEXT("Blueprint"),
            TEXT("Get Blueprint metadata summary"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true)
            }),
        &HandleBpGetSummary);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_get_component_details"),
            TEXT("Blueprint"),
            TEXT("Read component template properties (mobility, transform, absolute flags, physics, collision, visibility, mesh, materials) for a Blueprint. Covers own SCS + inherited components. Closes the gap where bp_get_summary only shows hierarchy."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("component"), TEXT("string"), TEXT("Optional: only this component name. Empty = all.")),
                FSmithUEToolParam(TEXT("props"), TEXT("string"), TEXT("Optional comma filter: transform,mobility,physics,rendering,mesh,collision. Default all.")),
                FSmithUEToolParam(TEXT("include_inherited"), TEXT("boolean"), TEXT("Include inherited (parent/native) components. Default true."))
            }),
        &HandleBpGetComponentDetails);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_get_class_members"),
            TEXT("Blueprint"),
            TEXT("Get a Blueprint or native class's members grouped by owning class, with inheritance-chain attribution and token-conscious output controls."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path OR native C++ class name (e.g. ACarPawn)"), true),
                FSmithUEToolParam(TEXT("kinds"), TEXT("string"), TEXT("Comma list: functions,variables,macros,delegates,interfaces. Default all.")),
                FSmithUEToolParam(TEXT("scope"), TEXT("string"), TEXT("self (default, only members declared in this class) | chain (full inheritance chain grouped by owner) | owner:<ClassName>")),
                FSmithUEToolParam(TEXT("detail"), TEXT("string"), TEXT("compact (default, names only) | full (signatures, types, flags)")),
                FSmithUEToolParam(TEXT("limit"), TEXT("integer"), TEXT("Max total members returned. Default 200."))
            }),
        &HandleBpGetClassMembers);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_health_check"),
            TEXT("Blueprint"),
            TEXT("Aggregate Blueprint health diagnostics: compile messages, unconnected required pins, broken references, and orphan impure nodes."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("checks"), TEXT("string"), TEXT("Optional comma filter: compile,unconnected_pins,broken_refs,orphan_nodes. Default all.")),
                FSmithUEToolParam(TEXT("limit"), TEXT("integer"), TEXT("Max items/messages per check. Default 50."))
            }),
        &HandleBpHealthCheck);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_diff"),
            TEXT("Blueprint"),
            TEXT("Structural comparison of two Blueprints across parent, components, variables, functions, interfaces, and overrides."),
            {
                FSmithUEToolParam(TEXT("bp_path_a"), TEXT("string"), TEXT("First Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("bp_path_b"), TEXT("string"), TEXT("Second Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("aspects"), TEXT("string"), TEXT("Optional comma filter: parent,components,variables,functions,interfaces,overrides. Default all."))
            }),
        &HandleBpDiff);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_trace_value"),
            TEXT("Blueprint"),
            TEXT("Trace data-flow upstream or downstream from a node data pin in a Blueprint graph."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"), true),
                FSmithUEToolParam(TEXT("node"), TEXT("string"), TEXT("NodeGuid string or node title substring"), true),
                FSmithUEToolParam(TEXT("pin"), TEXT("string"), TEXT("Optional input pin name. Empty = all input data pins.")),
                FSmithUEToolParam(TEXT("direction"), TEXT("string"), TEXT("upstream (default) or downstream.")),
                FSmithUEToolParam(TEXT("max_depth"), TEXT("integer"), TEXT("Max recursive trace depth. Default 5."))
            }),
        &HandleBpTraceValue);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_describe_graph"),
            TEXT("Blueprint"),
            TEXT("Describe nodes in a Blueprint graph. mode: full(default)/compact/summary/node_pins/exec_chain. exec_chain mode follows exec pins from entry points (add entry_node param to start from specific N-id)."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"), true),
                FSmithUEToolParam(TEXT("entry_node"), TEXT("string"), TEXT("For exec_chain mode: N-id to start BFS from (default: all entry points)"))
            }),
        &HandleBpDescribeGraph);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_compile_code"),
            TEXT("Blueprint"),
            TEXT("Compile Blueprint DSL into a Blueprint"),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true),
                FSmithUEToolParam(TEXT("code"), TEXT("string"), TEXT("Blueprint DSL text"), true)
            }),
        &HandleBpCompileCode);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_batch_op"),
            TEXT("Blueprint"),
            TEXT("Execute multiple Blueprint atomic operations in a single transaction. Supports op aliases (connect/link/disconnect/unlink/set_default/set_value/create/add_node/delete/remove_node). Max 50 ops. Partial commit: failures do not stop subsequent ops."),
            {
                FSmithUEToolParam(TEXT("operations"), TEXT("array"), TEXT("Array of operation objects {op, params}. Max 50."), true),
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Shared Blueprint asset path injected into each op (op-level overrides)")),
                FSmithUEToolParam(TEXT("graph_name"), TEXT("string"), TEXT("Shared graph name injected into each op (op-level overrides)"))
            }),
        &HandleBpBatchOp);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_validate_code"),
            TEXT("Blueprint"),
            TEXT("Validate Blueprint DSL syntax without compiling"),
            {
                FSmithUEToolParam(TEXT("code"), TEXT("string"), TEXT("Blueprint DSL text"), true)
            }),
        &HandleBpValidateCode);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("bp_search"),
            TEXT("Blueprint"),
            TEXT("Search nodes in a Blueprint by name (substring, case-insensitive) and/or type (exact class name). Searches all graphs (event, function, macro)."),
            {
                FSmithUEToolParam(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints"), true),
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Substring to match against node title (case-insensitive). Empty = no filter.")),
                FSmithUEToolParam(TEXT("type"), TEXT("string"), TEXT("Exact node class name to match (e.g. 'K2Node_CallFunction'). Empty = no filter.")),
                FSmithUEToolParam(TEXT("verbose"), TEXT("boolean"), TEXT("If true, include pins (in/out) for each matched node. Default false.")),
                FSmithUEToolParam(TEXT("limit"), TEXT("integer"), TEXT("Maximum number of nodes to return. Default 100."))
            }),
        &HandleBpSearch);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpGetSummary(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
    {
		return MakeErrResp(TEXT("Missing required param: 'bp_path'"));
    }

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetPathName() : TEXT("None"));
    Data->SetStringField(TEXT("compile_status"), CompileStatusToString(BP->Status));

    // --- Interfaces ---
    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
    {
        if (Iface.Interface)
        {
            Interfaces.Add(MakeShared<FJsonValueString>(Iface.Interface->GetName()));
        }
    }
    Data->SetArrayField(TEXT("interfaces"), Interfaces);

    // --- Variables (with flags) ---
    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), PinTypeToString(Var.VarType));
        if (Var.PropertyFlags & CPF_BlueprintReadOnly)
        {
            VarObj->SetBoolField(TEXT("read_only"), true);
        }
        if (Var.PropertyFlags & CPF_Net)
        {
            VarObj->SetBoolField(TEXT("replicated"), true);
        }
        if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate ||
            Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
        {
            VarObj->SetBoolField(TEXT("is_delegate"), true);
        }
        Variables.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    Data->SetArrayField(TEXT("variables"), Variables);

    // --- Functions (with signatures) ---
    TArray<TSharedPtr<FJsonValue>> Functions;
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());
        FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        // Extract signature from FunctionEntry/FunctionResult nodes
        TArray<TSharedPtr<FJsonValue>> Inputs;
        TArray<TSharedPtr<FJsonValue>> Outputs;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            if (Node->GetClass()->GetName().Contains(TEXT("K2Node_FunctionEntry")))
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->Direction == EGPD_Output &&
                        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        Inputs.Add(MakeShared<FJsonValueString>(
                            FString::Printf(TEXT("%s:%s"), *Pin->PinName.ToString(), *PinTypeToString(Pin->PinType))));
                    }
                }
            }
            else if (Node->GetClass()->GetName().Contains(TEXT("K2Node_FunctionResult")))
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->Direction == EGPD_Input &&
                        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        Outputs.Add(MakeShared<FJsonValueString>(
                            FString::Printf(TEXT("%s:%s"), *Pin->PinName.ToString(), *PinTypeToString(Pin->PinType))));
                    }
                }
            }
        }
        if (Inputs.Num() > 0)
        {
            FuncObj->SetArrayField(TEXT("inputs"), Inputs);
        }
        if (Outputs.Num() > 0)
        {
            FuncObj->SetArrayField(TEXT("outputs"), Outputs);
        }
        Functions.Add(MakeShared<FJsonValueObject>(FuncObj));
    }
    Data->SetArrayField(TEXT("functions"), Functions);

    // --- Macro graphs ---
    TArray<TSharedPtr<FJsonValue>> Macros;
    for (UEdGraph* Graph : BP->MacroGraphs)
    {
        if (Graph)
        {
            TSharedPtr<FJsonObject> MacroObj = MakeShared<FJsonObject>();
            MacroObj->SetStringField(TEXT("name"), Graph->GetName());
            MacroObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
            Macros.Add(MakeShared<FJsonValueObject>(MacroObj));
        }
    }
    if (Macros.Num() > 0)
    {
        Data->SetArrayField(TEXT("macros"), Macros);
    }

    // --- Event graphs (with node count + custom events + input bindings) ---
    TArray<TSharedPtr<FJsonValue>> EventGraphs;
    TArray<TSharedPtr<FJsonValue>> CustomEvents;
    TArray<TSharedPtr<FJsonValue>> InputBindings;
    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
        GraphObj->SetStringField(TEXT("name"), Graph->GetName());
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        EventGraphs.Add(MakeShared<FJsonValueObject>(GraphObj));

        // Scan nodes for custom events and input bindings
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            const FString ClassName = Node->GetClass()->GetName();
            if (ClassName.Contains(TEXT("K2Node_CustomEvent")))
            {
                CustomEvents.Add(MakeShared<FJsonValueString>(
                    Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
            }
            else if (ClassName.Contains(TEXT("K2Node_InputKey")) ||
                     ClassName.Contains(TEXT("K2Node_InputAction")))
            {
                InputBindings.Add(MakeShared<FJsonValueString>(
                    Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
            }
        }
    }
    Data->SetArrayField(TEXT("event_graphs"), EventGraphs);
    if (CustomEvents.Num() > 0)
    {
        Data->SetArrayField(TEXT("custom_events"), CustomEvents);
    }
    if (InputBindings.Num() > 0)
    {
        Data->SetArrayField(TEXT("input_bindings"), InputBindings);
    }

    // --- Delegate signature graphs (event dispatchers) ---
    TArray<TSharedPtr<FJsonValue>> Delegates;
    for (UEdGraph* Graph : BP->DelegateSignatureGraphs)
    {
        if (Graph)
        {
            Delegates.Add(MakeShared<FJsonValueString>(Graph->GetName()));
        }
    }
    if (Delegates.Num() > 0)
    {
        Data->SetArrayField(TEXT("event_dispatchers"), Delegates);
    }

    // --- 组件列表 (含层级关系) ---
    // --- Components (with hierarchy) ---
    TArray<TSharedPtr<FJsonValue>> Components;
    if (USimpleConstructionScript* SCS = BP->SimpleConstructionScript)
    {
        // 构建父级查找表: 子节点 → 父节点
        // Build parent lookup: child node → parent node
        TMap<USCS_Node*, USCS_Node*> ParentMap;
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node) { continue; }
            for (USCS_Node* Child : Node->ChildNodes)
            {
                if (Child) { ParentMap.Add(Child, Node); }
            }
        }

        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node)
            {
                continue;
            }
            TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
            CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None"));
            // Show SCS tree parent
            if (USCS_Node** ParentPtr = ParentMap.Find(Node))
            {
                CompObj->SetStringField(TEXT("parent"), (*ParentPtr)->GetVariableName().ToString());
            }
            // Show attachment to inherited component
            if (Node->ParentComponentOrVariableName != NAME_None)
            {
                CompObj->SetStringField(TEXT("attached_to"), Node->ParentComponentOrVariableName.ToString());
            }
            if (Node->ChildNodes.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> Children;
                for (USCS_Node* Child : Node->ChildNodes)
                {
                    if (Child) { Children.Add(MakeShared<FJsonValueString>(Child->GetVariableName().ToString())); }
                }
                CompObj->SetArrayField(TEXT("children"), Children);
            }
            Components.Add(MakeShared<FJsonValueObject>(CompObj));
        }
    }
    Data->SetArrayField(TEXT("components"), Components);

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpGetComponentDetails(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
    {
        return MakeErrResp(TEXT("Missing required param: 'bp_path'"));
    }

    FString ComponentFilter;
    Params->TryGetStringField(TEXT("component"), ComponentFilter);
    ComponentFilter.TrimStartAndEndInline();

    FString PropsParam;
    Params->TryGetStringField(TEXT("props"), PropsParam);
    const TSet<ESmithUEComponentPropGroup> PropGroups = ParseComponentPropGroups(PropsParam);
    if (PropGroups.Num() == 0)
    {
        return MakeErrResp(FString::Printf(TEXT("No supported component property groups requested: %s"), *PropsParam));
    }

    bool bIncludeInherited = true;
    Params->TryGetBoolField(TEXT("include_inherited"), bIncludeInherited);

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    TArray<TSharedPtr<FJsonValue>> Components;
    TSet<FName> CoveredComponentNames;

    if (USimpleConstructionScript* SCS = BP->SimpleConstructionScript)
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node)
            {
                continue;
            }

            const FName ComponentName = Node->GetVariableName();
            CoveredComponentNames.Add(ComponentName);

            const FString ComponentNameString = ComponentName.ToString();
            if (!ComponentNameMatchesFilter(ComponentNameString, ComponentFilter))
            {
                continue;
            }

            Components.Add(MakeShared<FJsonValueObject>(ComponentDetailsToJson(
                Node->ComponentTemplate,
                ComponentNameString,
                TEXT("scs"),
                PropGroups)));
        }
    }

    if (bIncludeInherited)
    {
        if (UClass* GenClass = BP->GeneratedClass)
        {
            if (AActor* CDO = Cast<AActor>(GenClass->GetDefaultObject()))
            {
                TArray<UActorComponent*> CDOComponents;
                CDO->GetComponents(CDOComponents);
                for (UActorComponent* Comp : CDOComponents)
                {
                    if (!Comp || CoveredComponentNames.Contains(Comp->GetFName()))
                    {
                        continue;
                    }

                    const FString ComponentNameString = Comp->GetName();
                    if (!ComponentNameMatchesFilter(ComponentNameString, ComponentFilter))
                    {
                        continue;
                    }

                    Components.Add(MakeShared<FJsonValueObject>(ComponentDetailsToJson(
                        Comp,
                        ComponentNameString,
                        TEXT("inherited"),
                        PropGroups)));
                }
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetArrayField(TEXT("components"), Components);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpGetClassMembers(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPath;
    if (!Params->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
    {
        return MakeErrResp(TEXT("Missing required param: 'bp_path'"));
    }

    FString KindsParam;
    Params->TryGetStringField(TEXT("kinds"), KindsParam);
    const TSet<ESmithUEMemberKind> Kinds = ParseMemberKinds(KindsParam);
    if (Kinds.Num() == 0)
    {
        return MakeErrResp(FString::Printf(TEXT("No supported member kinds requested: %s"), *KindsParam));
    }

    FString Scope = TEXT("self");
    Params->TryGetStringField(TEXT("scope"), Scope);
    Scope.TrimStartAndEndInline();
    FString ScopeLower = Scope;
    ScopeLower.ToLowerInline();

    FString OwnerFilter;
    bool bScopeChain = false;
    if (ScopeLower == TEXT("self") || ScopeLower.IsEmpty())
    {
        bScopeChain = false;
    }
    else if (ScopeLower == TEXT("chain"))
    {
        bScopeChain = true;
    }
    else if (ScopeLower.StartsWith(TEXT("owner:")))
    {
        bScopeChain = true;
        OwnerFilter = Scope.Mid(6);
        OwnerFilter.TrimStartAndEndInline();
        if (OwnerFilter.IsEmpty())
        {
            return MakeErrResp(TEXT("scope owner:<ClassName> requires a class name"));
        }
    }
    else
    {
        return MakeErrResp(FString::Printf(TEXT("Unsupported scope: %s"), *Scope));
    }

    FString Detail = TEXT("compact");
    Params->TryGetStringField(TEXT("detail"), Detail);
    Detail.TrimStartAndEndInline();
    Detail.ToLowerInline();
    if (Detail.IsEmpty())
    {
        Detail = TEXT("compact");
    }
    if (Detail != TEXT("compact") && Detail != TEXT("full"))
    {
        return MakeErrResp(FString::Printf(TEXT("Unsupported detail: %s"), *Detail));
    }
    const bool bFullDetail = Detail == TEXT("full");

    int32 Limit = 200;
    double LimitParam = 0.0;
    if (Params->TryGetNumberField(TEXT("limit"), LimitParam) && LimitParam > 0.0)
    {
        Limit = FMath::Max(1, static_cast<int32>(LimitParam));
    }

    UBlueprint* BP = nullptr;
    UClass* Cls = nullptr;
    FString ResolvedFrom = TEXT("native");
    const bool bLooksLikeAssetPath = BpPath.StartsWith(TEXT("/Game")) || BpPath.StartsWith(TEXT("/Engine")) || BpPath.Contains(TEXT("."));
    if (bLooksLikeAssetPath)
    {
        BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
        Cls = BP ? BP->GeneratedClass : nullptr;
        ResolvedFrom = TEXT("blueprint");
    }
    else
    {
        Cls = ResolveNativeClass(BpPath);
    }

    if (!Cls)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to resolve Blueprint/class: %s"), *BpPath));
    }

    TArray<TSharedPtr<FJsonValue>> Chain;
    TArray<UClass*> ChainClasses;
    for (UClass* C = Cls; C != nullptr; C = C->GetSuperClass())
    {
        ChainClasses.Add(C);

        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("class"), C->GetName());
        if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C))
        {
            Node->SetStringField(TEXT("type"), TEXT("blueprint"));
            if (C->ClassGeneratedBy)
            {
                Node->SetStringField(TEXT("blueprint_path"), C->ClassGeneratedBy->GetPathName());
            }
        }
        else
        {
            Node->SetStringField(TEXT("type"), TEXT("native"));
            const FString Pkg = C->GetOutermost() ? C->GetOutermost()->GetName() : TEXT("");
            FString Module;
            Pkg.Split(TEXT("/Script/"), nullptr, &Module);
            Node->SetStringField(TEXT("module"), Module.IsEmpty() ? Pkg : Module);
        }
        Chain.Add(MakeShared<FJsonValueObject>(Node));

        if (C == UObject::StaticClass())
        {
            break;
        }
    }

    TMap<FString, FSmithUEClassMemberBucket> Buckets;
    auto ShouldIncludeOwner = [&OwnerFilter](UClass* OwnerClass) -> bool
    {
        return ClassNameMatchesScope(OwnerClass, OwnerFilter);
    };

    const EFieldIteratorFlags::SuperClassFlags FieldFlags = bScopeChain
        ? EFieldIteratorFlags::IncludeSuper
        : EFieldIteratorFlags::ExcludeSuper;

    if (Kinds.Contains(ESmithUEMemberKind::Functions))
    {
        for (TFieldIterator<UFunction> It(Cls, FieldFlags); It; ++It)
        {
            UFunction* Function = *It;
            UClass* OwnerClass = Function ? Function->GetOwnerClass() : nullptr;
            if (!OwnerClass || !ShouldIncludeOwner(OwnerClass))
            {
                continue;
            }
            ++Buckets.FindOrAdd(OwnerClass->GetName()).FunctionCount;
        }
    }

    if (Kinds.Contains(ESmithUEMemberKind::Variables) || Kinds.Contains(ESmithUEMemberKind::Delegates))
    {
        for (TFieldIterator<FProperty> It(Cls, FieldFlags); It; ++It)
        {
            FProperty* Property = *It;
            UClass* OwnerClass = Property ? Property->GetOwnerClass() : nullptr;
            if (!OwnerClass || !ShouldIncludeOwner(OwnerClass))
            {
                continue;
            }

            const bool bIsDelegate = IsDelegateProperty(Property);
            FSmithUEClassMemberBucket& Bucket = Buckets.FindOrAdd(OwnerClass->GetName());
            if (bIsDelegate)
            {
                if (Kinds.Contains(ESmithUEMemberKind::Delegates))
                {
                    ++Bucket.DelegateCount;
                }
            }
            else if (Kinds.Contains(ESmithUEMemberKind::Variables))
            {
                ++Bucket.VariableCount;
            }
        }
    }

    if (Kinds.Contains(ESmithUEMemberKind::Interfaces))
    {
        for (UClass* C : ChainClasses)
        {
            if (!C || !ShouldIncludeOwner(C))
            {
                continue;
            }

            const UClass* SuperClass = C->GetSuperClass();
            for (const FImplementedInterface& Interface : C->Interfaces)
            {
                if (!Interface.Class)
                {
                    continue;
                }

                bool bInherited = false;
                if (SuperClass)
                {
                    for (const FImplementedInterface& SuperInterface : SuperClass->Interfaces)
                    {
                        if (SuperInterface.Class == Interface.Class)
                        {
                            bInherited = true;
                            break;
                        }
                    }
                }

                if (!bInherited)
                {
                    ++Buckets.FindOrAdd(C->GetName()).InterfaceCount;
                }
            }
        }
    }

    if (Kinds.Contains(ESmithUEMemberKind::Macros))
    {
        for (UClass* C : ChainClasses)
        {
            if (!C || !ShouldIncludeOwner(C) || !Cast<UBlueprintGeneratedClass>(C))
            {
                continue;
            }

            UBlueprint* SourceBP = Cast<UBlueprint>(C->ClassGeneratedBy);
            if (!SourceBP)
            {
                continue;
            }

            Buckets.FindOrAdd(C->GetName()).MacroCount += SourceBP->MacroGraphs.Num();
        }
    }

    if (Kinds.Contains(ESmithUEMemberKind::Delegates) && BP && ShouldIncludeOwner(Cls))
    {
        Buckets.FindOrAdd(Cls->GetName()).DelegateCount += BP->DelegateSignatureGraphs.Num();
    }

    int32 Added = 0;
    bool bTruncated = false;

    if (Kinds.Contains(ESmithUEMemberKind::Functions))
    {
        for (TFieldIterator<UFunction> It(Cls, FieldFlags); It; ++It)
        {
            UFunction* Function = *It;
            UClass* OwnerClass = Function ? Function->GetOwnerClass() : nullptr;
            if (!OwnerClass || !ShouldIncludeOwner(OwnerClass))
            {
                continue;
            }
            if (!TryAddLimited(Buckets.FindOrAdd(OwnerClass->GetName()).Functions, MakeFunctionMemberJson(Function, bFullDetail), Limit, Added, bTruncated))
            {
                break;
            }
        }
    }

    if (!bTruncated && (Kinds.Contains(ESmithUEMemberKind::Variables) || Kinds.Contains(ESmithUEMemberKind::Delegates)))
    {
        for (TFieldIterator<FProperty> It(Cls, FieldFlags); It; ++It)
        {
            FProperty* Property = *It;
            UClass* OwnerClass = Property ? Property->GetOwnerClass() : nullptr;
            if (!OwnerClass || !ShouldIncludeOwner(OwnerClass))
            {
                continue;
            }

            FSmithUEClassMemberBucket& Bucket = Buckets.FindOrAdd(OwnerClass->GetName());
            if (IsDelegateProperty(Property))
            {
                if (Kinds.Contains(ESmithUEMemberKind::Delegates))
                {
                    const bool bMulticast = CastField<FMulticastDelegateProperty>(Property) != nullptr;
                    FString Signature;
                    if (bFullDetail)
                    {
                        if (FMulticastDelegateProperty* MultiDelegate = CastField<FMulticastDelegateProperty>(Property))
                        {
                            Signature = FunctionSignatureToString(MultiDelegate->SignatureFunction);
                        }
                        else if (FDelegateProperty* Delegate = CastField<FDelegateProperty>(Property))
                        {
                            Signature = FunctionSignatureToString(Delegate->SignatureFunction);
                        }
                    }
                    if (!TryAddLimited(Bucket.Delegates, MakeDelegateMemberJson(Property->GetName(), bFullDetail, bMulticast, Signature), Limit, Added, bTruncated))
                    {
                        break;
                    }
                }
            }
            else if (Kinds.Contains(ESmithUEMemberKind::Variables))
            {
                if (!TryAddLimited(Bucket.Variables, MakeVariableMemberJson(Property, bFullDetail), Limit, Added, bTruncated))
                {
                    break;
                }
            }
        }
    }

    if (!bTruncated && Kinds.Contains(ESmithUEMemberKind::Delegates) && BP && ShouldIncludeOwner(Cls))
    {
        FSmithUEClassMemberBucket& Bucket = Buckets.FindOrAdd(Cls->GetName());
        for (UEdGraph* Graph : BP->DelegateSignatureGraphs)
        {
            if (!Graph)
            {
                continue;
            }
            if (!TryAddLimited(Bucket.Delegates, MakeDelegateMemberJson(Graph->GetName(), bFullDetail, true), Limit, Added, bTruncated))
            {
                break;
            }
        }
    }

    if (!bTruncated && Kinds.Contains(ESmithUEMemberKind::Interfaces))
    {
        for (UClass* C : ChainClasses)
        {
            if (!C || !ShouldIncludeOwner(C))
            {
                continue;
            }

            const UClass* SuperClass = C->GetSuperClass();
            for (const FImplementedInterface& Interface : C->Interfaces)
            {
                if (!Interface.Class)
                {
                    continue;
                }

                bool bInherited = false;
                if (SuperClass)
                {
                    for (const FImplementedInterface& SuperInterface : SuperClass->Interfaces)
                    {
                        if (SuperInterface.Class == Interface.Class)
                        {
                            bInherited = true;
                            break;
                        }
                    }
                }

                if (!bInherited)
                {
                    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                    if (bFullDetail)
                    {
                        Obj->SetStringField(TEXT("path"), Interface.Class->GetPathName());
                    }
                    if (!TryAddLimited(Buckets.FindOrAdd(C->GetName()).Interfaces, MakeCompactOrFullEntry(Interface.Class->GetName(), bFullDetail, Obj), Limit, Added, bTruncated))
                    {
                        break;
                    }
                }
            }
            if (bTruncated)
            {
                break;
            }
        }
    }

    if (!bTruncated && Kinds.Contains(ESmithUEMemberKind::Macros))
    {
        for (UClass* C : ChainClasses)
        {
            if (!C || !ShouldIncludeOwner(C) || !Cast<UBlueprintGeneratedClass>(C))
            {
                continue;
            }

            UBlueprint* SourceBP = Cast<UBlueprint>(C->ClassGeneratedBy);
            if (!SourceBP)
            {
                continue;
            }

            FSmithUEClassMemberBucket& Bucket = Buckets.FindOrAdd(C->GetName());
            for (UEdGraph* Graph : SourceBP->MacroGraphs)
            {
                if (!Graph)
                {
                    continue;
                }
                if (!TryAddLimited(Bucket.Macros, MakeNamedGraphMemberJson(Graph, bFullDetail), Limit, Added, bTruncated))
                {
                    break;
                }
            }
            if (bTruncated)
            {
                break;
            }
        }
    }

    TSharedPtr<FJsonObject> CountsObj = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> MembersObj = MakeShared<FJsonObject>();

    auto EmitBucket = [&Buckets, &CountsObj, &MembersObj](const FString& OwnerName)
    {
        FSmithUEClassMemberBucket* Bucket = Buckets.Find(OwnerName);
        if (!Bucket)
        {
            return;
        }

        TSharedPtr<FJsonObject> OwnerCounts = MakeShared<FJsonObject>();
        OwnerCounts->SetNumberField(TEXT("functions"), Bucket->FunctionCount);
        OwnerCounts->SetNumberField(TEXT("variables"), Bucket->VariableCount);
        OwnerCounts->SetNumberField(TEXT("macros"), Bucket->MacroCount);
        OwnerCounts->SetNumberField(TEXT("delegates"), Bucket->DelegateCount);
        OwnerCounts->SetNumberField(TEXT("interfaces"), Bucket->InterfaceCount);
        CountsObj->SetObjectField(OwnerName, OwnerCounts);

        TSharedPtr<FJsonObject> OwnerMembers = MakeShared<FJsonObject>();
        OwnerMembers->SetArrayField(TEXT("functions"), Bucket->Functions);
        OwnerMembers->SetArrayField(TEXT("variables"), Bucket->Variables);
        OwnerMembers->SetArrayField(TEXT("delegates"), Bucket->Delegates);
        OwnerMembers->SetArrayField(TEXT("interfaces"), Bucket->Interfaces);
        OwnerMembers->SetArrayField(TEXT("macros"), Bucket->Macros);
        MembersObj->SetObjectField(OwnerName, OwnerMembers);
    };

    if (!OwnerFilter.IsEmpty())
    {
        EmitBucket(OwnerFilter);
    }
    else
    {
        for (UClass* C : ChainClasses)
        {
            if (C)
            {
                EmitBucket(C->GetName());
            }
            if (!bScopeChain)
            {
                break;
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("class"), Cls->GetName());
    Data->SetStringField(TEXT("resolved_from"), ResolvedFrom);
    Data->SetArrayField(TEXT("inheritance_chain"), Chain);
    Data->SetObjectField(TEXT("counts"), CountsObj);
    Data->SetObjectField(TEXT("members"), MembersObj);
    Data->SetBoolField(TEXT("truncated"), bTruncated);

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpHealthCheck(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    FString ChecksParam;
    Params->TryGetStringField(TEXT("checks"), ChecksParam);
    const TSet<FString> Checks = ParseNameFilter(ChecksParam, {TEXT("compile"), TEXT("unconnected_pins"), TEXT("broken_refs"), TEXT("orphan_nodes")});
    int32 Limit = 50;
    double LimitParam = 0.0;
    if (Params->TryGetNumberField(TEXT("limit"), LimitParam) && LimitParam > 0.0)
    {
        Limit = FMath::Max(1, static_cast<int32>(LimitParam));
    }

    bool bHealthy = true;
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);

    const TArray<UEdGraph*> Graphs = GetSmithUEBlueprintGraphs(BP);
    if (Checks.Contains(TEXT("compile")))
    {
        FKismetEditorUtilities::CompileBlueprint(BP);
        int32 ErrorCount = 0;
        int32 WarningCount = 0;
        TArray<TSharedPtr<FJsonValue>> Messages;
        AddCompilerMessagesFromBlueprint(BP, Messages, ErrorCount, WarningCount, Limit);

        TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
        Compile->SetStringField(TEXT("status"), CompileStatusToString(BP->Status));
        Compile->SetNumberField(TEXT("error_count"), ErrorCount);
        Compile->SetNumberField(TEXT("warning_count"), WarningCount);
        Compile->SetArrayField(TEXT("messages"), Messages);
        Data->SetObjectField(TEXT("compile"), Compile);
        if (ErrorCount > 0 || BP->Status == BS_Error)
        {
            bHealthy = false;
        }
    }

    if (Checks.Contains(TEXT("unconnected_pins")))
    {
        int32 Count = 0;
        TArray<TSharedPtr<FJsonValue>> Items;
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!IsPinRequiredForHealthCheck(Pin))
                    {
                        continue;
                    }
                    ++Count;
                    if (Items.Num() >= Limit)
                    {
                        continue;
                    }
                    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                    Item->SetStringField(TEXT("graph"), Graph->GetName());
                    Item->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                    Item->SetStringField(TEXT("pin"), Pin->PinName.ToString());
                    Item->SetStringField(TEXT("direction"), BlueprintPinDirectionToString(Pin->Direction));
                    Items.Add(MakeShared<FJsonValueObject>(Item));
                }
            }
        }
        Data->SetObjectField(TEXT("unconnected_pins"), MakeCountedItemsObject(Count, Items));
    }

    if (Checks.Contains(TEXT("broken_refs")))
    {
        int32 Count = 0;
        TArray<TSharedPtr<FJsonValue>> Items;
        auto AddBrokenRef = [&](UEdGraph* Graph, UEdGraphNode* Node, const FString& Missing)
        {
            ++Count;
            if (Items.Num() >= Limit)
            {
                return;
            }
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("graph"), Graph ? Graph->GetName() : TEXT(""));
            Item->SetStringField(TEXT("node"), Node ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT(""));
            Item->SetStringField(TEXT("missing"), Missing);
            Items.Add(MakeShared<FJsonValueObject>(Item));
        };

        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }
                if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
                {
                    if (!VarNode->GetPropertyForVariable() && VarNode->VariableReference.GetMemberName() != NAME_None)
                    {
                        AddBrokenRef(Graph, Node, VarNode->VariableReference.GetMemberName().ToString());
                    }
                }
                if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
                {
                    if (!CallNode->GetTargetFunction() && CallNode->FunctionReference.GetMemberName() != NAME_None)
                    {
                        AddBrokenRef(Graph, Node, CallNode->FunctionReference.GetMemberName().ToString());
                    }
                }
                if (UK2Node_ComponentBoundEvent* ComponentEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
                {
                    if (!ComponentEvent->GetTargetDelegateProperty() && ComponentEvent->ComponentPropertyName != NAME_None)
                    {
                        AddBrokenRef(Graph, Node, ComponentEvent->ComponentPropertyName.ToString());
                    }
                }
            }
        }
        Data->SetObjectField(TEXT("broken_refs"), MakeCountedItemsObject(Count, Items));
        if (Count > 0)
        {
            bHealthy = false;
        }
    }

    if (Checks.Contains(TEXT("orphan_nodes")))
    {
        int32 Count = 0;
        TArray<TSharedPtr<FJsonValue>> Items;
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node || Cast<UK2Node_Event>(Node) || Cast<UK2Node_FunctionEntry>(Node))
                {
                    continue;
                }

                bool bHasUnlinkedExecInput = false;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (IsExecPin(Pin) && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0)
                    {
                        bHasUnlinkedExecInput = true;
                        break;
                    }
                }
                if (!bHasUnlinkedExecInput)
                {
                    continue;
                }

                ++Count;
                if (Items.Num() >= Limit)
                {
                    continue;
                }
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("graph"), Graph->GetName());
                Item->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
        Data->SetObjectField(TEXT("orphan_nodes"), MakeCountedItemsObject(Count, Items));
    }

    Data->SetBoolField(TEXT("healthy"), bHealthy);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpDiff(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path_a"), TEXT("bp_path_b")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPathA;
    FString BpPathB;
    Params->TryGetStringField(TEXT("bp_path_a"), BpPathA);
    Params->TryGetStringField(TEXT("bp_path_b"), BpPathB);
    UBlueprint* BPA = FSmithUEBpAtomicAPI::LoadBlueprint(BpPathA);
    UBlueprint* BPB = FSmithUEBpAtomicAPI::LoadBlueprint(BpPathB);
    if (!BPA)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPathA));
    }
    if (!BPB)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPathB));
    }

    FString AspectsParam;
    Params->TryGetStringField(TEXT("aspects"), AspectsParam);
    const TSet<FString> Aspects = ParseNameFilter(AspectsParam, {TEXT("parent"), TEXT("components"), TEXT("variables"), TEXT("functions"), TEXT("interfaces"), TEXT("overrides")});

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("a"), BpPathA);
    Data->SetStringField(TEXT("b"), BpPathB);

    if (Aspects.Contains(TEXT("parent")))
    {
        const FString ParentA = BPA->ParentClass ? BPA->ParentClass->GetPathName() : TEXT("None");
        const FString ParentB = BPB->ParentClass ? BPB->ParentClass->GetPathName() : TEXT("None");
        TSharedPtr<FJsonObject> Parent = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Empty;
        Parent->SetArrayField(TEXT("only_in_a"), Empty);
        Parent->SetArrayField(TEXT("only_in_b"), Empty);
        TArray<TSharedPtr<FJsonValue>> Differs;
        if (ParentA != ParentB)
        {
            TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
            Diff->SetStringField(TEXT("a"), ParentA);
            Diff->SetStringField(TEXT("b"), ParentB);
            Differs.Add(MakeShared<FJsonValueObject>(Diff));
        }
        Parent->SetArrayField(TEXT("differs"), Differs);
        Data->SetObjectField(TEXT("parent"), Parent);
    }
    if (Aspects.Contains(TEXT("components")))
    {
        Data->SetObjectField(TEXT("components"), DiffNameMaps(CollectComponentMap(BPA), CollectComponentMap(BPB)));
    }
    if (Aspects.Contains(TEXT("variables")))
    {
        Data->SetObjectField(TEXT("variables"), DiffNameMaps(CollectVariableMap(BPA), CollectVariableMap(BPB)));
    }
    if (Aspects.Contains(TEXT("functions")))
    {
        Data->SetObjectField(TEXT("functions"), DiffNameSets(CollectGraphNameSet(BPA->FunctionGraphs), CollectGraphNameSet(BPB->FunctionGraphs)));
    }
    if (Aspects.Contains(TEXT("interfaces")))
    {
        Data->SetObjectField(TEXT("interfaces"), DiffNameSets(CollectInterfaceSet(BPA), CollectInterfaceSet(BPB)));
    }
    if (Aspects.Contains(TEXT("overrides")))
    {
        Data->SetObjectField(TEXT("overrides"), DiffNameSets(CollectOverrideSet(BPA), CollectOverrideSet(BPB)));
    }

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpTraceValue(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("graph_name"), TEXT("node")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPath;
    FString GraphName;
    FString NodeQuery;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("graph_name"), GraphName);
    Params->TryGetStringField(TEXT("node"), NodeQuery);

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }
    UEdGraph* Graph = FindSmithUEGraphByName(BP, GraphName);
    if (!Graph)
    {
        return MakeErrResp(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }
    UEdGraphNode* StartNode = FindTraceNode(Graph, NodeQuery);
    if (!StartNode)
    {
        return MakeErrResp(FString::Printf(TEXT("Node not found: %s"), *NodeQuery));
    }

    FString Direction = TEXT("upstream");
    Params->TryGetStringField(TEXT("direction"), Direction);
    Direction.TrimStartAndEndInline();
    Direction.ToLowerInline();
    const bool bDownstream = Direction == TEXT("downstream");
    if (!bDownstream && Direction != TEXT("upstream") && !Direction.IsEmpty())
    {
        return MakeErrResp(FString::Printf(TEXT("Unsupported direction: %s"), *Direction));
    }

    int32 MaxDepth = 5;
    double DepthParam = 0.0;
    if (Params->TryGetNumberField(TEXT("max_depth"), DepthParam) && DepthParam > 0.0)
    {
        MaxDepth = FMath::Max(1, static_cast<int32>(DepthParam));
    }

    FString PinFilter;
    Params->TryGetStringField(TEXT("pin"), PinFilter);
    TArray<TSharedPtr<FJsonValue>> Trace;
    bool bTruncated = false;

    for (UEdGraphPin* Pin : StartNode->Pins)
    {
        if (!Pin || IsExecPin(Pin))
        {
            continue;
        }
        if (bDownstream)
        {
            if (Pin->Direction != EGPD_Output)
            {
                continue;
            }
        }
        else if (Pin->Direction != EGPD_Input)
        {
            continue;
        }
        if (!PinFilter.IsEmpty() && !Pin->PinName.ToString().Equals(PinFilter, ESearchCase::IgnoreCase))
        {
            continue;
        }

        TSet<FString> Visited;
        Visited.Add(StartNode->NodeGuid.ToString() + TEXT(".") + Pin->PinName.ToString());
        Trace.Add(MakeShared<FJsonValueObject>(BuildTraceTree(Pin, bDownstream, 0, MaxDepth, Visited, bTruncated)));
    }

    if (!PinFilter.IsEmpty() && Trace.Num() == 0)
    {
        return MakeErrResp(FString::Printf(TEXT("Data pin not found: %s"), *PinFilter));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("graph"), Graph->GetName());
    Data->SetStringField(TEXT("start_node"), StartNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Data->SetStringField(TEXT("direction"), bDownstream ? TEXT("downstream") : TEXT("upstream"));
    if (Trace.Num() == 1)
    {
        Data->SetObjectField(TEXT("trace"), Trace[0]->AsObject());
    }
    else
    {
        Data->SetArrayField(TEXT("trace"), Trace);
    }
    Data->SetBoolField(TEXT("truncated_at_depth"), bTruncated);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpDescribeGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("graph_name")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    FString GraphName;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    // Mode parameter: "full" (default), "compact", "summary", "node_pins", "exec_chain"
    FString Mode = TEXT("full");
    Params->TryGetStringField(TEXT("mode"), Mode);

    // node_ids param for "node_pins" mode
    TSet<FString> NodeIdsFilter;
    if (Mode == TEXT("node_pins"))
    {
        const TArray<TSharedPtr<FJsonValue>>* NodeIdsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArr) && NodeIdsArr)
        {
            for (const TSharedPtr<FJsonValue>& Val : *NodeIdsArr)
            {
                FString NidStr;
                if (Val.IsValid() && Val->TryGetString(NidStr))
                {
                    NodeIdsFilter.Add(NidStr);
                }
            }
        }
    }

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    UEdGraph* Graph = FSmithUEBpAtomicAPI::FindGraph(BP, GraphName);
    if (!Graph)
    {
		return MakeErrResp(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }

    // Build GUID → short ID mapping (N0, N1, ...)
    TMap<FGuid, FString> GuidToShortId;
    int32 NodeIndex = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node)
        {
            GuidToShortId.Add(Node->NodeGuid, FString::Printf(TEXT("N%d"), NodeIndex++));
        }
    }

    // Store server-side N-id → GUID mapping for subsequent commands (e.g. bp_connect_pins).
    // GraphPath is scoped per-graph so N-ids are never shared across graphs.
    {
        const FString GraphPath = BpPath + TEXT("::") + GraphName;
        TMap<int32, FGuid> IndexToGuid;
        IndexToGuid.Reserve(GuidToShortId.Num());
        for (const TPair<FGuid, FString>& Pair : GuidToShortId)
        {
            // NidStr is "N<index>"; strip the leading 'N' to get the integer key.
            const FString& NidStr = Pair.Value;
            if (NidStr.Len() >= 2 && NidStr[0] == TEXT('N'))
            {
                const int32 Idx = FCString::Atoi(*NidStr.Mid(1));
                IndexToGuid.Add(Idx, Pair.Key);
            }
        }
        FSmithUEToolRegistry::Get().NidSession.StoreNids(GraphPath, IndexToGuid);
    }

    // For exec_chain mode: collect only exec-reachable nodes via BFS.
    // IMPORTANT: placed after StoreNids so entry_node N-id resolution works.
    TSet<UEdGraphNode*> ExecChainNodes;
    if (Mode == TEXT("exec_chain"))
    {
        TQueue<UEdGraphNode*> BfsQueue;

        // Optional: start from a specific node.
        FString EntryNodeId;
        if (Params->TryGetStringField(TEXT("entry_node"), EntryNodeId) && !EntryNodeId.IsEmpty())
        {
            FString DummyError;
            const FString GraphPath = BpPath + TEXT("::") + GraphName;
            UEdGraphNode* StartNode = SmithUEBpAtomicAPIHelpers::ResolveNodeId(Graph, GraphPath, EntryNodeId, DummyError);
            if (StartNode)
            {
                BfsQueue.Enqueue(StartNode);
            }
        }
        else
        {
            // Find all entry point nodes (events, function entries, input key nodes).
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }
                const FString NodeClass = Node->GetClass()->GetName();
                if (NodeClass.Contains(TEXT("K2Node_FunctionEntry")) ||
                    NodeClass.Contains(TEXT("K2Node_Event")) ||
                    NodeClass.Contains(TEXT("K2Node_InputKey")) ||
                    NodeClass.Contains(TEXT("K2Node_InputAction")) ||
                    NodeClass.Contains(TEXT("K2Node_EnhancedInputAction")) ||
                    NodeClass.Contains(TEXT("K2Node_CustomEvent")))
                {
                    BfsQueue.Enqueue(Node);
                }
            }
        }

        // BFS following exec output pins only.
        while (!BfsQueue.IsEmpty())
        {
            UEdGraphNode* Current = nullptr;
            BfsQueue.Dequeue(Current);
            if (!Current || ExecChainNodes.Contains(Current))
            {
                continue;
            }
            ExecChainNodes.Add(Current);
            for (UEdGraphPin* Pin : Current->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output)
                {
                    continue;
                }
                if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                {
                    continue;
                }
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (LinkedPin && LinkedPin->GetOwningNode())
                    {
                        BfsQueue.Enqueue(LinkedPin->GetOwningNode());
                    }
                }
            }
        }
    }

    // Helper: resolve a linked pin to "ShortId.PinName"
    auto ResolvePinRef = [&GuidToShortId](UEdGraphPin* LinkedPin) -> FString
    {
        if (!LinkedPin)
        {
            return FString();
        }
        UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
        if (!LinkedNode)
        {
            return FString();
        }
        const FString* ShortId = GuidToShortId.Find(LinkedNode->NodeGuid);
        if (!ShortId)
        {
            return FString();
        }
        return FString::Printf(TEXT("%s.%s"), **ShortId, *LinkedPin->PinName.ToString());
    };

    // Helper: build full pin arrays for a node (shared by "full" and "node_pins" modes)
    auto BuildPinArrays = [&](UEdGraphNode* Node,
                               TArray<TSharedPtr<FJsonValue>>& Inputs,
                               TArray<TSharedPtr<FJsonValue>>& Outputs,
                               bool bCompact)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            // compact: skip hidden pins and pins with no default and no connections
            if (bCompact)
            {
                if (Pin->bHidden)
                {
                    continue;
                }
                if (Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.Num() == 0)
                {
                    continue;
                }
            }

            if (Pin->Direction == EGPD_Input)
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

                // Default value: only include if non-empty
                const FString DefaultVal = Pin->GetDefaultAsString();
                if (!DefaultVal.IsEmpty())
                {
                    PinObj->SetStringField(TEXT("default"), DefaultVal);
                }

                // All connections
                if (Pin->LinkedTo.Num() > 0)
                {
                    TArray<TSharedPtr<FJsonValue>> Conns;
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        FString Ref = ResolvePinRef(LinkedPin);
                        if (!Ref.IsEmpty())
                        {
                            Conns.Add(MakeShared<FJsonValueString>(Ref));
                        }
                    }
                    if (Conns.Num() == 1)
                    {
                        PinObj->SetStringField(TEXT("from"), Conns[0]->AsString());
                    }
                    else if (Conns.Num() > 1)
                    {
                        PinObj->SetArrayField(TEXT("from"), Conns);
                    }
                }

                Inputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
            else // Output
            {
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

                TArray<TSharedPtr<FJsonValue>> Conns;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    FString Ref = ResolvePinRef(LinkedPin);
                    if (!Ref.IsEmpty())
                    {
                        Conns.Add(MakeShared<FJsonValueString>(Ref));
                    }
                }
                if (Conns.Num() == 1)
                {
                    PinObj->SetStringField(TEXT("to"), Conns[0]->AsString());
                }
                else if (Conns.Num() > 1)
                {
                    PinObj->SetArrayField(TEXT("to"), Conns);
                }
                Outputs.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }
    };

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        const FString* ShortId = GuidToShortId.Find(Node->NodeGuid);
        if (!ShortId)
        {
            continue;
        }

        // exec_chain mode: skip nodes not on the exec path.
        if (Mode == TEXT("exec_chain") && !ExecChainNodes.Contains(Node))
        {
            continue;
        }

        // node_pins mode: skip nodes not in the requested list
        if (Mode == TEXT("node_pins") && !NodeIdsFilter.Contains(*ShortId))
        {
            continue;
        }

        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("id"), *ShortId);
        NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

        // summary mode: no pins
        if (Mode != TEXT("summary"))
        {
            const bool bCompact = (Mode == TEXT("compact") || Mode == TEXT("exec_chain"));
            TArray<TSharedPtr<FJsonValue>> Inputs;
            TArray<TSharedPtr<FJsonValue>> Outputs;
            BuildPinArrays(Node, Inputs, Outputs, bCompact);

            if (Inputs.Num() > 0)
            {
                NodeObj->SetArrayField(TEXT("in"), Inputs);
            }
            if (Outputs.Num() > 0)
            {
                NodeObj->SetArrayField(TEXT("out"), Outputs);
            }
        }

        Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetStringField(TEXT("graph_name"), GraphName);
    Data->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
    Data->SetArrayField(TEXT("nodes"), Nodes);

    // Only "full" mode includes id_to_guid (largest token cost)
    if (Mode == TEXT("full"))
    {
        TSharedPtr<FJsonObject> IdMap = MakeShared<FJsonObject>();
        for (const auto& Pair : GuidToShortId)
        {
            IdMap->SetStringField(Pair.Value, Pair.Key.ToString());
        }
        Data->SetObjectField(TEXT("id_to_guid"), IdMap);
    }

    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpCompileCode(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path"), TEXT("code")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString BpPath;
    FString Code;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);
    Params->TryGetStringField(TEXT("code"), Code);

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
		return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    FSmithUECompileResult Result = FSmithUEBpCompiler::CompileFunction(BP, Code);

    TArray<TSharedPtr<FJsonValue>> Errors;
    for (const FString& Item : Result.Errors)
    {
        Errors.Add(MakeShared<FJsonValueString>(Item));
    }

    TArray<TSharedPtr<FJsonValue>> CreatedNodeIds;
    for (const FString& NodeId : Result.CreatedNodeIds)
    {
        CreatedNodeIds.Add(MakeShared<FJsonValueString>(NodeId));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("success"), Result.bSuccess);
    Data->SetStringField(TEXT("graph_name"), Result.GraphName);
    Data->SetArrayField(TEXT("errors"), Errors);
    Data->SetArrayField(TEXT("created_node_ids"), CreatedNodeIds);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpBatchOp(const TSharedPtr<FJsonObject>& Params)
{
    // --- Op alias table (resolved once at first call) ---
    static const TMap<FString, FString> OpAliases = []()
    {
        TMap<FString, FString> T;
        T.Add(TEXT("connect"),       TEXT("bp_connect_pins"));
        T.Add(TEXT("link"),          TEXT("bp_connect_pins"));
        T.Add(TEXT("disconnect"),    TEXT("bp_disconnect_pins"));
        T.Add(TEXT("unlink"),        TEXT("bp_disconnect_pins"));
        T.Add(TEXT("set_default"),   TEXT("bp_set_pin_default"));
        T.Add(TEXT("set_value"),     TEXT("bp_set_pin_default"));
        T.Add(TEXT("create"),        TEXT("bp_create_node"));
        T.Add(TEXT("add_node"),      TEXT("bp_create_node"));
        T.Add(TEXT("delete"),        TEXT("bp_delete_node"));
        T.Add(TEXT("remove_node"),   TEXT("bp_delete_node"));
        return T;
    }();

    // --- Validate 'operations' is present ---
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("operations")}, Error))
    {
        return MakeErrResp(Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
    {
        return MakeErrResp(TEXT("Invalid or missing param: 'operations'"));
    }

    // --- Batch size limit ---
    if (Operations->Num() > 50)
    {
        return MakeErrResp(FString::Printf(
            TEXT("Batch size %d exceeds maximum of 50 operations"), Operations->Num()));
    }

    // --- Shared batch-level params (bp_path, graph_name) ---
    FString BatchBpPath;
    Params->TryGetStringField(TEXT("bp_path"), BatchBpPath);
    FString BatchGraphName;
    Params->TryGetStringField(TEXT("graph_name"), BatchGraphName);

    bool bAtomic = false;
    Params->TryGetBoolField(TEXT("atomic"), bAtomic);
    if (bAtomic)
    {
        auto MakeAtomicDryRunError = [](const TArray<FString>& Errors) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
            Response->SetStringField(TEXT("status"), TEXT("error"));
            Response->SetStringField(TEXT("error"), TEXT("atomic dry-run failed"));
            TArray<TSharedPtr<FJsonValue>> ErrorValues;
            for (const FString& Item : Errors)
            {
                ErrorValues.Add(MakeShared<FJsonValueString>(Item));
            }
            Response->SetArrayField(TEXT("errors"), ErrorValues);
            return Response;
        };

        auto BuildOpParams = [&BatchBpPath, &BatchGraphName](const TSharedPtr<FJsonObject>& OpObj) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> OpParams = MakeShared<FJsonObject>();
            if (!BatchBpPath.IsEmpty())
            {
                OpParams->SetStringField(TEXT("bp_path"), BatchBpPath);
            }
            if (!BatchGraphName.IsEmpty())
            {
                OpParams->SetStringField(TEXT("graph_name"), BatchGraphName);
            }
            for (const auto& KV : OpObj->Values)
            {
                if (!KV.Key.Equals(TEXT("op"), ESearchCase::IgnoreCase) &&
                    !KV.Key.Equals(TEXT("atomic"), ESearchCase::IgnoreCase))
                {
                    OpParams->Values.Add(KV.Key, KV.Value);
                }
            }
            const TSharedPtr<FJsonObject>* OpParamsPtr = nullptr;
            if (OpObj->TryGetObjectField(TEXT("params"), OpParamsPtr) && OpParamsPtr && OpParamsPtr->IsValid())
            {
                for (const auto& KV : (*OpParamsPtr)->Values)
                {
                    OpParams->Values.Add(KV.Key, KV.Value);
                }
            }
            return OpParams;
        };

        auto FindNodeById = [](UEdGraph* Graph, const FString& GraphPath, const FString& NodeId, FString& Error) -> UEdGraphNode*
        {
            if (!Graph)
            {
                Error = TEXT("Graph not found");
                return nullptr;
            }

            bool bIsStale = false;
            const FGuid NodeGuid = FSmithUEToolRegistry::Get().NidSession.ResolveNid(GraphPath, NodeId, bIsStale);
            if (bIsStale)
            {
                Error = FString::Printf(TEXT("N-id session is stale for graph '%s'"), *GraphPath);
                return nullptr;
            }
            if (!NodeGuid.IsValid())
            {
                Error = FString::Printf(TEXT("Node id '%s' not found in N-id session for graph '%s'"), *NodeId, *GraphPath);
                return nullptr;
            }

            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && Node->NodeGuid == NodeGuid)
                {
                    return Node;
                }
            }

            Error = FString::Printf(TEXT("Node id '%s' resolved to missing node in graph '%s'"), *NodeId, *GraphPath);
            return nullptr;
        };

        auto ValidateNodeAndPin = [&FindNodeById](UEdGraph* Graph, const FString& GraphPath, const FString& NodeField, const FString& NodeId, const FString& PinField, const FString& PinName, FString& Error) -> bool
        {
            UEdGraphNode* Node = FindNodeById(Graph, GraphPath, NodeId, Error);
            if (!Node)
            {
                Error = FString::Printf(TEXT("%s: %s"), *NodeField, *Error);
                return false;
            }

            if (!PinName.IsEmpty())
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->PinName.ToString() == PinName)
                    {
                        return true;
                    }
                }
                Error = FString::Printf(TEXT("%s '%s' not found on %s '%s'"), *PinField, *PinName, *NodeField, *NodeId);
                return false;
            }

            return true;
        };

        auto ExtractDispatchError = [](const TSharedPtr<FJsonObject>& DispatchResult, const FString& OpName) -> FString
        {
            FString OpError;
            if (DispatchResult.IsValid())
            {
                DispatchResult->TryGetStringField(TEXT("error"), OpError);
            }
            return OpError.IsEmpty() ? FString::Printf(TEXT("Op '%s' failed"), *OpName) : OpError;
        };

        struct FAtomicBatchOp
        {
            int32 Index = INDEX_NONE;
            FString OpName;
            TSharedPtr<FJsonObject> Params;
        };

        TArray<FAtomicBatchOp> AtomicOps;
        TArray<FAtomicBatchOp> CompileOps;
        TArray<FString> DryRunErrors;

        for (int32 OpIndex = 0; OpIndex < Operations->Num(); ++OpIndex)
        {
            const TSharedPtr<FJsonValue>& OpValue = (*Operations)[OpIndex];
            TSharedPtr<FJsonObject> OpObj = OpValue.IsValid() ? OpValue->AsObject() : nullptr;
            if (!OpObj.IsValid())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d]: Each operation must be an object"), OpIndex));
                continue;
            }

            FString OpName;
            if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d]: Operation missing 'op'"), OpIndex));
                continue;
            }
            if (const FString* Resolved = OpAliases.Find(OpName))
            {
                OpName = *Resolved;
            }

            TSharedPtr<FJsonObject> OpParams = BuildOpParams(OpObj);
            FAtomicBatchOp BatchOp{OpIndex, OpName, OpParams};
            if (OpName.Equals(TEXT("bp_compile"), ESearchCase::IgnoreCase))
            {
                CompileOps.Add(BatchOp);
                continue;
            }

            AtomicOps.Add(BatchOp);

            FString BpPath;
            FString GraphName;
            const bool bNeedsGraph = OpParams->HasField(TEXT("node_id")) ||
                                     OpParams->HasField(TEXT("source_node_id")) ||
                                     OpParams->HasField(TEXT("target_node_id"));
            if (!bNeedsGraph)
            {
                continue;
            }

            if (!OpParams->TryGetStringField(TEXT("bp_path"), BpPath) || BpPath.IsEmpty())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Missing required param: 'bp_path'"), OpIndex, *OpName));
                continue;
            }
            if (!OpParams->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Missing required param: 'graph_name'"), OpIndex, *OpName));
                continue;
            }

            UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
            if (!BP)
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Failed to load blueprint: %s"), OpIndex, *OpName, *BpPath));
                continue;
            }
            UEdGraph* Graph = FSmithUEBpAtomicAPI::FindGraph(BP, GraphName);
            if (!Graph)
            {
                DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': Graph not found: %s"), OpIndex, *OpName, *GraphName));
                continue;
            }

            const FString GraphPath = BpPath + TEXT("::") + GraphName;
            FString ValidationError;
            FString NodeId;
            if (OpParams->TryGetStringField(TEXT("node_id"), NodeId) && !NodeId.IsEmpty())
            {
                FString PinName;
                OpParams->TryGetStringField(TEXT("pin_name"), PinName);
                if (!ValidateNodeAndPin(Graph, GraphPath, TEXT("node_id"), NodeId, TEXT("pin_name"), PinName, ValidationError))
                {
                    DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': %s"), OpIndex, *OpName, *ValidationError));
                }
            }

            FString SourceNodeId;
            if (OpParams->TryGetStringField(TEXT("source_node_id"), SourceNodeId) && !SourceNodeId.IsEmpty())
            {
                FString SourcePin;
                OpParams->TryGetStringField(TEXT("source_pin"), SourcePin);
                if (!ValidateNodeAndPin(Graph, GraphPath, TEXT("source_node_id"), SourceNodeId, TEXT("source_pin"), SourcePin, ValidationError))
                {
                    DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': %s"), OpIndex, *OpName, *ValidationError));
                }
            }

            FString TargetNodeId;
            if (OpParams->TryGetStringField(TEXT("target_node_id"), TargetNodeId) && !TargetNodeId.IsEmpty())
            {
                FString TargetPin;
                OpParams->TryGetStringField(TEXT("target_pin"), TargetPin);
                if (!ValidateNodeAndPin(Graph, GraphPath, TEXT("target_node_id"), TargetNodeId, TEXT("target_pin"), TargetPin, ValidationError))
                {
                    DryRunErrors.Add(FString::Printf(TEXT("op[%d] '%s': %s"), OpIndex, *OpName, *ValidationError));
                }
            }
        }

        if (DryRunErrors.Num() > 0)
        {
            return MakeAtomicDryRunError(DryRunErrors);
        }

        TSet<FString> StaleGraphPaths;
        {
            FScopedTransaction AtomicTxn(FText::FromString(TEXT("SmithUE Atomic Batch")));
            for (const FAtomicBatchOp& Op : AtomicOps)
            {
                TSharedPtr<FJsonObject> DispatchResult = FSmithUEToolRegistry::Get().DispatchCommand(Op.OpName, Op.Params);
                if (!DispatchResult.IsValid())
                {
                    DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *Op.OpName));
                }

                FString OpStatus;
                DispatchResult->TryGetStringField(TEXT("status"), OpStatus);
                if (OpStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase))
                {
                    AtomicTxn.Cancel();
                    return MakeErrResp(FString::Printf(TEXT("op[%d] '%s' failed: %s"), Op.Index, *Op.OpName, *ExtractDispatchError(DispatchResult, Op.OpName)));
                }

                if (Op.OpName.Equals(TEXT("bp_create_node"), ESearchCase::IgnoreCase) ||
                    Op.OpName.Equals(TEXT("bp_delete_node"), ESearchCase::IgnoreCase))
                {
                    FString OpBpPath;
                    FString OpGraphName;
                    if (Op.Params->TryGetStringField(TEXT("bp_path"), OpBpPath) &&
                        Op.Params->TryGetStringField(TEXT("graph_name"), OpGraphName) &&
                        !OpBpPath.IsEmpty() && !OpGraphName.IsEmpty())
                    {
                        StaleGraphPaths.Add(OpBpPath + TEXT("::") + OpGraphName);
                    }
                }
            }
        }

        for (const FString& GraphPath : StaleGraphPaths)
        {
            FSmithUEToolRegistry::Get().NidSession.MarkStale(GraphPath);
        }

        for (const FAtomicBatchOp& Op : CompileOps)
        {
            TSharedPtr<FJsonObject> DispatchResult = FSmithUEToolRegistry::Get().DispatchCommand(Op.OpName, Op.Params);
            if (!DispatchResult.IsValid())
            {
                DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *Op.OpName));
            }

            FString OpStatus;
            DispatchResult->TryGetStringField(TEXT("status"), OpStatus);
            if (OpStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase))
            {
                return MakeErrResp(FString::Printf(TEXT("op[%d] '%s' failed: %s"), Op.Index, *Op.OpName, *ExtractDispatchError(DispatchResult, Op.OpName)));
            }
        }

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("atomic"), true);
        Data->SetNumberField(TEXT("operations_executed"), Operations->Num());
        return WrapSuccess(Data);
    }

    // --- Single transaction wrapping the entire batch ---
    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Blueprint Batch Op")));

    TArray<TSharedPtr<FJsonValue>> Results;
    TSet<FString> StaleGraphPaths;
    for (int32 OpIndex = 0; OpIndex < Operations->Num(); ++OpIndex)
    {
        const TSharedPtr<FJsonValue>& OpValue = (*Operations)[OpIndex];

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("index"), OpIndex);

        TSharedPtr<FJsonObject> OpObj = OpValue.IsValid() ? OpValue->AsObject() : nullptr;
        if (!OpObj.IsValid())
        {
            Item->SetStringField(TEXT("status"), TEXT("error"));
            Item->SetStringField(TEXT("error"), TEXT("Each operation must be an object"));
            Results.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }

        FString OpName;
        if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
        {
            Item->SetStringField(TEXT("status"), TEXT("error"));
            Item->SetStringField(TEXT("error"), TEXT("Operation missing 'op'"));
            Results.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }

        // --- Resolve alias ---
        if (const FString* Resolved = OpAliases.Find(OpName))
        {
            OpName = *Resolved;
        }

        // --- Build merged params: batch-level defaults, op-level overrides ---
        TSharedPtr<FJsonObject> OpParams = MakeShared<FJsonObject>();

        // Inject batch-level shared params first (lower priority)
        if (!BatchBpPath.IsEmpty())
        {
            OpParams->SetStringField(TEXT("bp_path"), BatchBpPath);
        }
        if (!BatchGraphName.IsEmpty())
        {
            OpParams->SetStringField(TEXT("graph_name"), BatchGraphName);
        }

        // Overlay op-level params (higher priority — overwrite batch-level)
        // First: copy direct op-level fields (flat format: {op:"set_default", node_id:"N5", ...})
        for (const auto& KV : OpObj->Values)
        {
            if (!KV.Key.Equals(TEXT("op"), ESearchCase::IgnoreCase))
            {
                OpParams->Values.Add(KV.Key, KV.Value);
            }
        }
        // Also support nested "params" sub-object (legacy format), overrides flat fields
        const TSharedPtr<FJsonObject>* OpParamsPtr = nullptr;
        if (OpObj->TryGetObjectField(TEXT("params"), OpParamsPtr) && OpParamsPtr && OpParamsPtr->IsValid())
        {
            for (const auto& KV : (*OpParamsPtr)->Values)
            {
                OpParams->Values.Add(KV.Key, KV.Value);
            }
        }

        // --- Dispatch ---
        TSharedPtr<FJsonObject> DispatchResult = FSmithUEToolRegistry::Get().DispatchCommand(OpName, OpParams);
        if (!DispatchResult.IsValid())
        {
            DispatchResult = MakeErrResp(FString::Printf(TEXT("Unknown command: %s"), *OpName));
        }

        // --- Build per-op result entry ---
        FString OpStatus;
        DispatchResult->TryGetStringField(TEXT("status"), OpStatus);
        const bool bOpFailed = OpStatus.Equals(TEXT("error"), ESearchCase::IgnoreCase);
        const bool bMutationOp = OpName.Equals(TEXT("bp_create_node"), ESearchCase::IgnoreCase) ||
                                 OpName.Equals(TEXT("bp_delete_node"), ESearchCase::IgnoreCase);

        Item->SetStringField(TEXT("status"), bOpFailed ? TEXT("error") : TEXT("success"));
        if (bOpFailed)
        {
            FString OpError;
            DispatchResult->TryGetStringField(TEXT("error"), OpError);
            if (OpError.IsEmpty())
            {
                // Fallback: serialize the full result as error detail
                OpError = FString::Printf(TEXT("Op '%s' failed"), *OpName);
            }
            Item->SetStringField(TEXT("error"), OpError);
        }
        else if (bMutationOp)
        {
            FString OpBpPath;
            FString OpGraphName;
            if (OpParams->TryGetStringField(TEXT("bp_path"), OpBpPath) &&
                OpParams->TryGetStringField(TEXT("graph_name"), OpGraphName) &&
                !OpBpPath.IsEmpty() && !OpGraphName.IsEmpty())
            {
                const FString GraphPath = OpBpPath + TEXT("::") + OpGraphName;
                StaleGraphPaths.Add(GraphPath);
                Item->SetBoolField(TEXT("nid_stale"), true);
            }
        }

        Results.Add(MakeShared<FJsonValueObject>(Item));
        // Partial commit: always continue to next op regardless of failure
    }

    for (const FString& GraphPath : StaleGraphPaths)
    {
        FSmithUEToolRegistry::Get().NidSession.MarkStale(GraphPath);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("results"), Results);
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpValidateCode(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("code")}, Error))
    {
		return MakeErrResp(Error);
    }

    FString Code;
    if (!Params->TryGetStringField(TEXT("code"), Code))
    {
		return MakeErrResp(TEXT("Missing required param: 'code'"));
    }

    FString SyntaxError;
    const bool bValid = FSmithUEBpCompiler::ValidateSyntax(Code, SyntaxError);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("valid"), bValid);
    if (!bValid)
    {
        Data->SetStringField(TEXT("error"), SyntaxError);
    }
    return WrapSuccess(Data);
}

TSharedPtr<FJsonObject> FSmithUEBlueprintCommands::HandleBpSearch(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("bp_path")}, Error))
    {
        return MakeErrResp(Error);
    }

    FString BpPath;
    Params->TryGetStringField(TEXT("bp_path"), BpPath);

    UBlueprint* BP = FSmithUEBpAtomicAPI::LoadBlueprint(BpPath);
    if (!BP)
    {
        return MakeErrResp(FString::Printf(TEXT("Failed to load blueprint: %s"), *BpPath));
    }

    // --- Filter params ---
    FString NameFilter;
    Params->TryGetStringField(TEXT("name"), NameFilter);
    const FString NameFilterLower = NameFilter.ToLower();

    FString TypeFilter;
    Params->TryGetStringField(TEXT("type"), TypeFilter);

    bool bVerbose = false;
    Params->TryGetBoolField(TEXT("verbose"), bVerbose);

    int32 Limit = 100;
    {
        int32 LimitParam = 0;
        if (Params->TryGetNumberField(TEXT("limit"), LimitParam) && LimitParam > 0)
        {
            Limit = LimitParam;
        }
    }

    // --- Collect all graphs ---
    TArray<UEdGraph*> AllGraphs;
    BP->GetAllGraphs(AllGraphs);

    // --- Search nodes ---
    TArray<TSharedPtr<FJsonValue>> ResultNodes;
    int32 MatchCount = 0;

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        const FString GraphName = Graph->GetName();
        const FString GraphPath = BpPath + TEXT("::") + GraphName;

        // --- Build / refresh GuidToShortId for this graph (mirrors bp_describe_graph) ---
        // Always rebuild: ensures the session is fresh and consistent with current node order.
        TMap<FGuid, FString> GuidToShortId;
        {
            int32 NodeIndex = 0;
            for (UEdGraphNode* N : Graph->Nodes)
            {
                if (N)
                {
                    GuidToShortId.Add(N->NodeGuid, FString::Printf(TEXT("N%d"), NodeIndex++));
                }
            }

            // Store into NidSession so subsequent commands (bp_connect_pins etc.) can resolve.
            TMap<int32, FGuid> IndexToGuid;
            IndexToGuid.Reserve(GuidToShortId.Num());
            for (const TPair<FGuid, FString>& Pair : GuidToShortId)
            {
                const FString& NidStr = Pair.Value;
                if (NidStr.Len() >= 2 && NidStr[0] == TEXT('N'))
                {
                    const int32 Idx = FCString::Atoi(*NidStr.Mid(1));
                    IndexToGuid.Add(Idx, Pair.Key);
                }
            }
            FSmithUEToolRegistry::Get().NidSession.StoreNids(GraphPath, IndexToGuid);
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            if (MatchCount >= Limit)
            {
                break;
            }

            // --- Type filter (exact class name match) ---
            if (!TypeFilter.IsEmpty())
            {
                if (!Node->GetClass()->GetName().Equals(TypeFilter, ESearchCase::IgnoreCase))
                {
                    continue;
                }
            }

            // --- Name filter (case-insensitive substring of full title) ---
            if (!NameFilterLower.IsEmpty())
            {
                const FString TitleLower = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().ToLower();
                if (!TitleLower.Contains(NameFilterLower))
                {
                    continue;
                }
            }

            // Resolve N-id from session map (guaranteed present after StoreNids above).
            const FString* ShortId = GuidToShortId.Find(Node->NodeGuid);
            const FString NidStr = ShortId ? *ShortId : Node->NodeGuid.ToString();

            // --- Build node object ---
            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("nid"),      NidStr);
            NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
            NodeObj->SetStringField(TEXT("title"),    Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            NodeObj->SetStringField(TEXT("type"),     Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("graph"),    GraphName);
            NodeObj->SetObjectField(TEXT("position"), [&]()
            {
                TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
                Pos->SetNumberField(TEXT("x"), Node->NodePosX);
                Pos->SetNumberField(TEXT("y"), Node->NodePosY);
                return Pos;
            }());

            // --- Verbose: include pins ---
            if (bVerbose)
            {
                TArray<TSharedPtr<FJsonValue>> PinsIn;
                TArray<TSharedPtr<FJsonValue>> PinsOut;

                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin)
                    {
                        continue;
                    }
                    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                    PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
                    PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

                    TArray<TSharedPtr<FJsonValue>> ConnectedTo;
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin) continue;
                        UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                        if (!LinkedNode) continue;
                        const FString* Nid = GuidToShortId.Find(LinkedNode->NodeGuid);
                        if (Nid) ConnectedTo.Add(MakeShared<FJsonValueString>(*Nid));
                    }
                    PinObj->SetArrayField(TEXT("connected_to"), ConnectedTo);

                    if (Pin->Direction == EGPD_Input)
                    {
                        PinsIn.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                    else
                    {
                        PinsOut.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                }

                if (PinsIn.Num() > 0)
                {
                    NodeObj->SetArrayField(TEXT("in"), PinsIn);
                }
                if (PinsOut.Num() > 0)
                {
                    NodeObj->SetArrayField(TEXT("out"), PinsOut);
                }
            }

            ResultNodes.Add(MakeShared<FJsonValueObject>(NodeObj));
            ++MatchCount;
        }

        if (MatchCount >= Limit)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bp_path"), BpPath);
    Data->SetArrayField(TEXT("nodes"), ResultNodes);
    Data->SetNumberField(TEXT("count"), ResultNodes.Num());
    return WrapSuccess(Data);
}

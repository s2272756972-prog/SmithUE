// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEAnimCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/AnimBlueprintFactory.h"

namespace SmithUEAnim
{
    UAnimMontage* LoadMontage(const FString& Path, FString& OutError)
    {
        UObject* Loaded = StaticLoadObject(UAnimMontage::StaticClass(), nullptr, *Path);
        UAnimMontage* Montage = Cast<UAnimMontage>(Loaded);
        if (!Montage)
        {
            OutError = FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *Path);
        }
        return Montage;
    }

    USkeleton* LoadSkeleton(const FString& Path, FString& OutError)
    {
        UObject* Loaded = StaticLoadObject(USkeleton::StaticClass(), nullptr, *Path);
        USkeleton* Skeleton = Cast<USkeleton>(Loaded);
        if (!Skeleton)
        {
            OutError = FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *Path);
        }
        return Skeleton;
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEAnimCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_create_montage"),
            TEXT("Animation"),
            TEXT("Create an AnimMontage asset for a skeleton"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Montage asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path"), true),
                FSmithUEToolParam(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path"), true)
            }),
        &HandleAnimCreateMontage);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_read_montage"),
            TEXT("Animation"),
            TEXT("Read montage sections, notifies, and slots"),
            {
                FSmithUEToolParam(TEXT("montage_path"), TEXT("string"), TEXT("AnimMontage asset path"), true)
            }),
        &HandleAnimReadMontage);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_add_section"),
            TEXT("Animation"),
            TEXT("Add a section to an AnimMontage"),
            {
                FSmithUEToolParam(TEXT("montage_path"), TEXT("string"), TEXT("AnimMontage asset path"), true),
                FSmithUEToolParam(TEXT("section_name"), TEXT("string"), TEXT("Section name"), true),
                FSmithUEToolParam(TEXT("start_time"), TEXT("float"), TEXT("Section start time in seconds"), true)
            }),
        &HandleAnimAddSection);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_link_sections"),
            TEXT("Animation"),
            TEXT("Link two montage sections for sequential playback"),
            {
                FSmithUEToolParam(TEXT("montage_path"), TEXT("string"), TEXT("AnimMontage asset path"), true),
                FSmithUEToolParam(TEXT("from_section"), TEXT("string"), TEXT("Source section name"), true),
                FSmithUEToolParam(TEXT("to_section"), TEXT("string"), TEXT("Target section name"), true)
            }),
        &HandleAnimLinkSections);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_add_notify"),
            TEXT("Animation"),
            TEXT("Add an anim notify at a time"),
            {
                FSmithUEToolParam(TEXT("montage_path"), TEXT("string"), TEXT("AnimMontage asset path"), true),
                FSmithUEToolParam(TEXT("notify_name"), TEXT("string"), TEXT("Notify display name"), true),
                FSmithUEToolParam(TEXT("time"), TEXT("float"), TEXT("Time in seconds"), true),
                FSmithUEToolParam(TEXT("notify_class"), TEXT("string"), TEXT("Optional AnimNotify class path"))
            }),
        &HandleAnimAddNotify);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_create_blueprint"),
            TEXT("Animation"),
            TEXT("Create an AnimBlueprint asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("AnimBP asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path"), true),
                FSmithUEToolParam(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path"), true),
                FSmithUEToolParam(TEXT("parent_class"), TEXT("string"), TEXT("Optional parent AnimInstance class path"))
            }),
        &HandleAnimCreateBlueprint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("anim_read_blueprint"),
            TEXT("Animation"),
            TEXT("Read AnimBP info (state machines, variables, skeleton)"),
            {
                FSmithUEToolParam(TEXT("anim_bp_path"), TEXT("string"), TEXT("AnimBlueprint asset path"), true)
            }),
        &HandleAnimReadBlueprint);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimCreateMontage(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, Path, SkeletonPath;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);

    FString Error;
    USkeleton* Skeleton = SmithUEAnim::LoadSkeleton(SkeletonPath, Error);
    if (!Skeleton) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
    Factory->TargetSkeleton = Skeleton;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UAnimMontage::StaticClass(), Factory);
    if (!NewAsset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create AnimMontage"));
    }

    UEditorAssetLibrary::SaveLoadedAsset(NewAsset);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    UE_LOG(LogSmithUE, Log, TEXT("anim_create_montage: created %s"), *NewAsset->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimReadMontage(const TSharedPtr<FJsonObject>& Params)
{
    FString MontagePath;
    Params->TryGetStringField(TEXT("montage_path"), MontagePath);

    FString Error;
    UAnimMontage* Montage = SmithUEAnim::LoadMontage(MontagePath, Error);
    if (!Montage) return FSmithUECommonUtils::CreateErrorResponse(Error);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("montage_path"), Montage->GetPathName());
    Data->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
    Data->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
    Data->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());

    // Sections
    TArray<TSharedPtr<FJsonValue>> SectionsArray;
    for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
    {
        const FCompositeSection& Section = Montage->CompositeSections[i];
        TSharedPtr<FJsonObject> SecObj = MakeShared<FJsonObject>();
        SecObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
        SecObj->SetNumberField(TEXT("start_time"), Section.GetTime());
        if (Section.NextSectionName != NAME_None)
        {
            SecObj->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
        }
        SectionsArray.Add(MakeShared<FJsonValueObject>(SecObj));
    }
    Data->SetArrayField(TEXT("sections"), SectionsArray);

    // Slot tracks
    TArray<TSharedPtr<FJsonValue>> SlotsArray;
    for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
    {
        TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
        SlotObj->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
        SlotsArray.Add(MakeShared<FJsonValueObject>(SlotObj));
    }
    Data->SetArrayField(TEXT("slots"), SlotsArray);

    // Notifies
    TArray<TSharedPtr<FJsonValue>> NotifiesArray;
    for (const FAnimNotifyEvent& Notify : Montage->Notifies)
    {
        TSharedPtr<FJsonObject> NotifyObj = MakeShared<FJsonObject>();
        NotifyObj->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
        NotifyObj->SetNumberField(TEXT("time"), Notify.GetTime());
        if (Notify.Notify)
        {
            NotifyObj->SetStringField(TEXT("class"), Notify.Notify->GetClass()->GetName());
        }
        NotifiesArray.Add(MakeShared<FJsonValueObject>(NotifyObj));
    }
    Data->SetArrayField(TEXT("notifies"), NotifiesArray);

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimAddSection(const TSharedPtr<FJsonObject>& Params)
{
    FString MontagePath, SectionName;
    double StartTime = 0;
    Params->TryGetStringField(TEXT("montage_path"), MontagePath);
    Params->TryGetStringField(TEXT("section_name"), SectionName);
    Params->TryGetNumberField(TEXT("start_time"), StartTime);

    FString Error;
    UAnimMontage* Montage = SmithUEAnim::LoadMontage(MontagePath, Error);
    if (!Montage) return FSmithUECommonUtils::CreateErrorResponse(Error);

    // Check if section already exists
    int32 ExistingIdx = Montage->GetSectionIndex(FName(*SectionName));
    if (ExistingIdx != INDEX_NONE)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Section '%s' already exists at index %d"), *SectionName, ExistingIdx));
    }

    // Add section
    int32 NewIdx = Montage->AddAnimCompositeSection(FName(*SectionName), (float)StartTime);

    Montage->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(Montage);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("section_name"), SectionName);
    Data->SetNumberField(TEXT("start_time"), StartTime);
    Data->SetNumberField(TEXT("section_index"), NewIdx);
    Data->SetNumberField(TEXT("total_sections"), Montage->CompositeSections.Num());
    UE_LOG(LogSmithUE, Log, TEXT("anim_add_section: added '%s' at %.2f"), *SectionName, StartTime);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimLinkSections(const TSharedPtr<FJsonObject>& Params)
{
    FString MontagePath, FromSection, ToSection;
    Params->TryGetStringField(TEXT("montage_path"), MontagePath);
    Params->TryGetStringField(TEXT("from_section"), FromSection);
    Params->TryGetStringField(TEXT("to_section"), ToSection);

    FString Error;
    UAnimMontage* Montage = SmithUEAnim::LoadMontage(MontagePath, Error);
    if (!Montage) return FSmithUECommonUtils::CreateErrorResponse(Error);

    int32 FromIdx = Montage->GetSectionIndex(FName(*FromSection));
    int32 ToIdx = Montage->GetSectionIndex(FName(*ToSection));

    if (FromIdx == INDEX_NONE)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("from_section '%s' not found"), *FromSection));
    }
    if (ToIdx == INDEX_NONE)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("to_section '%s' not found"), *ToSection));
    }

    Montage->CompositeSections[FromIdx].NextSectionName = FName(*ToSection);
    Montage->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(Montage);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("from_section"), FromSection);
    Data->SetStringField(TEXT("to_section"), ToSection);
    UE_LOG(LogSmithUE, Log, TEXT("anim_link_sections: %s → %s"), *FromSection, *ToSection);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimAddNotify(const TSharedPtr<FJsonObject>& Params)
{
    FString MontagePath, NotifyName, NotifyClass;
    double Time = 0;
    Params->TryGetStringField(TEXT("montage_path"), MontagePath);
    Params->TryGetStringField(TEXT("notify_name"), NotifyName);
    Params->TryGetNumberField(TEXT("time"), Time);
    Params->TryGetStringField(TEXT("notify_class"), NotifyClass);

    FString Error;
    UAnimMontage* Montage = SmithUEAnim::LoadMontage(MontagePath, Error);
    if (!Montage) return FSmithUECommonUtils::CreateErrorResponse(Error);

    FAnimNotifyEvent NewNotify;
    NewNotify.NotifyName = FName(*NotifyName);

    // Set time
    NewNotify.SetTime((float)Time);

    // If notify class specified, create instance
    if (!NotifyClass.IsEmpty())
    {
        UClass* NClass = LoadClass<UAnimNotify>(nullptr, *NotifyClass);
        if (NClass)
        {
            NewNotify.Notify = NewObject<UAnimNotify>(Montage, NClass);
        }
    }

    // Add to first track slot
    NewNotify.TriggerTimeOffset = 0.0f;
    NewNotify.TrackIndex = 0;
    Montage->Notifies.Add(NewNotify);

    Montage->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(Montage);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("notify_name"), NotifyName);
    Data->SetNumberField(TEXT("time"), Time);
    Data->SetNumberField(TEXT("total_notifies"), Montage->Notifies.Num());
    UE_LOG(LogSmithUE, Log, TEXT("anim_add_notify: added '%s' at %.2f"), *NotifyName, Time);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, Path, SkeletonPath, ParentClassPath;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
    Params->TryGetStringField(TEXT("parent_class"), ParentClassPath);

    FString Error;
    USkeleton* Skeleton = SmithUEAnim::LoadSkeleton(SkeletonPath, Error);
    if (!Skeleton) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
    Factory->TargetSkeleton = Skeleton;

    if (!ParentClassPath.IsEmpty())
    {
        UClass* ParentClass = LoadClass<UAnimInstance>(nullptr, *ParentClassPath);
        if (ParentClass)
        {
            Factory->ParentClass = ParentClass;
        }
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UAnimBlueprint::StaticClass(), Factory);
    if (!NewAsset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create AnimBlueprint"));
    }

    UEditorAssetLibrary::SaveLoadedAsset(NewAsset);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    UE_LOG(LogSmithUE, Log, TEXT("anim_create_blueprint: created %s"), *NewAsset->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEAnimCommands::HandleAnimReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString AnimBPPath;
    Params->TryGetStringField(TEXT("anim_bp_path"), AnimBPPath);

    UObject* Loaded = StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *AnimBPPath);
    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Loaded);
    if (!AnimBP)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load AnimBlueprint at '%s'"), *AnimBPPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("anim_bp_path"), AnimBP->GetPathName());

    if (AnimBP->TargetSkeleton)
    {
        Data->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton->GetPathName());
    }

    Data->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ? AnimBP->ParentClass->GetPathName() : TEXT("None"));

    // Variables
    TArray<TSharedPtr<FJsonValue>> VarsArray;
    for (FBPVariableDescription& Var : AnimBP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
        VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    Data->SetArrayField(TEXT("variables"), VarsArray);
    Data->SetNumberField(TEXT("variable_count"), VarsArray.Num());

    // Generated class info
    UAnimBlueprintGeneratedClass* GenClass = Cast<UAnimBlueprintGeneratedClass>(AnimBP->GeneratedClass);
    if (GenClass)
    {
        Data->SetNumberField(TEXT("anim_node_count"), GenClass->AnimNodeProperties.Num());
    }

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEEditorCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "SLevelViewport.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Misc/ScopedSlowTask.h"
#include "Editor/TransBuffer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    /** Find an actor in the editor world by label (display name) or internal name. */
    AActor* FindActorByLabel(const FString& Label)
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World)
        {
            return nullptr;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor && (Actor->GetActorLabel() == Label || Actor->GetName() == Label))
            {
                return Actor;
            }
        }
        return nullptr;
    }

    /** Build a compact JSON object describing an actor. */
    TSharedPtr<FJsonObject> ActorToJson(AActor* Actor)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

        const FVector Loc = Actor->GetActorLocation();
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), Loc.X);
        LocObj->SetNumberField(TEXT("y"), Loc.Y);
        LocObj->SetNumberField(TEXT("z"), Loc.Z);
        Obj->SetObjectField(TEXT("location"), LocObj);

        const FRotator Rot = Actor->GetActorRotation();
        TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
        RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
        RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
        RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
        Obj->SetObjectField(TEXT("rotation"), RotObj);

        return Obj;
    }

    /** Read an {x,y,z} JSON object field into FVector. */
    FVector VectorFromJsonField(const TSharedPtr<FJsonObject>& Params, const FString& Field, FVector Default = FVector::ZeroVector)
    {
        const TSharedPtr<FJsonObject>* SubObj = nullptr;
        if (Params->TryGetObjectField(Field, SubObj) && SubObj)
        {
            double X = Default.X, Y = Default.Y, Z = Default.Z;
            (*SubObj)->TryGetNumberField(TEXT("x"), X);
            (*SubObj)->TryGetNumberField(TEXT("y"), Y);
            (*SubObj)->TryGetNumberField(TEXT("z"), Z);
            return FVector(X, Y, Z);
        }
        return Default;
    }

    /** Read a {pitch,yaw,roll} JSON object field into FRotator. */
    FRotator RotatorFromJsonField(const TSharedPtr<FJsonObject>& Params, const FString& Field, FRotator Default = FRotator::ZeroRotator)
    {
        const TSharedPtr<FJsonObject>* SubObj = nullptr;
        if (Params->TryGetObjectField(Field, SubObj) && SubObj)
        {
            double Pitch = Default.Pitch, Yaw = Default.Yaw, Roll = Default.Roll;
            (*SubObj)->TryGetNumberField(TEXT("pitch"), Pitch);
            (*SubObj)->TryGetNumberField(TEXT("yaw"), Yaw);
            (*SubObj)->TryGetNumberField(TEXT("roll"), Roll);
            return FRotator(Pitch, Yaw, Roll);
        }
        return Default;
    }
} // namespace

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEEditorCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // spawn_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("spawn_actor"),
            TEXT("Editor"),
            TEXT("Spawn an actor in the current level"),
            {
                FSmithUEToolParam(TEXT("class"),    TEXT("string"), TEXT("Actor class name (e.g. StaticMeshActor, PointLight)"), true),
                FSmithUEToolParam(TEXT("name"),     TEXT("string"), TEXT("Actor label shown in World Outliner")),
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Spawn location {x,y,z}")),
                FSmithUEToolParam(TEXT("rotation"), TEXT("object"), TEXT("Spawn rotation {pitch,yaw,roll}")),
                FSmithUEToolParam(TEXT("scale"),    TEXT("object"), TEXT("Scale {x,y,z}"))
            }),
        &HandleSpawnActor);

    // get_all_actors
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_all_actors"),
            TEXT("Editor"),
            TEXT("List all actors in the current level"),
            {
                FSmithUEToolParam(TEXT("class_filter"), TEXT("string"), TEXT("Optional class name filter"))
            }),
        &HandleGetAllActors);

    // set_actor_property
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_actor_property"),
            TEXT("Editor"),
            TEXT("Set a reflected property on an actor by label"),
            {
                FSmithUEToolParam(TEXT("actor_label"),   TEXT("string"), TEXT("Actor label"), true),
                FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name"), true),
                FSmithUEToolParam(TEXT("value"),         TEXT("string"), TEXT("Property value (string/number/bool)"), true)
            }),
        &HandleSetActorProperty);

    // delete_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("delete_actor"),
            TEXT("Editor"),
            TEXT("Delete an actor from the current level by label"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label"), true)
            }),
        &HandleDeleteActor);

    // get_viewport_info
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_viewport_info"),
            TEXT("Editor"),
            TEXT("Get the active editor viewport camera location, rotation and FOV"),
            {}),
        &HandleGetViewportInfo);

    // add_postprocess_material
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("add_postprocess_material"),
            TEXT("Editor"),
            TEXT("Add a material to a PostProcessVolume's blendable list"),
            {
                FSmithUEToolParam(TEXT("actor_label"),   TEXT("string"), TEXT("PostProcessVolume actor label"), true),
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Material asset path (e.g. /Game/Materials/M_PP_ThermalVision)"), true),
                FSmithUEToolParam(TEXT("weight"),        TEXT("number"), TEXT("Blend weight (0.0-1.0, default 1.0)"))
            }),
        &HandleAddPostProcessMaterial);

    // get_project_setting
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_project_setting"),
            TEXT("Editor"),
            TEXT("Read a project configuration setting from INI file"),
            {
                FSmithUEToolParam(TEXT("section"),      TEXT("string"), TEXT("INI section name"), true),
                FSmithUEToolParam(TEXT("key"),          TEXT("string"), TEXT("INI key name"), true),
                FSmithUEToolParam(TEXT("config_file"),  TEXT("string"), TEXT("Config file: Engine (default), Editor, Game, Input"))
            }),
        &HandleGetProjectSetting);

    // set_project_setting
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_project_setting"),
            TEXT("Editor"),
            TEXT("Write a project configuration setting to INI file and flush to disk"),
            {
                FSmithUEToolParam(TEXT("section"),      TEXT("string"), TEXT("INI section name"), true),
                FSmithUEToolParam(TEXT("key"),          TEXT("string"), TEXT("INI key name"), true),
                FSmithUEToolParam(TEXT("value"),        TEXT("string"), TEXT("Value to set"), true),
                FSmithUEToolParam(TEXT("config_file"),  TEXT("string"), TEXT("Config file: Engine (default), Editor, Game, Input"))
            }),
        &HandleSetProjectSetting);
}

// ---------------------------------------------------------------------------
// Command 1: spawn_actor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // --- required: class ---
    FString ClassName;
    if (!Params->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'class'"));
    }

    // --- find UClass by name ---
    UClass* ActorClass = FindObject<UClass>(ANY_PACKAGE, *ClassName);
    if (!ActorClass)
    {
        // Try loading it (handles short names like "StaticMeshActor")
        ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr,
            *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
    }
    if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown or non-actor class: '%s'"), *ClassName));
    }

    // --- world ---
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // --- optional transform ---
    const FVector  Location = VectorFromJsonField(Params, TEXT("location"), FVector::ZeroVector);
    const FRotator Rotation = RotatorFromJsonField(Params, TEXT("rotation"), FRotator::ZeroRotator);
    const FVector  Scale    = VectorFromJsonField(Params, TEXT("scale"),    FVector::OneVector);

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));
    SpawnTransform.SetScale3D(Scale);

    // --- spawn inside a transaction ---
    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Spawn Actor")));

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
    if (!NewActor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("SpawnActor failed for class '%s'"), *ClassName));
    }

    // --- optional label (accept both "label" and "name") ---
    FString ActorName;
    if ((!Params->TryGetStringField(TEXT("label"), ActorName) || ActorName.IsEmpty()) &&
        (!Params->TryGetStringField(TEXT("name"),  ActorName) || ActorName.IsEmpty()))
    {
        ActorName.Empty();
    }
    if (!ActorName.IsEmpty())
    {
        NewActor->SetActorLabel(ActorName);
    }

    UE_LOG(LogSmithUE, Log, TEXT("spawn_actor: spawned '%s' (%s)"),
        *NewActor->GetActorLabel(), *ClassName);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
    Data->SetStringField(TEXT("class"), ClassName);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 2: get_all_actors
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleGetAllActors(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    FString ClassFilter;
    Params->TryGetStringField(TEXT("class_filter"), ClassFilter);

    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor)
        {
            continue;
        }
        if (!ClassFilter.IsEmpty())
        {
            const FString ActorClassName = Actor->GetClass()->GetName();
            if (!ActorClassName.Contains(ClassFilter))
            {
                continue;
            }
        }
        ActorArray.Add(MakeShared<FJsonValueObject>(ActorToJson(Actor)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("actors"), ActorArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 3: set_actor_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'property_name'"));
    }

    if (!Params->HasField(TEXT("value")))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'value'"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    // Try actor first, then fall through to components
    FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
    UObject* PropOwner = Actor;
    if (!Prop)
    {
        for (UActorComponent* Comp : Actor->GetComponents())
        {
            if (!Comp) continue;
            Prop = Comp->GetClass()->FindPropertyByName(FName(*PropertyName));
            if (Prop) { PropOwner = Comp; break; }
        }
    }
    if (!Prop)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Property not found: '%s' on actor '%s'"),
                *PropertyName, *ActorLabel));
    }

    // Reject unsupported complex property types
    if (Prop->IsA<FMapProperty>() || Prop->IsA<FSetProperty>() ||
        Prop->IsA<FDelegateProperty>() || Prop->IsA<FMulticastDelegateProperty>() ||
        Prop->IsA<FSoftObjectProperty>() || Prop->IsA<FSoftClassProperty>())
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Complex property type not supported for '%s'"), *PropertyName));
    }

    TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("value"));
    void* PropAddr = Prop->ContainerPtrToValuePtr<void>(PropOwner);

    // Capture before value
    FString BeforeValue;
    Prop->ExportTextItem_Direct(BeforeValue, PropAddr, nullptr, PropOwner, PPF_None);

    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Set Actor Property")));
    Actor->Modify();
    if (PropOwner != Actor)
    {
        CastChecked<UActorComponent>(PropOwner)->Modify();
    }

    FText ImportError;
    bool bSuccess = false;

    // Convert JSON value to string for ImportText
    FString ValueStr;
    if (!JsonValue.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("'value' parameter is missing or null"));
    }
    if (JsonValue->Type == EJson::String) ValueStr = JsonValue->AsString();
    else if (JsonValue->Type == EJson::Number) ValueStr = FString::SanitizeFloat(JsonValue->AsNumber());
    else if (JsonValue->Type == EJson::Boolean) ValueStr = JsonValue->AsBool() ? TEXT("True") : TEXT("False");

    // Use ImportText as primary method
    if (!ValueStr.IsEmpty())
    {
        const TCHAR* Result = Prop->ImportText(*ValueStr, PropAddr, 0, PropOwner);
        bSuccess = (Result != nullptr);
    }

    // Fallback: direct type-specific setting for bitfield bools
    if (!bSuccess && Prop->IsA<FBoolProperty>())
    {
        CastField<FBoolProperty>(Prop)->SetPropertyValue(PropAddr, JsonValue->AsBool());
        bSuccess = true;
    }

    if (!bSuccess)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to set property '%s': %s"),
                *PropertyName, *ImportError.ToString()));
    }

    // Special handling: bHidden needs SetActorHiddenInGame to propagate to components
    if (PropertyName == TEXT("bHidden"))
    {
        Actor->SetActorHiddenInGame(JsonValue->AsBool());
    }

    // Propagate visual update: re-register the owning component to rebuild
    // its render proxy and recalculate world transform. This avoids
    // Actor::PostEditChangeProperty which triggers RerunConstructionScripts
    // and reverts simple property values.
    if (UActorComponent* Comp = Cast<UActorComponent>(PropOwner))
    {
        FComponentReregisterContext ReregCtx(Comp);
    }

    // Mark dirty + force viewport redraw
    Actor->MarkPackageDirty();
    if (GEditor)
    {
        GEditor->RedrawLevelEditingViewports();
    }

    FString AfterValue;
    Prop->ExportTextItem_Direct(AfterValue, PropAddr, nullptr, PropOwner, PPF_None);

    UE_LOG(LogSmithUE, Log, TEXT("set_actor_property: '%s'.%s set successfully"), *ActorLabel, *PropertyName);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetStringField(TEXT("property_name"), PropertyName);
    Data->SetStringField(TEXT("before"), BeforeValue);
    Data->SetStringField(TEXT("after"), AfterValue);
    Data->SetBoolField(TEXT("changed"), BeforeValue != AfterValue);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 4: delete_actor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    UWorld* World = Actor->GetWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get world from actor"));
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Delete Actor")));

    UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    bool bDeleted = false;
    if (ActorSubsystem)
    {
        bDeleted = ActorSubsystem->DestroyActor(Actor);
    }
    else
    {
        bDeleted = World->EditorDestroyActor(Actor, true);
    }

    if (!bDeleted)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to delete actor: '%s'"), *ActorLabel));
    }

    UE_LOG(LogSmithUE, Log, TEXT("delete_actor: deleted '%s'"), *ActorLabel);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("deleted_actor_label"), ActorLabel);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 5: get_viewport_info
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleGetViewportInfo(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
    if (!LevelEditorModule)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("LevelEditor module not available"));
    }

    TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule->GetFirstActiveViewport();
    if (!ActiveViewport.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No active level editor viewport found"));
    }

    FEditorViewportClient& Client = static_cast<FEditorViewportClient&>(ActiveViewport->GetAssetViewportClient());
    const FVector  ViewLoc = Client.GetViewLocation();
    const FRotator ViewRot = Client.GetViewRotation();
    const float    FOV     = Client.ViewFOV;

    TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
    LocObj->SetNumberField(TEXT("x"), ViewLoc.X);
    LocObj->SetNumberField(TEXT("y"), ViewLoc.Y);
    LocObj->SetNumberField(TEXT("z"), ViewLoc.Z);

    TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
    RotObj->SetNumberField(TEXT("pitch"), ViewRot.Pitch);
    RotObj->SetNumberField(TEXT("yaw"),   ViewRot.Yaw);
    RotObj->SetNumberField(TEXT("roll"),  ViewRot.Roll);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetObjectField(TEXT("location"), LocObj);
    Data->SetObjectField(TEXT("rotation"), RotObj);
    Data->SetNumberField(TEXT("fov"), FOV);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 6: add_postprocess_material
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleAddPostProcessMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'material_path'"));
    }

    double Weight = 1.0;
    Params->TryGetNumberField(TEXT("weight"), Weight);

    // Find actor
    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    APostProcessVolume* PPV = Cast<APostProcessVolume>(Actor);
    if (!PPV)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor '%s' is not a PostProcessVolume (class: %s)"),
                *ActorLabel, *Actor->GetClass()->GetName()));
    }

    // Load material
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found at path: '%s'"), *MaterialPath));
    }

    // Add to blendables
    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Add PostProcess Material")));
    PPV->Modify();

    FWeightedBlendable Entry;
    Entry.Weight = (float)Weight;
    Entry.Object = Material;
    PPV->Settings.WeightedBlendables.Array.Add(Entry);

    // Mark dirty + force viewport redraw
     PPV->MarkPackageDirty();
     if (GEditor)
     {
         GEditor->RedrawLevelEditingViewports();
     }

     UE_LOG(LogSmithUE, Log, TEXT("add_postprocess_material: added '%s' to '%s' (weight=%.2f)"),
         *MaterialPath, *ActorLabel, Weight);

     TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
     Data->SetStringField(TEXT("actor_label"), ActorLabel);
     Data->SetStringField(TEXT("material_path"), MaterialPath);
     Data->SetNumberField(TEXT("weight"), Weight);
     Data->SetNumberField(TEXT("blendable_count"), PPV->Settings.WeightedBlendables.Array.Num());
     return FSmithUECommonUtils::CreateSuccessResponse(Data);
 }

 // ---------------------------------------------------------------------------
 // Command 7: get_project_setting
 // ---------------------------------------------------------------------------

 TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleGetProjectSetting(const TSharedPtr<FJsonObject>& Params)
 {
     if (!Params)
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid parameters"));
     }

     FString Section, Key, ConfigFileName;
     if (!Params->TryGetStringField(TEXT("section"), Section) || Section.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'section' parameter"));
     }
     if (!Params->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'key' parameter"));
     }
     Params->TryGetStringField(TEXT("config_file"), ConfigFileName);

     // Resolve config file path
     auto ResolveConfigFile = [](const FString& Name) -> FString {
         if (Name.Equals(TEXT("Editor"), ESearchCase::IgnoreCase)) return GEditorIni;
         if (Name.Equals(TEXT("Game"), ESearchCase::IgnoreCase)) return GGameIni;
         if (Name.Equals(TEXT("Input"), ESearchCase::IgnoreCase)) return GInputIni;
         return GEngineIni; // default
     };

     FString ConfigFile = ResolveConfigFile(ConfigFileName);
     FString OutValue;
     bool bFound = GConfig->GetString(*Section, *Key, OutValue, ConfigFile);

     TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
     Data->SetStringField(TEXT("section"), Section);
     Data->SetStringField(TEXT("key"), Key);
     Data->SetStringField(TEXT("value"), OutValue);
     Data->SetBoolField(TEXT("found"), bFound);

     UE_LOG(LogSmithUE, Log, TEXT("get_project_setting: [%s] %s = '%s' (found=%d)"),
         *Section, *Key, *OutValue, bFound);

     return FSmithUECommonUtils::CreateSuccessResponse(Data);
 }

 // ---------------------------------------------------------------------------
 // Command 8: set_project_setting
 // ---------------------------------------------------------------------------

 TSharedPtr<FJsonObject> FSmithUEEditorCommands::HandleSetProjectSetting(const TSharedPtr<FJsonObject>& Params)
 {
     if (!Params)
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid parameters"));
     }

     FString Section, Key, Value, ConfigFileName;
     if (!Params->TryGetStringField(TEXT("section"), Section) || Section.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'section' parameter"));
     }
     if (!Params->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty 'key' parameter"));
     }
     if (!Params->TryGetStringField(TEXT("value"), Value))
     {
         return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));
     }
     Params->TryGetStringField(TEXT("config_file"), ConfigFileName);

     // Resolve config file path
     auto ResolveConfigFile = [](const FString& Name) -> FString {
         if (Name.Equals(TEXT("Editor"), ESearchCase::IgnoreCase)) return GEditorIni;
         if (Name.Equals(TEXT("Game"), ESearchCase::IgnoreCase)) return GGameIni;
         if (Name.Equals(TEXT("Input"), ESearchCase::IgnoreCase)) return GInputIni;
         return GEngineIni; // default
     };

     FString ConfigFile = ResolveConfigFile(ConfigFileName);

     // Read old value for logging
     FString OldValue;
     GConfig->GetString(*Section, *Key, OldValue, ConfigFile);

     // Write new value
     GConfig->SetString(*Section, *Key, *Value, ConfigFile);

     // CRITICAL: Flush to disk
     GConfig->Flush(false, ConfigFile);

     // Optionally apply CVar at runtime if it exists
     IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Key);
     if (CVar)
     {
         CVar->Set(*Value);
     }

     UE_LOG(LogSmithUE, Log, TEXT("set_project_setting: [%s] %s = '%s' (was '%s')"),
         *Section, *Key, *Value, *OldValue);

     TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
     Data->SetBoolField(TEXT("persisted"), true);

     return FSmithUECommonUtils::CreateSuccessResponse(Data);
 }

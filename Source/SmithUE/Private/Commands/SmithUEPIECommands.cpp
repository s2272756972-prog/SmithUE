// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEPIECommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

namespace SmithUEPIE
{
    UWorld* GetPIEWorld()
    {
        if (!GEditor) return nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World())
            {
                return Context.World();
            }
        }
        return nullptr;
    }

    bool IsPIERunning()
    {
        return GEditor && GEditor->IsPlayingSessionInEditor();
    }

    AActor* FindActorByLabel(UWorld* World, const FString& Label)
    {
        if (!World) return nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == Label || It->GetName() == Label)
            {
                return *It;
            }
        }
        return nullptr;
    }

    TSharedPtr<FJsonObject> VectorToJson(const FVector& V)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("x"), V.X);
        Obj->SetNumberField(TEXT("y"), V.Y);
        Obj->SetNumberField(TEXT("z"), V.Z);
        return Obj;
    }

    bool JsonToVector(const TSharedPtr<FJsonObject>& Json, FVector& OutVector)
    {
        if (!Json.IsValid()) return false;
        double X = 0, Y = 0, Z = 0;
        Json->TryGetNumberField(TEXT("x"), X);
        Json->TryGetNumberField(TEXT("y"), Y);
        Json->TryGetNumberField(TEXT("z"), Z);
        OutVector = FVector(X, Y, Z);
        return true;
    }

    bool JsonToRotator(const TSharedPtr<FJsonObject>& Json, FRotator& OutRotator)
    {
        if (!Json.IsValid()) return false;
        double Pitch = 0, Yaw = 0, Roll = 0;
        Json->TryGetNumberField(TEXT("pitch"), Pitch);
        Json->TryGetNumberField(TEXT("yaw"), Yaw);
        Json->TryGetNumberField(TEXT("roll"), Roll);
        OutRotator = FRotator(Pitch, Yaw, Roll);
        return true;
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEPIECommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_teleport_actor"),
            TEXT("PIE"),
            TEXT("Teleport an actor in the PIE world"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label or name"), true),
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("{x,y,z} target location"), true),
                FSmithUEToolParam(TEXT("rotation"), TEXT("object"), TEXT("Optional {pitch,yaw,roll} rotation"))
            }),
        &HandlePieTeleportActor);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_spawn_actor"),
            TEXT("PIE"),
            TEXT("Spawn an actor in the PIE world"),
            {
                FSmithUEToolParam(TEXT("class_path"), TEXT("string"), TEXT("Blueprint or native class path"), true),
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("{x,y,z} spawn location"), true),
                FSmithUEToolParam(TEXT("rotation"), TEXT("object"), TEXT("Optional {pitch,yaw,roll}")),
                FSmithUEToolParam(TEXT("label"), TEXT("string"), TEXT("Optional actor label"))
            }),
        &HandlePieSpawnActor);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_destroy_actor"),
            TEXT("PIE"),
            TEXT("Destroy an actor in the PIE world"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label or name"), true)
            }),
        &HandlePieDestroyActor);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_get_property"),
            TEXT("PIE"),
            TEXT("Get a property value from an actor via reflection"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label or name"), true),
                FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name"), true)
            }),
        &HandlePieGetProperty);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_set_property"),
            TEXT("PIE"),
            TEXT("Set a property value on an actor via reflection"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label or name"), true),
                FSmithUEToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("string"), TEXT("Value as string (converted via property import)"), true)
            }),
        &HandlePieSetProperty);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_get_game_state"),
            TEXT("PIE"),
            TEXT("Get PIE running state, player location, actor count"),
            {}),
        &HandlePieGetGameState);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_list_actors"),
            TEXT("PIE"),
            TEXT("List actors in the PIE world"),
            {
                FSmithUEToolParam(TEXT("class_filter"), TEXT("string"), TEXT("Optional class name filter")),
                FSmithUEToolParam(TEXT("name_filter"), TEXT("string"), TEXT("Optional name substring filter"))
            }),
        &HandlePieListActors);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_console_command"),
            TEXT("PIE"),
            TEXT("Execute a console command in the PIE world"),
            {
                FSmithUEToolParam(TEXT("command"), TEXT("string"), TEXT("Console command string"), true)
            }),
        &HandlePieConsoleCommand);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_start"),
            TEXT("PIE"),
            TEXT("Start a Play-In-Editor session"),
            {}),
        &HandlePieStart);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_stop"),
            TEXT("PIE"),
            TEXT("Stop the active PIE session"),
            {}),
        &HandlePieStop);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pie_is_active"),
            TEXT("PIE"),
            TEXT("Check whether a PIE session is currently active and return its mode"),
            {}),
        &HandlePieIsActive);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieTeleportActor(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running. Start Play-In-Editor first."));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    AActor* Actor = SmithUEPIE::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in PIE"), *ActorLabel));

    const TSharedPtr<FJsonObject>* LocPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("location"), LocPtr) || !LocPtr || !LocPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("location is required"));
    }
    FVector Location;
    SmithUEPIE::JsonToVector(*LocPtr, Location);

    FRotator Rotation = Actor->GetActorRotation();
    const TSharedPtr<FJsonObject>* RotPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("rotation"), RotPtr) && RotPtr && RotPtr->IsValid())
    {
        SmithUEPIE::JsonToRotator(*RotPtr, Rotation);
    }

    Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetField(TEXT("location"), MakeShared<FJsonValueObject>(SmithUEPIE::VectorToJson(Location)));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString ClassPath, Label;
    Params->TryGetStringField(TEXT("class_path"), ClassPath);
    Params->TryGetStringField(TEXT("label"), Label);

    UClass* ActorClass = LoadClass<AActor>(nullptr, *ClassPath);
    if (!ActorClass)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load class '%s'"), *ClassPath));
    }

    const TSharedPtr<FJsonObject>* LocPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("location"), LocPtr) || !LocPtr || !LocPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("location is required"));
    }
    FVector Location;
    SmithUEPIE::JsonToVector(*LocPtr, Location);

    FRotator Rotation = FRotator::ZeroRotator;
    const TSharedPtr<FJsonObject>* RotPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("rotation"), RotPtr) && RotPtr && RotPtr->IsValid())
    {
        SmithUEPIE::JsonToRotator(*RotPtr, Rotation);
    }

    FActorSpawnParameters SpawnParams;
    if (!Label.IsEmpty())
    {
        SpawnParams.Name = FName(*Label);
    }

    AActor* NewActor = World->SpawnActor(ActorClass, &Location, &Rotation, SpawnParams);
    if (!NewActor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to spawn actor"));
    }

    if (!Label.IsEmpty())
    {
        NewActor->SetActorLabel(Label);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_name"), NewActor->GetName());
    Data->SetStringField(TEXT("class"), ActorClass->GetName());
    Data->SetField(TEXT("location"), MakeShared<FJsonValueObject>(SmithUEPIE::VectorToJson(Location)));
    UE_LOG(LogSmithUE, Log, TEXT("pie_spawn_actor: spawned %s at (%f,%f,%f)"), *NewActor->GetName(), Location.X, Location.Y, Location.Z);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieDestroyActor(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    AActor* Actor = SmithUEPIE::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in PIE"), *ActorLabel));

    FString Name = Actor->GetName();
    Actor->Destroy();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("destroyed"), Name);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieGetProperty(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString ActorLabel, PropertyName;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
    Params->TryGetStringField(TEXT("property_name"), PropertyName);

    AActor* Actor = SmithUEPIE::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in PIE"), *ActorLabel));

    FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
    if (!Prop) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found"), *PropertyName));

    FString ValueStr;
    const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
    Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetStringField(TEXT("property"), PropertyName);
    Data->SetStringField(TEXT("value"), ValueStr);
    Data->SetStringField(TEXT("type"), Prop->GetCPPType());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieSetProperty(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString ActorLabel, PropertyName, Value;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
    Params->TryGetStringField(TEXT("property_name"), PropertyName);
    Params->TryGetStringField(TEXT("value"), Value);

    AActor* Actor = SmithUEPIE::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in PIE"), *ActorLabel));

    FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
    if (!Prop) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found"), *PropertyName));

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
    const TCHAR* Result = Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None);
    if (!Result)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to import value '%s' for property '%s'"), *Value, *PropertyName));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetStringField(TEXT("property"), PropertyName);
    Data->SetStringField(TEXT("value"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieGetGameState(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("pie_running"), SmithUEPIE::IsPIERunning());

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World)
    {
        Data->SetStringField(TEXT("message"), TEXT("PIE is not running"));
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    // Actor count
    int32 ActorCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        ++ActorCount;
    }
    Data->SetNumberField(TEXT("actor_count"), ActorCount);
    Data->SetNumberField(TEXT("time_seconds"), World->GetTimeSeconds());

    // Player location
    APlayerController* PC = World->GetFirstPlayerController();
    if (PC)
    {
        APawn* Pawn = PC->GetPawn();
        if (Pawn)
        {
            Data->SetField(TEXT("player_location"), MakeShared<FJsonValueObject>(SmithUEPIE::VectorToJson(Pawn->GetActorLocation())));
            Data->SetStringField(TEXT("player_pawn"), Pawn->GetName());
        }
    }

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieListActors(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString ClassFilter, NameFilter;
    Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
    Params->TryGetStringField(TEXT("name_filter"), NameFilter);

    TArray<TSharedPtr<FJsonValue>> ActorsArray;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        FString ClassName = Actor->GetClass()->GetName();
        FString ActorName = Actor->GetName();

        if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter))
        {
            continue;
        }
        if (!NameFilter.IsEmpty() && !ActorName.Contains(NameFilter))
        {
            continue;
        }

        TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
        ActorObj->SetStringField(TEXT("name"), ActorName);
        ActorObj->SetStringField(TEXT("class"), ClassName);
        ActorObj->SetField(TEXT("location"), MakeShared<FJsonValueObject>(SmithUEPIE::VectorToJson(Actor->GetActorLocation())));
        ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));

        if (ActorsArray.Num() >= 100)
        {
            break; // cap at 100
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("count"), ActorsArray.Num());
    Data->SetArrayField(TEXT("actors"), ActorsArray);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieConsoleCommand(const TSharedPtr<FJsonObject>& Params)
{
    if (!SmithUEPIE::IsPIERunning())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
    }

    UWorld* World = SmithUEPIE::GetPIEWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No PIE world found"));

    FString Command;
    Params->TryGetStringField(TEXT("command"), Command);

    if (Command.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("command is required"));
    }

    GEngine->Exec(World, *Command);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("command"), Command);
    Data->SetStringField(TEXT("status"), TEXT("executed"));
    UE_LOG(LogSmithUE, Log, TEXT("pie_console_command: %s"), *Command);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// pie_start / pie_stop / pie_is_active
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieStart(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    FRequestPlaySessionParams PlayParams;
    GEditor->RequestPlaySession(PlayParams);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("requested"), true);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieStop(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    if (GEditor->PlayWorld == nullptr)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No active PIE session to stop"));
    }

    GEditor->RequestEndPlayMap();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("stopped"), true);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPIECommands::HandlePieIsActive(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    const bool bActive = GEditor->PlayWorld != nullptr;
    FString Mode = TEXT("none");
    if (bActive)
    {
        Mode = GEditor->bIsSimulatingInEditor ? TEXT("simulate") : TEXT("play");
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("active"), bActive);
    Data->SetStringField(TEXT("mode"), Mode);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

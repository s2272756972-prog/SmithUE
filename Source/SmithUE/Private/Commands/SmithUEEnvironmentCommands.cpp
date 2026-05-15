// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEEnvironmentCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SplineComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "GameFramework/Actor.h"

namespace SmithUEEnvironment
{
    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    AActor* FindActorByLabel(UWorld* World, const FString& Label)
    {
        if (!World) return nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == Label)
            {
                return *It;
            }
        }
        return nullptr;
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

    TSharedPtr<FJsonObject> VectorToJson(const FVector& V)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("x"), V.X);
        Obj->SetNumberField(TEXT("y"), V.Y);
        Obj->SetNumberField(TEXT("z"), V.Z);
        return Obj;
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEEnvironmentCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_post_process"),
            TEXT("Environment"),
            TEXT("Configure post-process volume settings"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("PostProcessVolume actor label (first found if omitted)")),
                FSmithUEToolParam(TEXT("settings"), TEXT("object"), TEXT("Settings object: bloom_intensity, exposure_compensation, color_saturation, etc."), true)
            }),
        &HandleEnvSetPostProcess);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_fog"),
            TEXT("Environment"),
            TEXT("Set exponential height fog properties"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Fog actor label (first found if omitted)")),
                FSmithUEToolParam(TEXT("settings"), TEXT("object"), TEXT("Settings: density, height_falloff, start_distance, color{r,g,b}"), true)
            }),
        &HandleEnvSetFog);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_sky_atmosphere"),
            TEXT("Environment"),
            TEXT("Set sky atmosphere parameters"),
            {
                FSmithUEToolParam(TEXT("settings"), TEXT("object"), TEXT("Settings: rayleigh_scattering_scale, mie_scattering_scale, atmosphere_height"), true)
            }),
        &HandleEnvSetSkyAtmosphere);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_light"),
            TEXT("Environment"),
            TEXT("Set directional/point/spot light properties"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Light actor label"), true),
                FSmithUEToolParam(TEXT("settings"), TEXT("object"), TEXT("Settings: intensity, color{r,g,b}, temperature, use_temperature, attenuation_radius"), true)
            }),
        &HandleEnvSetLight);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_physics"),
            TEXT("Environment"),
            TEXT("Enable/disable physics simulation on an actor"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label"), true),
                FSmithUEToolParam(TEXT("simulate"), TEXT("bool"), TEXT("Enable or disable physics"), true),
                FSmithUEToolParam(TEXT("gravity_override"), TEXT("float"), TEXT("Optional gravity scale override"))
            }),
        &HandleEnvSetPhysics);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_collision"),
            TEXT("Environment"),
            TEXT("Set collision profile/preset on an actor"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label"), true),
                FSmithUEToolParam(TEXT("profile_name"), TEXT("string"), TEXT("Collision profile name (e.g. BlockAll, OverlapAll, NoCollision)"), true)
            }),
        &HandleEnvSetCollision);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_get_physics_info"),
            TEXT("Environment"),
            TEXT("Get physics/collision info for an actor"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label"), true)
            }),
        &HandleEnvGetPhysicsInfo);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_create_spline"),
            TEXT("Environment"),
            TEXT("Create a spline actor with specified points"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Actor label name"), true),
                FSmithUEToolParam(TEXT("points"), TEXT("array"), TEXT("Array of {x,y,z} point objects"), true),
                FSmithUEToolParam(TEXT("closed"), TEXT("bool"), TEXT("Whether the spline is closed loop"))
            }),
        &HandleEnvCreateSpline);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_add_spline_point"),
            TEXT("Environment"),
            TEXT("Add a point to an existing spline actor"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Spline actor label"), true),
                FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("{x,y,z} position"), true),
                FSmithUEToolParam(TEXT("index"), TEXT("int"), TEXT("Insert index (appends if omitted)"))
            }),
        &HandleEnvAddSplinePoint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_set_spline_point"),
            TEXT("Environment"),
            TEXT("Modify a spline point position and tangent"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Spline actor label"), true),
                FSmithUEToolParam(TEXT("index"), TEXT("int"), TEXT("Point index to modify"), true),
                FSmithUEToolParam(TEXT("position"), TEXT("object"), TEXT("{x,y,z} new position"), true),
                FSmithUEToolParam(TEXT("tangent"), TEXT("object"), TEXT("Optional {x,y,z} arrive tangent"))
            }),
        &HandleEnvSetSplinePoint);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("env_get_spline_info"),
            TEXT("Environment"),
            TEXT("Get spline point count, length, closed state"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Spline actor label"), true)
            }),
        &HandleEnvGetSplineInfo);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetPostProcess(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    const TSharedPtr<FJsonObject>* SettingsPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("settings"), SettingsPtr) || !SettingsPtr || !SettingsPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("settings is required"));
    }
    const TSharedPtr<FJsonObject>& Settings = *SettingsPtr;

    APostProcessVolume* PPV = nullptr;
    if (ActorLabel.IsEmpty())
    {
        for (TActorIterator<APostProcessVolume> It(World); It; ++It)
        {
            PPV = *It;
            break;
        }
    }
    else
    {
        AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
        PPV = Cast<APostProcessVolume>(Actor);
    }

    if (!PPV) return FSmithUECommonUtils::CreateErrorResponse(TEXT("PostProcessVolume not found"));

    FPostProcessSettings& S = PPV->Settings;
    double Val;
    if (Settings->TryGetNumberField(TEXT("bloom_intensity"), Val)) { S.bOverride_BloomIntensity = true; S.BloomIntensity = (float)Val; }
    if (Settings->TryGetNumberField(TEXT("exposure_compensation"), Val)) { S.bOverride_AutoExposureBias = true; S.AutoExposureBias = (float)Val; }
    if (Settings->TryGetNumberField(TEXT("auto_exposure_min"), Val)) { S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = (float)Val; }
    if (Settings->TryGetNumberField(TEXT("auto_exposure_max"), Val)) { S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = (float)Val; }
    if (Settings->TryGetNumberField(TEXT("vignette_intensity"), Val)) { S.bOverride_VignetteIntensity = true; S.VignetteIntensity = (float)Val; }

    const TSharedPtr<FJsonObject>* ColorSatPtr = nullptr;
    if (Settings->TryGetObjectField(TEXT("color_saturation"), ColorSatPtr) && ColorSatPtr && ColorSatPtr->IsValid())
    {
        S.bOverride_ColorSaturation = true;
        double R = 1, G = 1, B = 1, A = 1;
        (*ColorSatPtr)->TryGetNumberField(TEXT("r"), R);
        (*ColorSatPtr)->TryGetNumberField(TEXT("g"), G);
        (*ColorSatPtr)->TryGetNumberField(TEXT("b"), B);
        (*ColorSatPtr)->TryGetNumberField(TEXT("a"), A);
        S.ColorSaturation = FVector4((float)R, (float)G, (float)B, (float)A);
    }

    PPV->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), PPV->GetActorLabel());
    UE_LOG(LogSmithUE, Log, TEXT("env_set_post_process: updated %s"), *PPV->GetActorLabel());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetFog(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    const TSharedPtr<FJsonObject>* SettingsPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("settings"), SettingsPtr) || !SettingsPtr || !SettingsPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("settings is required"));
    }
    const TSharedPtr<FJsonObject>& Settings = *SettingsPtr;

    AExponentialHeightFog* FogActor = nullptr;
    if (ActorLabel.IsEmpty())
    {
        for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
        {
            FogActor = *It;
            break;
        }
    }
    else
    {
        AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
        FogActor = Cast<AExponentialHeightFog>(Actor);
    }

    if (!FogActor) return FSmithUECommonUtils::CreateErrorResponse(TEXT("ExponentialHeightFog actor not found"));

    UExponentialHeightFogComponent* FogComp = FogActor->GetComponent();
    if (!FogComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No fog component"));

    double Val;
    if (Settings->TryGetNumberField(TEXT("density"), Val)) FogComp->FogDensity = (float)Val;
    if (Settings->TryGetNumberField(TEXT("height_falloff"), Val)) FogComp->FogHeightFalloff = (float)Val;
    if (Settings->TryGetNumberField(TEXT("start_distance"), Val)) FogComp->StartDistance = (float)Val;
    if (Settings->TryGetNumberField(TEXT("max_opacity"), Val)) FogComp->FogMaxOpacity = (float)Val;

    const TSharedPtr<FJsonObject>* ColorPtr = nullptr;
    if (Settings->TryGetObjectField(TEXT("color"), ColorPtr) && ColorPtr && ColorPtr->IsValid())
    {
        double R = 1, G = 1, B = 1;
        (*ColorPtr)->TryGetNumberField(TEXT("r"), R);
        (*ColorPtr)->TryGetNumberField(TEXT("g"), G);
        (*ColorPtr)->TryGetNumberField(TEXT("b"), B);
        FogComp->SetFogInscatteringColor(FLinearColor((float)R, (float)G, (float)B));
    }

    FogComp->MarkRenderStateDirty();
    FogActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), FogActor->GetActorLabel());
    Data->SetNumberField(TEXT("density"), FogComp->FogDensity);
    UE_LOG(LogSmithUE, Log, TEXT("env_set_fog: updated %s"), *FogActor->GetActorLabel());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetSkyAtmosphere(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    const TSharedPtr<FJsonObject>* SettingsPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("settings"), SettingsPtr) || !SettingsPtr || !SettingsPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("settings is required"));
    }
    const TSharedPtr<FJsonObject>& Settings = *SettingsPtr;

    USkyAtmosphereComponent* SkyComp = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        SkyComp = (*It)->FindComponentByClass<USkyAtmosphereComponent>();
        if (SkyComp) break;
    }

    if (!SkyComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No SkyAtmosphereComponent found in level"));

    double Val;
    if (Settings->TryGetNumberField(TEXT("rayleigh_scattering_scale"), Val)) SkyComp->RayleighScatteringScale = (float)Val;
    if (Settings->TryGetNumberField(TEXT("mie_scattering_scale"), Val)) SkyComp->MieScatteringScale = (float)Val;
    if (Settings->TryGetNumberField(TEXT("atmosphere_height"), Val)) SkyComp->AtmosphereHeight = (float)Val;
    if (Settings->TryGetNumberField(TEXT("mie_absorption_scale"), Val)) SkyComp->MieAbsorptionScale = (float)Val;

    SkyComp->MarkRenderStateDirty();
    SkyComp->GetOwner()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), SkyComp->GetOwner()->GetActorLabel());
    UE_LOG(LogSmithUE, Log, TEXT("env_set_sky_atmosphere: updated"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetLight(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    const TSharedPtr<FJsonObject>* SettingsPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("settings"), SettingsPtr) || !SettingsPtr || !SettingsPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("settings is required"));
    }
    const TSharedPtr<FJsonObject>& Settings = *SettingsPtr;

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    ULightComponent* LightComp = Actor->FindComponentByClass<ULightComponent>();
    if (!LightComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no LightComponent"));

    double Val;
    if (Settings->TryGetNumberField(TEXT("intensity"), Val)) LightComp->SetIntensity((float)Val);
    if (Settings->TryGetNumberField(TEXT("temperature"), Val)) LightComp->SetTemperature((float)Val);

    bool BoolVal;
    if (Settings->TryGetBoolField(TEXT("use_temperature"), BoolVal)) LightComp->bUseTemperature = BoolVal;

    const TSharedPtr<FJsonObject>* ColorPtr = nullptr;
    if (Settings->TryGetObjectField(TEXT("color"), ColorPtr) && ColorPtr && ColorPtr->IsValid())
    {
        double R = 1, G = 1, B = 1;
        (*ColorPtr)->TryGetNumberField(TEXT("r"), R);
        (*ColorPtr)->TryGetNumberField(TEXT("g"), G);
        (*ColorPtr)->TryGetNumberField(TEXT("b"), B);
        LightComp->SetLightColor(FLinearColor((float)R, (float)G, (float)B));
    }

    if (UPointLightComponent* PointLight = Cast<UPointLightComponent>(LightComp))
    {
        if (Settings->TryGetNumberField(TEXT("attenuation_radius"), Val)) PointLight->SetAttenuationRadius((float)Val);
    }

    LightComp->MarkRenderStateDirty();
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetStringField(TEXT("light_type"), LightComp->GetClass()->GetName());
    Data->SetNumberField(TEXT("intensity"), LightComp->Intensity);
    UE_LOG(LogSmithUE, Log, TEXT("env_set_light: updated %s"), *ActorLabel);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetPhysics(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    bool Simulate = false;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
    Params->TryGetBoolField(TEXT("simulate"), Simulate);

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    UPrimitiveComponent* PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
    if (!PrimComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no PrimitiveComponent"));

    PrimComp->SetSimulatePhysics(Simulate);

    double GravityOverride;
    if (Params->TryGetNumberField(TEXT("gravity_override"), GravityOverride))
    {
        PrimComp->SetEnableGravity(GravityOverride != 0.0);
    }

    PrimComp->MarkRenderStateDirty();
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetBoolField(TEXT("simulate_physics"), Simulate);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetCollision(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel, ProfileName;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
    Params->TryGetStringField(TEXT("profile_name"), ProfileName);

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    UPrimitiveComponent* PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
    if (!PrimComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no PrimitiveComponent"));

    PrimComp->SetCollisionProfileName(FName(*ProfileName));
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetStringField(TEXT("collision_profile"), ProfileName);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvGetPhysicsInfo(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    UPrimitiveComponent* PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
    if (!PrimComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no PrimitiveComponent"));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), ActorLabel);
    Data->SetBoolField(TEXT("simulate_physics"), PrimComp->IsSimulatingPhysics());
    Data->SetBoolField(TEXT("gravity_enabled"), PrimComp->IsGravityEnabled());
    Data->SetStringField(TEXT("collision_profile"), PrimComp->GetCollisionProfileName().ToString());
    Data->SetBoolField(TEXT("collision_enabled"), PrimComp->IsCollisionEnabled());
    Data->SetNumberField(TEXT("mass"), PrimComp->GetMass());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvCreateSpline(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString Name;
    Params->TryGetStringField(TEXT("name"), Name);

    const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
    if (!Params->TryGetArrayField(TEXT("points"), PointsArray) || !PointsArray || PointsArray->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("points array is required and must not be empty"));
    }

    bool Closed = false;
    Params->TryGetBoolField(TEXT("closed"), Closed);

    // Parse points
    TArray<FVector> Points;
    for (const TSharedPtr<FJsonValue>& PointVal : *PointsArray)
    {
        const TSharedPtr<FJsonObject>* PointObj = nullptr;
        if (PointVal->TryGetObject(PointObj) && PointObj && PointObj->IsValid())
        {
            FVector V;
            SmithUEEnvironment::JsonToVector(*PointObj, V);
            Points.Add(V);
        }
    }

    if (Points.Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No valid points parsed"));
    }

    // Spawn actor with spline component
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = FName(*Name);
    AActor* SplineActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (!SplineActor) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to spawn spline actor"));

    SplineActor->SetActorLabel(Name);

    USplineComponent* SplineComp = NewObject<USplineComponent>(SplineActor, TEXT("SplineComponent"));
    SplineComp->RegisterComponent();
    SplineActor->AddInstanceComponent(SplineComp);
    SplineActor->SetRootComponent(SplineComp);

    SplineComp->ClearSplinePoints(false);
    for (int32 i = 0; i < Points.Num(); ++i)
    {
        SplineComp->AddSplinePoint(Points[i], ESplineCoordinateSpace::World, false);
    }
    SplineComp->SetClosedLoop(Closed, false);
    SplineComp->UpdateSpline();

    SplineActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), Name);
    Data->SetNumberField(TEXT("point_count"), Points.Num());
    Data->SetBoolField(TEXT("closed"), Closed);
    Data->SetNumberField(TEXT("spline_length"), SplineComp->GetSplineLength());
    UE_LOG(LogSmithUE, Log, TEXT("env_create_spline: created '%s' with %d points"), *Name, Points.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvAddSplinePoint(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
    if (!SplineComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no SplineComponent"));

    const TSharedPtr<FJsonObject>* PosPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("position"), PosPtr) || !PosPtr || !PosPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("position is required"));
    }

    FVector Position;
    SmithUEEnvironment::JsonToVector(*PosPtr, Position);

    double IndexD = -1;
    int32 Index = SplineComp->GetNumberOfSplinePoints(); // default: append
    if (Params->TryGetNumberField(TEXT("index"), IndexD))
    {
        Index = (int32)IndexD;
    }

    SplineComp->AddSplinePointAtIndex(Position, Index, ESplineCoordinateSpace::World, true);
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetNumberField(TEXT("index"), Index);
    Data->SetNumberField(TEXT("point_count"), SplineComp->GetNumberOfSplinePoints());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvSetSplinePoint(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
    if (!SplineComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no SplineComponent"));

    double IndexD = 0;
    Params->TryGetNumberField(TEXT("index"), IndexD);
    int32 Index = (int32)IndexD;

    if (Index < 0 || Index >= SplineComp->GetNumberOfSplinePoints())
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Index %d out of range [0, %d)"), Index, SplineComp->GetNumberOfSplinePoints()));
    }

    const TSharedPtr<FJsonObject>* PosPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("position"), PosPtr) && PosPtr && PosPtr->IsValid())
    {
        FVector Position;
        SmithUEEnvironment::JsonToVector(*PosPtr, Position);
        SplineComp->SetLocationAtSplinePoint(Index, Position, ESplineCoordinateSpace::World, true);
    }

    const TSharedPtr<FJsonObject>* TangentPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("tangent"), TangentPtr) && TangentPtr && TangentPtr->IsValid())
    {
        FVector Tangent;
        SmithUEEnvironment::JsonToVector(*TangentPtr, Tangent);
        SplineComp->SetTangentAtSplinePoint(Index, Tangent, ESplineCoordinateSpace::World, true);
    }

    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetNumberField(TEXT("index"), Index);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEEnvironmentCommands::HandleEnvGetSplineInfo(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUEEnvironment::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    AActor* Actor = SmithUEEnvironment::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
    if (!SplineComp) return FSmithUECommonUtils::CreateErrorResponse(TEXT("Actor has no SplineComponent"));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetNumberField(TEXT("point_count"), SplineComp->GetNumberOfSplinePoints());
    Data->SetNumberField(TEXT("spline_length"), SplineComp->GetSplineLength());
    Data->SetBoolField(TEXT("closed_loop"), SplineComp->IsClosedLoop());

    TArray<TSharedPtr<FJsonValue>> PointsArray;
    for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
    {
        FVector Pos = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
        PointsArray.Add(MakeShared<FJsonValueObject>(SmithUEEnvironment::VectorToJson(Pos)));
    }
    Data->SetArrayField(TEXT("points"), PointsArray);

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

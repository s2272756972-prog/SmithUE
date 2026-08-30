// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUELevelCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"

namespace SmithUELevel
{
    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    static AActor* SpawnSimpleActor(UWorld* World, const TCHAR* ClassName, const FTransform& Xform, const FString& Label)
    {
        if (!World) { return nullptr; }
        UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), ClassName));
        if (!Cls) { return nullptr; }
        FActorSpawnParameters SP;
        SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* A = World->SpawnActor<AActor>(Cls, Xform, SP);
        if (A && !Label.IsEmpty()) { A->SetActorLabel(Label); }
        return A;
    }

    static AStaticMeshActor* SpawnMeshActor(UWorld* World, const TCHAR* MeshPath, const FString& MaterialPath, const FTransform& Xform, const FString& Label)
    {
        if (!World) { return nullptr; }
        FActorSpawnParameters SP;
        SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Xform, SP);
        if (!A) { return nullptr; }
        if (!Label.IsEmpty()) { A->SetActorLabel(Label); }
        UStaticMeshComponent* Comp = A->GetStaticMeshComponent();
        if (Comp)
        {
            if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath))
            {
                Comp->SetStaticMesh(Mesh);
            }
            if (!MaterialPath.IsEmpty())
            {
                if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
                {
                    Comp->SetMaterial(0, Mat);
                }
            }
        }
        return A;
    }

    TSharedPtr<FJsonObject> MakeVectorJson(const FVector& Vector)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("x"), Vector.X);
        Json->SetNumberField(TEXT("y"), Vector.Y);
        Json->SetNumberField(TEXT("z"), Vector.Z);
        return Json;
    }

    bool JsonToVector(const TSharedPtr<FJsonObject>& Json, FVector& OutVector)
    {
        if (!Json.IsValid())
        {
            return false;
        }

        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        if (!Json->TryGetNumberField(TEXT("x"), X) || !Json->TryGetNumberField(TEXT("y"), Y) || !Json->TryGetNumberField(TEXT("z"), Z))
        {
            return false;
        }

        OutVector = FVector(X, Y, Z);
        return true;
    }

    bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
    {
        if (!Value.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
        if (Value->TryGetObject(ObjectPtr) && ObjectPtr && ObjectPtr->IsValid())
        {
            return JsonToVector(*ObjectPtr, OutVector);
        }

        const TArray<TSharedPtr<FJsonValue>>* ArrayPtr = nullptr;
        if (Value->TryGetArray(ArrayPtr) && ArrayPtr && ArrayPtr->Num() >= 3)
        {
            OutVector = FVector((*ArrayPtr)[0]->AsNumber(), (*ArrayPtr)[1]->AsNumber(), (*ArrayPtr)[2]->AsNumber());
            return true;
        }

        return false;
    }

    UStaticMesh* LoadStaticMesh(const FString& MeshPath, FString& OutError)
    {
        UObject* Loaded = StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath);
        UStaticMesh* Mesh = Cast<UStaticMesh>(Loaded);
        if (!Mesh)
        {
            OutError = FString::Printf(TEXT("Failed to load UStaticMesh at '%s'"), *MeshPath);
        }
        return Mesh;
    }

    UFoliageType_InstancedStaticMesh* FindFoliageTypeForMesh(AInstancedFoliageActor* FoliageActor, UStaticMesh* Mesh)
    {
        if (!FoliageActor || !Mesh)
        {
            return nullptr;
        }

        UFoliageType_InstancedStaticMesh* FoundType = nullptr;
        FoliageActor->ForEachFoliageInfo([&](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
        {
            UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
            if (StaticMeshType && StaticMeshType->GetStaticMesh() == Mesh)
            {
                FoundType = StaticMeshType;
                return false;
            }
            return true;
        });

        return FoundType;
    }

    UFoliageType_InstancedStaticMesh* GetOrCreateFoliageTypeForMesh(AInstancedFoliageActor* FoliageActor, UStaticMesh* Mesh)
    {
        if (!FoliageActor || !Mesh)
        {
            return nullptr;
        }

        if (UFoliageType_InstancedStaticMesh* ExistingType = FindFoliageTypeForMesh(FoliageActor, Mesh))
        {
            return ExistingType;
        }

        UFoliageType_InstancedStaticMesh* NewType = NewObject<UFoliageType_InstancedStaticMesh>(FoliageActor, NAME_None, RF_Transactional);
        NewType->SetStaticMesh(Mesh);
        NewType->Density = 100.0f;

        FFoliageInfo* FoliageInfo = nullptr;
        FoliageActor->AddFoliageType(NewType, &FoliageInfo);
        FoliageActor->MarkPackageDirty();
        return NewType;
    }
}

namespace
{
    struct FSmithUEPendingNewMap { bool bValid = false; FString Name; FString Path; };
    static FSmithUEPendingNewMap GSmithUEPendingNewMap;

    static void SmithUE_ExecInternalNewBlankMap()
    {
        if (!GSmithUEPendingNewMap.bValid) { return; }
        GSmithUEPendingNewMap.bValid = false;
        UWorld* W = UEditorLoadingAndSavingUtils::NewBlankMap(true);
        if (W && !GSmithUEPendingNewMap.Path.IsEmpty())
        {
            const FString& P = GSmithUEPendingNewMap.Path;
            const FString Save = P.EndsWith(TEXT("/")) ? P + GSmithUEPendingNewMap.Name : P / GSmithUEPendingNewMap.Name;
            UEditorLoadingAndSavingUtils::SaveMap(W, Save);
        }
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUELevelCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    {
        static bool bConsoleCmdRegistered = false;
        if (!bConsoleCmdRegistered)
        {
            bConsoleCmdRegistered = true;
            IConsoleManager::Get().RegisterConsoleCommand(
                TEXT("SmithUE.InternalNewBlankMap"),
                TEXT("Internal: deferred blank-map creation for level_new (crash-safe)."),
                FConsoleCommandDelegate::CreateStatic(&SmithUE_ExecInternalNewBlankMap),
                ECVF_Default);
        }
    }

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_new"),
            TEXT("Level"),
            TEXT("Create a new blank level/map (deferred next-frame creation, crash-safe). Query level state after ~1s. Executes on the next frame — success means the operation was QUEUED, not finished; query level state after ~1s."),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("New level name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Optional package path or filename for saving the new map"))
            }),
        &HandleLevelNew);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_open"),
            TEXT("Level"),
            TEXT("Open an existing level/map. Executes on the next frame — success means the operation was QUEUED, not finished; query level state after ~1s."),
            {
                FSmithUEToolParam(TEXT("level_path"), TEXT("string"), TEXT("Map package path or filename to open"), true)
            }),
        &HandleLevelOpen);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_save"),
            TEXT("Level"),
            TEXT("Save the current level"),
            {}),
        &HandleLevelSave);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_get_info"),
            TEXT("Level"),
            TEXT("Get current level name, path, and actor count"),
            {}),
        &HandleLevelGetInfo);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_create_landscape"),
            TEXT("Level"),
            TEXT("Create a landscape in the current level"),
            {
                FSmithUEToolParam(TEXT("section_size"), TEXT("integer"), TEXT("Landscape section size")),
                FSmithUEToolParam(TEXT("sections_x"), TEXT("integer"), TEXT("Number of landscape sections along X")),
                FSmithUEToolParam(TEXT("sections_y"), TEXT("integer"), TEXT("Number of landscape sections along Y")),
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Optional landscape material path"))
            }),
        &HandleLevelCreateLandscape);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_set_landscape_material"),
            TEXT("Level"),
            TEXT("Set the material on all landscape proxies in the current level"),
            {
                FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Material asset path"), true)
            }),
        &HandleLevelSetLandscapeMaterial);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_get_landscape_info"),
            TEXT("Level"),
            TEXT("Get landscape proxy component counts, dimensions, and materials"),
            {}),
        &HandleLevelGetLandscapeInfo);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_add_foliage_type"),
            TEXT("Level"),
            TEXT("Add a static mesh foliage type to the current level"),
            {
                FSmithUEToolParam(TEXT("static_mesh_path"), TEXT("string"), TEXT("Static mesh asset path"), true)
            }),
        &HandleLevelAddFoliageType);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_paint_foliage"),
            TEXT("Level"),
            TEXT("Add foliage instances at explicit locations"),
            {
                FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static mesh asset path"), true),
                FSmithUEToolParam(TEXT("locations"), TEXT("array"), TEXT("Array of {x,y,z} objects or [x,y,z] arrays"), true),
                FSmithUEToolParam(TEXT("density"), TEXT("float"), TEXT("Optional foliage type density"))
            }),
        &HandleLevelPaintFoliage);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_erase_foliage"),
            TEXT("Level"),
            TEXT("Remove foliage instances within a radius"),
            {
                FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Optional static mesh asset path filter")),
                FSmithUEToolParam(TEXT("center"), TEXT("object"), TEXT("Center point as {x,y,z}"), true),
                FSmithUEToolParam(TEXT("radius"), TEXT("float"), TEXT("Erase radius"), true)
            }),
        &HandleLevelEraseFoliage);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_get_foliage_stats"),
            TEXT("Level"),
            TEXT("Count foliage types and instances in the current level"),
            {}),
        &HandleLevelGetFoliageStats);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("level_add_basic_env"),
            TEXT("Level"),
            TEXT("Add basic environment to the CURRENT level: directional light, sky atmosphere, sky light, height fog, player start, and a floor. Safe (no world switch)."),
            {
                FSmithUEToolParam(TEXT("floor_scale"), TEXT("number"), TEXT("Uniform floor plane scale (default 20)"))
            }),
        &HandleLevelAddBasicEnv);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelNew(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("name")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString Name;
    FString Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    // Defer the blank-map creation to next frame via DeferredCommands (crash-safe).
    // Calling NewBlankMap synchronously here destroys the world during tick processing
    // while TickTaskManager still holds references (assertion in FreeTickTaskLevel).
    // Our registered console command runs in UEngine::TickDeferredCommands at frame start,
    // safely outside any tick group (same pattern as level_open's MAP LOAD).
    GSmithUEPendingNewMap = FSmithUEPendingNewMap{ true, Name, Path };
    GEngine->DeferredCommands.Add(TEXT("SmithUE.InternalNewBlankMap"));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("requested_name"), Name);
    Data->SetStringField(TEXT("requested_path"), Path);
    Data->SetBoolField(TEXT("scheduled"), true);
    Data->SetStringField(TEXT("note"), TEXT("New map creation deferred to next frame (crash-safe). Query level state after ~1s."));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelAddBasicEnv(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    double FloorScaleD = 20.0;
    if (Params.IsValid()) { Params->TryGetNumberField(TEXT("floor_scale"), FloorScaleD); }
    const float FloorScale = static_cast<float>(FloorScaleD);

    TArray<TSharedPtr<FJsonValue>> Spawned;
    auto Record = [&Spawned](AActor* A)
    {
        if (A) { Spawned.Add(MakeShared<FJsonValueString>(A->GetActorLabel())); }
    };

    FTransform SunXform;
    SunXform.SetRotation(FQuat(FRotator(-46.0f, 30.0f, 0.0f)));
    Record(SmithUELevel::SpawnSimpleActor(World, TEXT("DirectionalLight"), SunXform, TEXT("Sun")));

    Record(SmithUELevel::SpawnSimpleActor(World, TEXT("SkyAtmosphere"), FTransform::Identity, TEXT("SkyAtmosphere")));
    Record(SmithUELevel::SpawnSimpleActor(World, TEXT("SkyLight"), FTransform::Identity, TEXT("SkyLight")));
    Record(SmithUELevel::SpawnSimpleActor(World, TEXT("ExponentialHeightFog"), FTransform::Identity, TEXT("Fog")));

    FTransform StartXform;
    StartXform.SetLocation(FVector(0.0f, 0.0f, 120.0f));
    Record(SmithUELevel::SpawnSimpleActor(World, TEXT("PlayerStart"), StartXform, TEXT("PlayerStart")));

    FTransform FloorXform;
    FloorXform.SetScale3D(FVector(FloorScale, FloorScale, 1.0f));
    Record(SmithUELevel::SpawnMeshActor(World, TEXT("/Engine/BasicShapes/Plane.Plane"), FString(), FloorXform, TEXT("Floor")));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("spawned"), Spawned);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelOpen(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("level_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    FString LevelPath;
    Params->TryGetStringField(TEXT("level_path"), LevelPath);

    // Defer map load to next frame via DeferredCommands.
    // Loading a map during tick processing (AsyncTask picked up by WaitUntilTasksComplete)
    // destroys the world while TickTaskManager still holds tick level references,
    // causing assertion: !LevelList.Contains(TickTaskLevel) in FreeTickTaskLevel.
    // DeferredCommands execute at the beginning of the next frame in UEngine::TickDeferredCommands,
    // safely outside any tick group.
    GEngine->DeferredCommands.Add(FString::Printf(TEXT("MAP LOAD \"%s\""), *LevelPath));

    UWorld* World = SmithUELevel::GetEditorWorld();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("level_path"), LevelPath);
    Data->SetStringField(TEXT("level_name"), World ? World->GetMapName() : TEXT(""));
    Data->SetBoolField(TEXT("loaded"), true);
    Data->SetStringField(TEXT("note"), TEXT("Map load deferred to next frame. Allow ~1s before querying new level state."));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelSave(const TSharedPtr<FJsonObject>& Params)
{
    const bool bSaved = FEditorFileUtils::SaveCurrentLevel();
    if (!bSaved)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to save current level"));
    }

    UWorld* World = SmithUELevel::GetEditorWorld();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("level_name"), World ? World->GetMapName() : TEXT(""));
    Data->SetStringField(TEXT("level_path"), World && World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
    Data->SetBoolField(TEXT("saved"), true);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelGetInfo(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    int32 ActorCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        ++ActorCount;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("level_name"), World->GetMapName());
    Data->SetStringField(TEXT("level_path"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
    Data->SetNumberField(TEXT("actor_count"), ActorCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelCreateLandscape(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("created"), false);
    Data->SetStringField(TEXT("message"), TEXT("Landscape creation is not supported by this SmithUE command. Use the editor Landscape tool, then manage material/info through SmithUE."));
    Data->SetStringField(TEXT("reason"), TEXT("Landscape creation requires editor-mode/import setup that is unsafe to invoke as a generic automation command."));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelSetLandscapeMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("material_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    FString MaterialPath;
    Params->TryGetStringField(TEXT("material_path"), MaterialPath);
    UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
    if (!Material)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
    }

    int32 UpdatedCount = 0;
    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
    {
        ALandscapeProxy* Landscape = *It;
        Landscape->Modify();
        Landscape->LandscapeMaterial = Material;
        Landscape->PostEditChange();
        Landscape->MarkPackageDirty();
        ++UpdatedCount;
    }

    if (UpdatedCount == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No landscape proxy found in the current level"));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetNumberField(TEXT("updated_landscapes"), UpdatedCount);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelGetLandscapeInfo(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    TArray<TSharedPtr<FJsonValue>> Landscapes;
    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
    {
        ALandscapeProxy* Landscape = *It;
        FVector Origin;
        FVector Extent;
        Landscape->GetActorBounds(false, Origin, Extent);

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Landscape->GetActorNameOrLabel());
        Item->SetStringField(TEXT("class"), Landscape->GetClass()->GetName());
        Item->SetNumberField(TEXT("component_count"), Landscape->LandscapeComponents.Num());
        Item->SetObjectField(TEXT("origin"), SmithUELevel::MakeVectorJson(Origin));
        Item->SetObjectField(TEXT("dimensions"), SmithUELevel::MakeVectorJson(Extent * 2.0));
        Item->SetStringField(TEXT("material_path"), Landscape->LandscapeMaterial ? Landscape->LandscapeMaterial->GetPathName() : TEXT(""));
        Landscapes.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("landscape_count"), Landscapes.Num());
    Data->SetArrayField(TEXT("landscapes"), Landscapes);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelAddFoliageType(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("static_mesh_path")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    FString MeshPath;
    Params->TryGetStringField(TEXT("static_mesh_path"), MeshPath);
    UStaticMesh* Mesh = SmithUELevel::LoadStaticMesh(MeshPath, Error);
    if (!Mesh)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
    if (!FoliageActor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get or create InstancedFoliageActor"));
    }

    const bool bAlreadyExisted = SmithUELevel::FindFoliageTypeForMesh(FoliageActor, Mesh) != nullptr;
    UFoliageType_InstancedStaticMesh* FoliageType = SmithUELevel::GetOrCreateFoliageTypeForMesh(FoliageActor, Mesh);
    if (!FoliageType)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to add foliage type"));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("static_mesh_path"), MeshPath);
    Data->SetStringField(TEXT("foliage_type"), FoliageType->GetName());
    Data->SetBoolField(TEXT("already_existed"), bAlreadyExisted);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelPaintFoliage(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("mesh_path"), TEXT("locations")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    FString MeshPath;
    Params->TryGetStringField(TEXT("mesh_path"), MeshPath);
    UStaticMesh* Mesh = SmithUELevel::LoadStaticMesh(MeshPath, Error);
    if (!Mesh)
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* LocationsJson = nullptr;
    if (!Params->TryGetArrayField(TEXT("locations"), LocationsJson) || !LocationsJson || LocationsJson->Num() == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing or empty required parameter: locations"));
    }

    AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
    if (!FoliageActor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get or create InstancedFoliageActor"));
    }

    UFoliageType_InstancedStaticMesh* FoliageType = SmithUELevel::GetOrCreateFoliageTypeForMesh(FoliageActor, Mesh);
    if (!FoliageType)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to get or create foliage type"));
    }

    double Density = 0.0;
    if (Params->TryGetNumberField(TEXT("density"), Density) && Density > 0.0)
    {
        FoliageType->Modify();
        FoliageType->Density = Density;
    }

    TArray<FTransform> Transforms;
    for (const TSharedPtr<FJsonValue>& LocationValue : *LocationsJson)
    {
        FVector Location;
        if (!SmithUELevel::JsonValueToVector(LocationValue, Location))
        {
            return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid foliage location. Expected {x,y,z} object or [x,y,z] array."));
        }
        Transforms.Add(FTransform(FRotator::ZeroRotator, Location, FVector::OneVector));
    }

    FoliageActor->Modify();
    
    FFoliageInfo* FoliageInfo = nullptr;
    FoliageActor->ForEachFoliageInfo([&](UFoliageType* Type, FFoliageInfo& Info)
    {
        if (Type == FoliageType)
        {
            FoliageInfo = &Info;
            return false;
        }
        return true;
    });
    
    if (FoliageInfo)
    {
        FoliageInfo->ReserveAdditionalInstances(FoliageType, Transforms.Num());
        for (const FTransform& Transform : Transforms)
        {
            FFoliageInstance NewInst;
            NewInst.Location = Transform.GetLocation();
            NewInst.Rotation = Transform.GetRotation().Rotator();
            NewInst.DrawScale3D = FVector3f(Transform.GetScale3D());
            FoliageInfo->AddInstance(FoliageType, NewInst);
        }
    }
    FoliageActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mesh_path"), MeshPath);
    Data->SetNumberField(TEXT("instances_added"), Transforms.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelEraseFoliage(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("center"), TEXT("radius")}, Error))
    {
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    }

    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    const TSharedPtr<FJsonObject>* CenterObject = nullptr;
    FVector Center;
    if (!Params->TryGetObjectField(TEXT("center"), CenterObject) || !CenterObject || !SmithUELevel::JsonToVector(*CenterObject, Center))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid center. Expected {x,y,z} object."));
    }

    double Radius = 0.0;
    if (!Params->TryGetNumberField(TEXT("radius"), Radius) || Radius <= 0.0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Invalid radius. Expected a positive number."));
    }

    FString MeshPath;
    UStaticMesh* MeshFilter = nullptr;
    if (Params->TryGetStringField(TEXT("mesh_path"), MeshPath) && !MeshPath.IsEmpty())
    {
        MeshFilter = SmithUELevel::LoadStaticMesh(MeshPath, Error);
        if (!MeshFilter)
        {
            return FSmithUECommonUtils::CreateErrorResponse(Error);
        }
    }

    int32 RemovedCount = 0;
    for (TActorIterator<AInstancedFoliageActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AInstancedFoliageActor* FoliageActor = *ActorIt;
        FoliageActor->Modify();
        FoliageActor->ForEachFoliageInfo([&](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
        {
            UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
            if (MeshFilter && (!StaticMeshType || StaticMeshType->GetStaticMesh() != MeshFilter))
            {
                return true;
            }

            TArray<int32> InstancesToRemove;
            const int32 InstanceCount = FoliageInfo.Instances.Num();
            for (int32 Index = 0; Index < InstanceCount; ++Index)
            {
                if (FVector::DistSquared(FoliageInfo.Instances[Index].Location, Center) <= FMath::Square(Radius))
                {
                    InstancesToRemove.Add(Index);
                }
            }

            if (InstancesToRemove.Num() > 0)
            {
                RemovedCount += InstancesToRemove.Num();
                FoliageInfo.RemoveInstances(InstancesToRemove, true);
            }
            return true;
        });
        FoliageActor->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("instances_removed"), RemovedCount);
    Data->SetObjectField(TEXT("center"), SmithUELevel::MakeVectorJson(Center));
    Data->SetNumberField(TEXT("radius"), Radius);
    if (!MeshPath.IsEmpty())
    {
        Data->SetStringField(TEXT("mesh_path"), MeshPath);
    }
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUELevelCommands::HandleLevelGetFoliageStats(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = SmithUELevel::GetEditorWorld();
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world is available"));
    }

    int32 TypeCount = 0;
    int32 InstanceCount = 0;
    TArray<TSharedPtr<FJsonValue>> Types;

    for (TActorIterator<AInstancedFoliageActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AInstancedFoliageActor* FoliageActor = *ActorIt;
        FoliageActor->ForEachFoliageInfo([&](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
        {
            const int32 Count = FoliageInfo.Instances.Num();
            ++TypeCount;
            InstanceCount += Count;

            TSharedPtr<FJsonObject> TypeJson = MakeShared<FJsonObject>();
            TypeJson->SetStringField(TEXT("foliage_type"), FoliageType ? FoliageType->GetName() : TEXT(""));
            TypeJson->SetStringField(TEXT("foliage_actor"), FoliageActor->GetActorNameOrLabel());
            TypeJson->SetNumberField(TEXT("instance_count"), Count);
            if (UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType))
            {
                UStaticMesh* Mesh = StaticMeshType->GetStaticMesh();
                TypeJson->SetStringField(TEXT("static_mesh_path"), Mesh ? Mesh->GetPathName() : TEXT(""));
            }
            Types.Add(MakeShared<FJsonValueObject>(TypeJson));
            return true;
        });
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("foliage_type_count"), TypeCount);
    Data->SetNumberField(TEXT("foliage_instance_count"), InstanceCount);
    Data->SetArrayField(TEXT("types"), Types);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

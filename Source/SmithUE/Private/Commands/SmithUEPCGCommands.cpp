// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEPCGCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "PCGComponent.h"

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEPCGCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("create_pcg_graph"),
            TEXT("PCG"),
            TEXT("Create a new PCG Graph asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content path e.g. /Game/PCG"), true)
            }),
        &FSmithUEPCGCommands::HandleCreatePcgGraph);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("find_pcg_graphs"),
            TEXT("PCG"),
            TEXT("Find PCG Graph assets in the project"),
            {
                FSmithUEToolParam(TEXT("query"), TEXT("string"), TEXT("Optional name filter substring"))
            }),
        &FSmithUEPCGCommands::HandleFindPcgGraphs);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("spawn_pcg_volume"),
            TEXT("PCG"),
            TEXT("Spawn a PCG Volume actor in the editor world and assign a PCG Graph"),
            {
                FSmithUEToolParam(TEXT("graph"), TEXT("string"), TEXT("PCG Graph asset path e.g. /Game/PCG/MyGraph"), true),
                FSmithUEToolParam(TEXT("label"), TEXT("string"), TEXT("Actor label"), false),
                FSmithUEToolParam(TEXT("x"), TEXT("number"), TEXT("Location X"), false),
                FSmithUEToolParam(TEXT("y"), TEXT("number"), TEXT("Location Y"), false),
                FSmithUEToolParam(TEXT("z"), TEXT("number"), TEXT("Location Z"), false),
                FSmithUEToolParam(TEXT("scaleX"), TEXT("number"), TEXT("Scale X"), false),
                FSmithUEToolParam(TEXT("scaleY"), TEXT("number"), TEXT("Scale Y"), false),
                FSmithUEToolParam(TEXT("scaleZ"), TEXT("number"), TEXT("Scale Z"), false)
            }),
        &FSmithUEPCGCommands::HandleSpawnPcgVolume);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("pcg_generate"),
            TEXT("PCG"),
            TEXT("Force regenerate PCG on an existing PCG Volume actor"),
            {
                FSmithUEToolParam(TEXT("actor"), TEXT("string"), TEXT("Actor label of the PCG Volume"), true)
            }),
        &FSmithUEPCGCommands::HandlePcgGenerate);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleCreatePcgGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    if (Name.IsEmpty() || Path.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("name and path are required"));
    }

    // Normalize
    Name.RemoveFromEnd(TEXT(".uasset"));
    if (!Path.StartsWith(TEXT("/"))) Path = TEXT("/") + Path;
    while (Path.EndsWith(TEXT("/"))) Path.RemoveAt(Path.Len() - 1);

    const FString PackagePath = Path + TEXT("/") + Name;

    // Return existing if already present
    if (UObject* Existing = LoadObject<UPCGGraph>(nullptr, *PackagePath))
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("alreadyExists"), true);
        Data->SetStringField(TEXT("name"), Name);
        Data->SetStringField(TEXT("path"), Existing->GetPathName());
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
    }

    UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *Name, RF_Public | RF_Standalone);
    if (!Graph)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UPCGGraph"));
    }

    FAssetRegistryModule::AssetCreated(Graph);
    Graph->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("path"), Graph->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleFindPcgGraphs(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    Params->TryGetStringField(TEXT("query"), Query);

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FARFilter Filter;
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/PCG"), TEXT("PCGGraph")));
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(TEXT("/Game"));

    TArray<FAssetData> Found;
    ARM.Get().GetAssets(Filter, Found);

    TArray<TSharedPtr<FJsonValue>> Arr;
    for (const FAssetData& A : Found)
    {
        const FString AssetName = A.AssetName.ToString();
        if (!Query.IsEmpty() && !AssetName.Contains(Query, ESearchCase::IgnoreCase))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), AssetName);
        Obj->SetStringField(TEXT("path"), A.GetObjectPathString());
        Arr.Add(MakeShared<FJsonValueObject>(Obj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("count"), Arr.Num());
    Data->SetArrayField(TEXT("graphs"), Arr);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleSpawnPcgVolume(const TSharedPtr<FJsonObject>& Params)
{
    FString GraphPath, Label;
    Params->TryGetStringField(TEXT("graph"), GraphPath);
    Params->TryGetStringField(TEXT("label"), Label);

    if (GraphPath.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("graph (PCG graph asset path) is required"));
    }

    if (Label.IsEmpty())
    {
        Label = TEXT("PCG_Volume");
    }

    UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
    if (!Graph)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available"));
    }

    FVector Location(0.0, 0.0, 0.0);
    FVector Scale(20.0, 20.0, 5.0);

    double Val = 0.0;
    if (Params->TryGetNumberField(TEXT("x"), Val)) Location.X = Val;
    if (Params->TryGetNumberField(TEXT("y"), Val)) Location.Y = Val;
    if (Params->TryGetNumberField(TEXT("z"), Val)) Location.Z = Val;
    if (Params->TryGetNumberField(TEXT("scaleX"), Val)) Scale.X = Val;
    if (Params->TryGetNumberField(TEXT("scaleY"), Val)) Scale.Y = Val;
    if (Params->TryGetNumberField(TEXT("scaleZ"), Val)) Scale.Z = Val;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    APCGVolume* Volume = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
    if (!Volume)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to spawn APCGVolume"));
    }

    Volume->SetActorScale3D(Scale);
    Volume->SetActorLabel(Label);

    if (UPCGComponent* Comp = Volume->FindComponentByClass<UPCGComponent>())
    {
        Comp->SetGraph(Graph);
        Comp->Generate();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor"), Volume->GetActorLabel());
    Data->SetStringField(TEXT("graph"), GraphPath);
    Data->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), Location.X, Location.Y, Location.Z));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandlePcgGenerate(const TSharedPtr<FJsonObject>& Params)
{
    FString TargetLabel;
    Params->TryGetStringField(TEXT("actor"), TargetLabel);

    if (TargetLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("actor (label of the PCG volume) is required"));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available"));
    }

    int32 Hits = 0;
    for (TActorIterator<APCGVolume> It(World); It; ++It)
    {
        APCGVolume* Vol = *It;
        if (!Vol) continue;
        if (!Vol->GetActorLabel().Equals(TargetLabel, ESearchCase::IgnoreCase)) continue;

        if (UPCGComponent* Comp = Vol->FindComponentByClass<UPCGComponent>())
        {
            Comp->Generate();
            Hits++;
        }
    }

    if (Hits == 0)
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No PCGVolume with label '%s'"), *TargetLabel));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("result"), TEXT("generated"));
    Data->SetNumberField(TEXT("regenerated"), Hits);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

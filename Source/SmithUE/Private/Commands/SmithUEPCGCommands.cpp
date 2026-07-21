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
// Local helpers
// ---------------------------------------------------------------------------
namespace
{
	UWorld* GetPcgEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	void ReadVec(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FVector& InOut)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(Field, Obj) && Obj && Obj->IsValid())
		{
			double V = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("x"), V)) { InOut.X = V; }
			if ((*Obj)->TryGetNumberField(TEXT("y"), V)) { InOut.Y = V; }
			if ((*Obj)->TryGetNumberField(TEXT("z"), V)) { InOut.Z = V; }
		}
	}
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEPCGCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(
		FSmithUEToolSchema(
			TEXT("create_pcg_graph"),
			TEXT("PCG"),
			TEXT("Create an empty PCG Graph asset (Procedural Content Generation). Read it back with read_pcg_graph; drive it in a level via spawn_pcg_volume. Returns already_exists=true (not an error) if the asset is already present."),
			{
				FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Asset name (no extension)"), /*required*/ true),
				FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder, e.g. /Game/PCG"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandleCreatePcgGraph);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("read_pcg_graph"),
			TEXT("PCG"),
			TEXT("Read a PCG Graph asset: name, path, node count, and whether it has input/output nodes. Read-only."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path, e.g. /Game/PCG/MyGraph"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandleReadPcgGraph);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("find_pcg_graphs"),
			TEXT("PCG"),
			TEXT("List PCG Graph assets under /Game (optionally filtered by a name substring). Read-only."),
			{
				FSmithUEToolParam(TEXT("query"), TEXT("string"), TEXT("Optional case-insensitive name filter substring"), /*required*/ false)
			}),
		&FSmithUEPCGCommands::HandleFindPcgGraphs);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("spawn_pcg_volume"),
			TEXT("PCG"),
			TEXT("Spawn a PCG Volume actor in the current editor level, assign a PCG Graph to its PCG Component, and generate. MUTATES the level (spawns an actor). Needs an editor world (not during PIE)."),
			{
				FSmithUEToolParam(TEXT("graph_path"), TEXT("string"), TEXT("PCG Graph asset path to assign, e.g. /Game/PCG/MyGraph"), /*required*/ true),
				FSmithUEToolParam(TEXT("label"), TEXT("string"), TEXT("Actor label (default 'PCG_Volume')"), /*required*/ false),
				FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Spawn location {x,y,z} (default origin)"), /*required*/ false),
				FSmithUEToolParam(TEXT("scale"), TEXT("object"), TEXT("Actor scale {x,y,z} (default {20,20,5})"), /*required*/ false)
			}),
		&FSmithUEPCGCommands::HandleSpawnPcgVolume);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("pcg_generate"),
			TEXT("PCG"),
			TEXT("Force-regenerate PCG on an existing PCG Volume actor (by label) in the current editor level."),
			{
				FSmithUEToolParam(TEXT("actor"), TEXT("string"), TEXT("Actor label of the PCG Volume"), /*required*/ true)
			}),
		&FSmithUEPCGCommands::HandlePcgGenerate);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleCreatePcgGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("name"), TEXT("path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString Name, Path;
	Params->TryGetStringField(TEXT("name"), Name);
	Params->TryGetStringField(TEXT("path"), Path);

	Name.RemoveFromEnd(TEXT(".uasset"));
	if (!Path.StartsWith(TEXT("/"))) { Path = TEXT("/") + Path; }
	while (Path.EndsWith(TEXT("/"))) { Path.RemoveAt(Path.Len() - 1); }

	const FString PackagePath = Path + TEXT("/") + Name;

	if (UPCGGraph* Existing = LoadObject<UPCGGraph>(nullptr, *PackagePath))
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("already_exists"), true);
		Data->SetStringField(TEXT("name"), Name);
		Data->SetStringField(TEXT("path"), Existing->GetPathName());
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package '%s'"), *PackagePath));
	}

	UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *Name, RF_Public | RF_Standalone);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create UPCGGraph"));
	}

	FAssetRegistryModule::AssetCreated(Graph);
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("already_exists"), false);
	Data->SetStringField(TEXT("name"), Name);
	Data->SetStringField(TEXT("path"), Graph->GetPathName());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleReadPcgGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString GraphPath;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s'"), *GraphPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Graph->GetName());
	Data->SetStringField(TEXT("path"), Graph->GetPathName());
	Data->SetNumberField(TEXT("node_count"), Graph->GetNodes().Num());
	Data->SetBoolField(TEXT("has_input_node"), Graph->GetInputNode() != nullptr);
	Data->SetBoolField(TEXT("has_output_node"), Graph->GetOutputNode() != nullptr);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandleFindPcgGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (Params.IsValid()) { Params->TryGetStringField(TEXT("query"), Query); }

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
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("graph_path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString GraphPath, Label;
	Params->TryGetStringField(TEXT("graph_path"), GraphPath);
	Params->TryGetStringField(TEXT("label"), Label);
	if (Label.IsEmpty()) { Label = TEXT("PCG_Volume"); }

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: '%s' (create it with create_pcg_graph first)"), *GraphPath));
	}

	UWorld* World = GetPcgEditorWorld();
	if (!World)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available (not during PIE)"));
	}

	FVector Location(0.0, 0.0, 0.0);
	FVector Scale(20.0, 20.0, 5.0);
	ReadVec(Params, TEXT("location"), Location);
	ReadVec(Params, TEXT("scale"), Scale);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APCGVolume* Volume = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Volume)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to spawn APCGVolume"));
	}

	Volume->SetActorScale3D(Scale);
	Volume->SetActorLabel(Label);

	bool bGenerated = false;
	if (UPCGComponent* Comp = Volume->FindComponentByClass<UPCGComponent>())
	{
		Comp->SetGraph(Graph);
		Comp->Generate();
		bGenerated = true;
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("actor"), Volume->GetActorLabel());
	Data->SetStringField(TEXT("graph_path"), GraphPath);
	Data->SetBoolField(TEXT("generated"), bGenerated);
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Location.X);
	LocObj->SetNumberField(TEXT("y"), Location.Y);
	LocObj->SetNumberField(TEXT("z"), Location.Z);
	Data->SetObjectField(TEXT("location"), LocObj);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEPCGCommands::HandlePcgGenerate(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("actor") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}

	FString TargetLabel;
	Params->TryGetStringField(TEXT("actor"), TargetLabel);

	UWorld* World = GetPcgEditorWorld();
	if (!World)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world available (not during PIE)"));
	}

	int32 Hits = 0;
	for (TActorIterator<APCGVolume> It(World); It; ++It)
	{
		APCGVolume* Vol = *It;
		if (!Vol || !Vol->GetActorLabel().Equals(TargetLabel, ESearchCase::IgnoreCase)) { continue; }
		if (UPCGComponent* Comp = Vol->FindComponentByClass<UPCGComponent>())
		{
			Comp->Generate();
			Hits++;
		}
	}

	if (Hits == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No PCG Volume actor with label '%s' (spawn one with spawn_pcg_volume)"), *TargetLabel));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("result"), TEXT("generated"));
	Data->SetNumberField(TEXT("regenerated"), Hits);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

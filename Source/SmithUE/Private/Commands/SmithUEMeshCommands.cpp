// Copyright 2026, 123dx-svg. MIT License.
#include "Commands/SmithUEMeshCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Animation/Skeleton.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshEditorSubsystem.h"
#include "StaticMeshEditorSubsystemHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	UStaticMesh* LoadStaticMesh(const FString& Path)
	{
		return LoadObject<UStaticMesh>(nullptr, *Path);
	}

	UStaticMeshEditorSubsystem* GetMeshSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
	}

	bool ResolveCollisionShape(const FString& In, EScriptCollisionShapeType& Out)
	{
		const FString S = In.ToLower();
		if (S == TEXT("box")) { Out = EScriptCollisionShapeType::Box; return true; }
		if (S == TEXT("sphere")) { Out = EScriptCollisionShapeType::Sphere; return true; }
		if (S == TEXT("capsule")) { Out = EScriptCollisionShapeType::Capsule; return true; }
		if (S == TEXT("ndop10x") || S == TEXT("ndop10_x")) { Out = EScriptCollisionShapeType::NDOP10_X; return true; }
		if (S == TEXT("ndop10y") || S == TEXT("ndop10_y")) { Out = EScriptCollisionShapeType::NDOP10_Y; return true; }
		if (S == TEXT("ndop10z") || S == TEXT("ndop10_z")) { Out = EScriptCollisionShapeType::NDOP10_Z; return true; }
		if (S == TEXT("ndop18")) { Out = EScriptCollisionShapeType::NDOP18; return true; }
		if (S == TEXT("ndop26")) { Out = EScriptCollisionShapeType::NDOP26; return true; }
		return false;
	}
}

void FSmithUEMeshCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(FSmithUEToolSchema(TEXT("read_mesh_info"), TEXT("Mesh"),
		TEXT("Read a Static Mesh's build state: LOD count + screen sizes, material slot count, simple-collision primitive count, Nanite enabled flag, and LOD0 vertex count. The assertion companion for mesh build tools. Read-only."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static Mesh asset path"), true) }),
		&FSmithUEMeshCommands::HandleReadMeshInfo);

	Registry.Register(FSmithUEToolSchema(TEXT("mesh_add_collision"), TEXT("Mesh"),
		TEXT("Add a simple collision primitive to a Static Mesh (auto-fits the mesh bounds). shape = box/sphere/capsule/ndop10x/ndop10y/ndop10z/ndop18/ndop26. Adds to any existing collision. Returns the new collision primitive count. MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static Mesh asset path"), true),
		  FSmithUEToolParam(TEXT("shape"), TEXT("string"), TEXT("box/sphere/capsule/ndop10x/ndop18/ndop26"), true).SetExample(TEXT("box")) }),
		&FSmithUEMeshCommands::HandleMeshAddCollision);

	Registry.Register(FSmithUEToolSchema(TEXT("mesh_remove_collision"), TEXT("Mesh"),
		TEXT("Remove ALL simple collision primitives from a Static Mesh. MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static Mesh asset path"), true) }),
		&FSmithUEMeshCommands::HandleMeshRemoveCollision);

	Registry.Register(FSmithUEToolSchema(TEXT("mesh_set_nanite"), TEXT("Mesh"),
		TEXT("Enable or disable Nanite on a Static Mesh (rebuilds the mesh). MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static Mesh asset path"), true),
		  FSmithUEToolParam(TEXT("enabled"), TEXT("bool"), TEXT("true to enable Nanite, false to disable"), true) }),
		&FSmithUEMeshCommands::HandleMeshSetNanite);

	Registry.Register(FSmithUEToolSchema(TEXT("mesh_generate_lods"), TEXT("Mesh"),
		TEXT("Auto-generate reduction LODs for a Static Mesh. lod_count is the total number of LODs (incl. LOD0). Each successive LOD halves the triangle percentage (LOD0=100%, LOD1=50%, ...); screen sizes are auto-computed. Replaces existing LODs. MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static Mesh asset path"), true),
		  FSmithUEToolParam(TEXT("lod_count"), TEXT("int"), TEXT("Total LOD count incl. LOD0 (1-8)"), true).SetExample(TEXT("4")) }),
		&FSmithUEMeshCommands::HandleMeshGenerateLods);

	Registry.Register(FSmithUEToolSchema(TEXT("mesh_set_material"), TEXT("Mesh"),
		TEXT("Assign a material to a Static Mesh or Skeletal Mesh material slot. slot = a 0-based slot index or a slot name (from read_mesh_info / read_skeletal_mesh_info). material_path is a Material or Material Instance asset. MUTATES; call save_asset."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Static or Skeletal Mesh asset path"), true),
		  FSmithUEToolParam(TEXT("slot"), TEXT("string"), TEXT("Material slot index or slot name"), true),
		  FSmithUEToolParam(TEXT("material_path"), TEXT("string"), TEXT("Material/MaterialInstance asset path"), true) }),
		&FSmithUEMeshCommands::HandleMeshSetMaterial);

	Registry.Register(FSmithUEToolSchema(TEXT("read_skeletal_mesh_info"), TEXT("Mesh"),
		TEXT("Read a Skeletal Mesh's state: material slots (name + assigned material), skeleton, physics asset, LOD count, socket count. Read-only."),
		{ FSmithUEToolParam(TEXT("mesh_path"), TEXT("string"), TEXT("Skeletal Mesh asset path"), true) }),
		&FSmithUEMeshCommands::HandleReadSkeletalMeshInfo);
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleReadMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath; Params->TryGetStringField(TEXT("mesh_path"), MeshPath);
	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static Mesh not found: '%s'"), *MeshPath)); }
	UStaticMeshEditorSubsystem* Sub = GetMeshSubsystem();
	if (!Sub) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("StaticMeshEditorSubsystem unavailable")); }

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("lod_count"), Sub->GetLodCount(Mesh));
	Data->SetNumberField(TEXT("material_count"), Sub->GetNumberMaterials(Mesh));
	Data->SetNumberField(TEXT("collision_count"), Sub->GetSimpleCollisionCount(Mesh));
	Data->SetBoolField(TEXT("nanite_enabled"), Mesh->GetNaniteSettings().bEnabled != 0);
	Data->SetNumberField(TEXT("verts_lod0"), Sub->GetNumberVerts(Mesh, 0));
	TArray<TSharedPtr<FJsonValue>> Screens;
	for (float S : Sub->GetLodScreenSizes(Mesh)) { Screens.Add(MakeShared<FJsonValueNumber>(S)); }
	Data->SetArrayField(TEXT("lod_screen_sizes"), Screens);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleMeshAddCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path"), TEXT("shape") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath, Shape; Params->TryGetStringField(TEXT("mesh_path"), MeshPath); Params->TryGetStringField(TEXT("shape"), Shape);
	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static Mesh not found: '%s'"), *MeshPath)); }
	UStaticMeshEditorSubsystem* Sub = GetMeshSubsystem();
	if (!Sub) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("StaticMeshEditorSubsystem unavailable")); }
	EScriptCollisionShapeType ShapeType;
	if (!ResolveCollisionShape(Shape, ShapeType)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown shape '%s' (box/sphere/capsule/ndop10x/ndop18/ndop26)"), *Shape)); }

	Sub->AddSimpleCollisions(Mesh, ShapeType);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetStringField(TEXT("shape"), Shape);
	Data->SetNumberField(TEXT("collision_count"), Sub->GetSimpleCollisionCount(Mesh));
	Data->SetStringField(TEXT("note"), TEXT("Collision added; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleMeshRemoveCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath; Params->TryGetStringField(TEXT("mesh_path"), MeshPath);
	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static Mesh not found: '%s'"), *MeshPath)); }
	UStaticMeshEditorSubsystem* Sub = GetMeshSubsystem();
	if (!Sub) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("StaticMeshEditorSubsystem unavailable")); }

	const int32 Before = Sub->GetSimpleCollisionCount(Mesh);
	const bool bOk = Sub->RemoveCollisions(Mesh);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetBoolField(TEXT("removed"), bOk);
	Data->SetNumberField(TEXT("removed_count"), Before);
	Data->SetNumberField(TEXT("collision_count"), Sub->GetSimpleCollisionCount(Mesh));
	Data->SetStringField(TEXT("note"), TEXT("Collision removed; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleMeshSetNanite(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path"), TEXT("enabled") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath; bool bEnabled = false;
	Params->TryGetStringField(TEXT("mesh_path"), MeshPath); Params->TryGetBoolField(TEXT("enabled"), bEnabled);
	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static Mesh not found: '%s'"), *MeshPath)); }
	UStaticMeshEditorSubsystem* Sub = GetMeshSubsystem();
	if (!Sub) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("StaticMeshEditorSubsystem unavailable")); }

	FMeshNaniteSettings Settings = Mesh->GetNaniteSettings();
	Settings.bEnabled = bEnabled ? 1 : 0;
	Sub->SetNaniteSettings(Mesh, Settings, /*bApplyChanges*/ true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetBoolField(TEXT("nanite_enabled"), Mesh->GetNaniteSettings().bEnabled != 0);
	Data->SetStringField(TEXT("note"), TEXT("Nanite setting applied + rebuilt; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleMeshGenerateLods(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path"), TEXT("lod_count") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath; int32 LodCount = 1;
	Params->TryGetStringField(TEXT("mesh_path"), MeshPath); Params->TryGetNumberField(TEXT("lod_count"), LodCount);
	LodCount = FMath::Clamp(LodCount, 1, 8);
	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Static Mesh not found: '%s'"), *MeshPath)); }
	UStaticMeshEditorSubsystem* Sub = GetMeshSubsystem();
	if (!Sub) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("StaticMeshEditorSubsystem unavailable")); }

	FStaticMeshReductionOptions Options;
	Options.bAutoComputeLODScreenSize = true;
	for (int32 i = 0; i < LodCount; ++i)
	{
		FStaticMeshReductionSettings S;
		S.PercentTriangles = FMath::Pow(0.5f, static_cast<float>(i)); // LOD0=1.0, LOD1=0.5, ...
		S.ScreenSize = FMath::Pow(0.5f, static_cast<float>(i));
		Options.ReductionSettings.Add(S);
	}
	const int32 Result = Sub->SetLods(Mesh, Options);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("requested_lods"), LodCount);
	Data->SetNumberField(TEXT("lod_count"), Sub->GetLodCount(Mesh));
	Data->SetNumberField(TEXT("result"), Result);
	Data->SetStringField(TEXT("note"), TEXT("LODs generated; call save_asset to persist."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleMeshSetMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path"), TEXT("slot"), TEXT("material_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath, Slot, MaterialPath;
	Params->TryGetStringField(TEXT("mesh_path"), MeshPath); Params->TryGetStringField(TEXT("slot"), Slot); Params->TryGetStringField(TEXT("material_path"), MaterialPath);

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: '%s'"), *MaterialPath)); }

	// Resolve the slot: numeric index or a slot name.
	auto ResolveSlotIndex = [&Slot](const auto& Materials) -> int32
	{
		if (Slot.IsNumeric()) { return FCString::Atoi(*Slot); }
		const FName SlotF(*Slot);
		for (int32 i = 0; i < Materials.Num(); ++i) { if (Materials[i].MaterialSlotName == SlotF) { return i; } }
		return INDEX_NONE;
	};

	if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *MeshPath))
	{
		const int32 Index = ResolveSlotIndex(SM->GetStaticMaterials());
		if (!SM->GetStaticMaterials().IsValidIndex(Index)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid slot '%s' (mesh has %d slots)"), *Slot, SM->GetStaticMaterials().Num())); }
		SM->Modify();
		SM->SetMaterial(Index, Material);
		SM->PostEditChange();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("mesh_path"), MeshPath);
		Data->SetStringField(TEXT("mesh_type"), TEXT("StaticMesh"));
		Data->SetNumberField(TEXT("slot_index"), Index);
		Data->SetStringField(TEXT("slot_name"), SM->GetStaticMaterials()[Index].MaterialSlotName.ToString());
		Data->SetStringField(TEXT("material"), Material->GetPathName());
		Data->SetStringField(TEXT("note"), TEXT("Material assigned; call save_asset to persist."));
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}
	if (USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
	{
		TArray<FSkeletalMaterial>& Materials = SK->GetMaterials();
		const int32 Index = ResolveSlotIndex(Materials);
		if (!Materials.IsValidIndex(Index)) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid slot '%s' (mesh has %d slots)"), *Slot, Materials.Num())); }
		SK->Modify();
		Materials[Index].MaterialInterface = Material;
		SK->PostEditChange();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("mesh_path"), MeshPath);
		Data->SetStringField(TEXT("mesh_type"), TEXT("SkeletalMesh"));
		Data->SetNumberField(TEXT("slot_index"), Index);
		Data->SetStringField(TEXT("slot_name"), Materials[Index].MaterialSlotName.ToString());
		Data->SetStringField(TEXT("material"), Material->GetPathName());
		Data->SetStringField(TEXT("note"), TEXT("Material assigned; call save_asset to persist."));
		return FSmithUECommonUtils::CreateSuccessResponse(Data);
	}
	return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No Static or Skeletal Mesh at '%s'"), *MeshPath));
}

TSharedPtr<FJsonObject> FSmithUEMeshCommands::HandleReadSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("mesh_path") }, Error)) { return FSmithUECommonUtils::CreateErrorResponse(Error); }
	FString MeshPath; Params->TryGetStringField(TEXT("mesh_path"), MeshPath);
	USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SK) { return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeletal Mesh not found: '%s'"), *MeshPath)); }

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetStringField(TEXT("skeleton"), SK->GetSkeleton() ? SK->GetSkeleton()->GetPathName() : FString());
	Data->SetStringField(TEXT("physics_asset"), SK->GetPhysicsAsset() ? SK->GetPhysicsAsset()->GetPathName() : FString());
	Data->SetNumberField(TEXT("lod_count"), SK->GetLODNum());
	Data->SetNumberField(TEXT("socket_count"), SK->NumSockets());

	const TArray<FSkeletalMaterial>& Materials = SK->GetMaterials();
	TArray<TSharedPtr<FJsonValue>> MatArr;
	for (const FSkeletalMaterial& M : Materials)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("slot_name"), M.MaterialSlotName.ToString());
		Obj->SetStringField(TEXT("material"), M.MaterialInterface ? M.MaterialInterface->GetPathName() : FString());
		MatArr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Data->SetNumberField(TEXT("material_count"), MatArr.Num());
	Data->SetArrayField(TEXT("materials"), MatArr);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

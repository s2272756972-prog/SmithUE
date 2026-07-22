// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * Static Mesh build/editing tools that go beyond simple property sets — simple
 * collision generation, Nanite enable/disable, LOD auto-generation — via
 * UStaticMeshEditorSubsystem. Requires the StaticMeshEditor module.
 */
class FSmithUEMeshCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleReadMeshInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleMeshAddCollision(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleMeshRemoveCollision(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleMeshSetNanite(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleMeshGenerateLods(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleMeshSetMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params);
};

// Copyright 2026, 123dx-svg. MIT License.

#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * Graph layout commands - universal across Material, Blueprint, Niagara, and other UEdGraph-based editors.
 */
class FSmithUEGraphCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleAutoLayoutGraph(const TSharedPtr<FJsonObject>& Params);
};

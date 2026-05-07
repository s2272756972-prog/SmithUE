// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FUEAgentToolRegistry;

class FUEAgentTextureCommands
{
public:
	static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleGenerateTexture(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleCheckGenerationTask(const TSharedPtr<FJsonObject>& Params);
};

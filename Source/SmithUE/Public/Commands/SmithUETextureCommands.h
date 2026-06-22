// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUETextureCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleGenerateTexture(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGenerateAudio(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleCheckGenerationTask(const TSharedPtr<FJsonObject>& Params);
};

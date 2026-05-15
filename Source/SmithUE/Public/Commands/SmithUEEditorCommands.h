#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEEditorCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetAllActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetViewportInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleAddPostProcessMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetProjectSetting(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleSetProjectSetting(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleAutoLayoutGraph(const TSharedPtr<FJsonObject>& Params);
};

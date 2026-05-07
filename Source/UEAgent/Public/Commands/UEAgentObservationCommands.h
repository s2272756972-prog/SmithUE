// Copyright 2026, 123dx-svg. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FUEAgentToolRegistry;

class UEAGENT_API FUEAgentObservationCommands
{
public:
	static void RegisterTools(FUEAgentToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleListPanels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleOpenPanel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleClosePanel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetEditorState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetLevelInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetActorProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleGetWorldOutline(const TSharedPtr<FJsonObject>& Params);
};

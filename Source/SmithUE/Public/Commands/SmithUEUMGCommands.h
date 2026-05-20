// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEUMGCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
	static TSharedPtr<FJsonObject> HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleReadWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleAddWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params);
};

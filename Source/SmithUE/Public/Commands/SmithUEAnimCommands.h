// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEAnimCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleAnimCreateMontage(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAnimReadMontage(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAnimAddSection(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAnimLinkSections(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAnimAddNotify(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAnimCreateBlueprint(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleAnimReadBlueprint(const TSharedPtr<FJsonObject>& Params);
};

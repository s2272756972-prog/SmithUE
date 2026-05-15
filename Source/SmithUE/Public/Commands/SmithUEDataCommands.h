// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEDataCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleDataCreateTable(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDataAddRow(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDataReadTable(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDataImportJson(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDataCreateStruct(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleDataCreateEnum(const TSharedPtr<FJsonObject>& Params);
};

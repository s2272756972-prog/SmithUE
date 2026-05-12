#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

class FSmithUEProjectCommands
{
public:
    static void RegisterTools(FSmithUEToolRegistry& Registry);

private:
    static TSharedPtr<FJsonObject> HandleGetProjectInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleListPlugins(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetProjectSettings(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleCreateFolder(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> HandleGetSourceFiles(const TSharedPtr<FJsonObject>& Params);
};

#pragma once

#include "CoreMinimal.h"

struct FUpdateInfo
{
	FString CurrentVersion;  // from .uplugin VersionName
	FString BranchName;      // current git branch
	bool bUpdateAvailable = false;
};

DECLARE_MULTICAST_DELEGATE(FOnUpdateCheckComplete);

class SMITHUE_API FSmithUEUpdateChecker
{
public:
	static void CheckForUpdate();
	static bool IsUpdateAvailable();
	static FString GetLatestVersion();
	static FString GetCurrentVersion();
	static void ExecuteUpdate();
	static void ShowUpdateNotification();

	static FOnUpdateCheckComplete OnUpdateCheckComplete;
};

// Copyright (c) 2024 SmithUE. All Rights Reserved.

#include "Utils/SmithUEUpdateChecker.h"

#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "UnrealEdMisc.h"

// Static member definition
FOnUpdateCheckComplete FSmithUEUpdateChecker::OnUpdateCheckComplete;

// Internal cached state
static FUpdateInfo GCachedUpdateInfo;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace SmithUEUpdateCheckerInternal
{
	/** Detect current git branch by running git in the plugin directory. */
	static FString DetectBranchName()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE"));
		if (!Plugin.IsValid())
		{
			return FString();
		}

		const FString PluginDir = Plugin->GetBaseDir();

		FString StdOut;
		FString StdErr;
		int32 ReturnCode = 0;

		const bool bSuccess = FPlatformProcess::ExecProcess(
			TEXT("git"),
			TEXT("rev-parse --abbrev-ref HEAD"),
			&ReturnCode,
			&StdOut,
			&StdErr,
			*PluginDir
		);

		if (bSuccess && ReturnCode == 0)
		{
			StdOut.TrimStartAndEndInline();
			return StdOut;
		}

		return FString();
	}
} // namespace SmithUEUpdateCheckerInternal

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FSmithUEUpdateChecker::CheckForUpdate()
{
	// Populate branch and current version synchronously (cheap)
	GCachedUpdateInfo.CurrentVersion = GetCurrentVersion();
	GCachedUpdateInfo.BranchName = SmithUEUpdateCheckerInternal::DetectBranchName();
	GCachedUpdateInfo.bUpdateAvailable = false;

	if (GCachedUpdateInfo.BranchName.IsEmpty())
	{
		OnUpdateCheckComplete.Broadcast();
		return;
	}

	// Get plugin dir for git commands
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE"));
	if (!Plugin.IsValid())
	{
		OnUpdateCheckComplete.Broadcast();
		return;
	}
	const FString PluginDir = Plugin->GetBaseDir();
	const FString BranchName = GCachedUpdateInfo.BranchName;

	// Run git fetch + rev-list off-thread to avoid blocking editor
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PluginDir, BranchName]()
	{
		// Fetch latest refs from remote (fast, updates tracking refs only)
		FString FetchOut, FetchErr;
		int32 FetchCode = -1;
		const FString FetchArgs = FString::Printf(TEXT("fetch origin %s"), *BranchName);
		FPlatformProcess::ExecProcess(TEXT("git"), *FetchArgs, &FetchCode, &FetchOut, &FetchErr, *PluginDir);

		if (FetchCode != 0)
		{
			// Network error or git issue — silently ignore
			AsyncTask(ENamedThreads::GameThread, []()
			{
				FSmithUEUpdateChecker::OnUpdateCheckComplete.Broadcast();
			});
			return;
		}

		// Count commits on remote that are NOT in local HEAD
		const FString RevListArgs = FString::Printf(TEXT("rev-list --count HEAD..origin/%s"), *BranchName);
		FString CountOut, CountErr;
		int32 CountCode = -1;
		FPlatformProcess::ExecProcess(TEXT("git"), *RevListArgs, &CountCode, &CountOut, &CountErr, *PluginDir);

		int32 IncomingCommits = 0;
		if (CountCode == 0)
		{
			IncomingCommits = FCString::Atoi(*CountOut.TrimStartAndEnd());
		}

		// Back to game thread to update state and broadcast
		AsyncTask(ENamedThreads::GameThread, [IncomingCommits]()
		{
			GCachedUpdateInfo.bUpdateAvailable = (IncomingCommits > 0);
			FSmithUEUpdateChecker::OnUpdateCheckComplete.Broadcast();
		});
	});
}

bool FSmithUEUpdateChecker::IsUpdateAvailable()
{
	return GCachedUpdateInfo.bUpdateAvailable;
}

FString FSmithUEUpdateChecker::GetLatestVersion()
{
	return FString();
}

FString FSmithUEUpdateChecker::GetCurrentVersion()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE"));
	if (Plugin.IsValid())
	{
		const FString VersionName = Plugin->GetDescriptor().VersionName;
		if (!VersionName.IsEmpty())
		{
			return VersionName;
		}
	}
	return TEXT("?");
}

void FSmithUEUpdateChecker::ExecuteUpdate()
{
	// Helper lambda to run a git command in the plugin directory
	auto RunGit = [](const FString& PluginDir, const FString& Args, FString& OutStdOut, FString& OutStdErr) -> int32
	{
		int32 ReturnCode = -1;
		FPlatformProcess::ExecProcess(TEXT("git"), *Args, &ReturnCode, &OutStdOut, &OutStdErr, *PluginDir);
		return ReturnCode;
	};

	auto ShowErrorToast = [](const FString& Message)
	{
		FNotificationInfo ErrInfo(FText::FromString(Message));
		ErrInfo.bFireAndForget = true;
		ErrInfo.ExpireDuration = 8.0f;
		FSlateNotificationManager::Get().AddNotification(ErrInfo);
	};

	// 1. Check git availability
	{
		int32 ReturnCode = -1;
		FString StdOut, StdErr;
		FPlatformProcess::ExecProcess(TEXT("git"), TEXT("--version"), &ReturnCode, &StdOut, &StdErr, nullptr);
		if (ReturnCode != 0)
		{
			ShowErrorToast(TEXT("Update failed: git not found. Please install Git and ensure it's on PATH."));
			return;
		}
	}

	// 2. Get plugin base directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE"));
	if (!Plugin.IsValid())
	{
		ShowErrorToast(TEXT("Update failed. Please run 'git pull' manually in the plugin directory."));
		return;
	}
	const FString PluginDir = Plugin->GetBaseDir();

	// 3. Detect current branch
	FString BranchStdOut, BranchStdErr;
	const int32 BranchCode = RunGit(PluginDir, TEXT("rev-parse --abbrev-ref HEAD"), BranchStdOut, BranchStdErr);
	FString Branch = BranchStdOut.TrimStartAndEnd();
	if (BranchCode != 0 || Branch.IsEmpty())
	{
		Branch = TEXT("main");
	}

	// 4. Check dirty working tree
	FString StatusStdOut, StatusStdErr;
	RunGit(PluginDir, TEXT("status --porcelain"), StatusStdOut, StatusStdErr);
	const bool bDirty = !StatusStdOut.TrimStartAndEnd().IsEmpty();

	// 5. Stash if dirty
	bool bStashed = false;
	if (bDirty)
	{
		FString StashOut, StashErr;
		const int32 StashCode = RunGit(PluginDir, TEXT("stash"), StashOut, StashErr);
		bStashed = (StashCode == 0);
	}

	// 6. Run git pull
	const FString PullArgs = FString::Printf(TEXT("pull origin %s"), *Branch);
	FString PullOut, PullErr;
	const int32 PullCode = RunGit(PluginDir, PullArgs, PullOut, PullErr);

	if (PullCode == 0)
	{
		// 7. Build plugin after pull (source code may have changed)
		const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
		const FString BuildBat = EngineDir / TEXT("Build/BatchFiles/Build.bat");
		const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		const FString ProjectName = FPaths::GetBaseFilename(ProjectPath);

		const FString BuildArgs = FString::Printf(
			TEXT("%sEditor Win64 Development -Project=\"%s\""),
			*ProjectName, *ProjectPath
		);

		FString BuildOut, BuildErr;
		int32 BuildCode = -1;
		FPlatformProcess::ExecProcess(*BuildBat, *BuildArgs, &BuildCode, &BuildOut, &BuildErr, nullptr);

		if (BuildCode == 0)
		{
			// 8. Build succeeded — restart editor
			FUnrealEdMisc::Get().RestartEditor(false);
		}
		else
		{
			ShowErrorToast(TEXT("Update pulled but build failed. Please rebuild manually."));
		}
	}
	else
	{
		// 8. Failure — pop stash if we stashed, then show error
		if (bStashed)
		{
			FString PopOut, PopErr;
			RunGit(PluginDir, TEXT("stash pop"), PopOut, PopErr);
		}
		ShowErrorToast(TEXT("Update failed. Please run 'git pull' manually in the plugin directory."));
	}
}

void FSmithUEUpdateChecker::ShowUpdateNotification()
{
	// Build main text
	const FString MainText = FString::Printf(
		TEXT("SmithUE has new updates (branch: %s)"),
		*GCachedUpdateInfo.BranchName
	);

	// Build SubText with current version + restart reminder
	FString SubTextStr = FString::Printf(TEXT("Current: v%s\n"), *GCachedUpdateInfo.CurrentVersion);
	SubTextStr += TEXT("• Restart connected SmithUE clients after updating\n");

	FNotificationInfo Info(FText::FromString(MainText));
	Info.SubText = FText::FromString(SubTextStr);
	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.0f;
	Info.bUseLargeFont = false;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = false;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Restart to Update")),
		FText::FromString(TEXT("Pull latest changes and restart editor")),
		FSimpleDelegate::CreateStatic(&FSmithUEUpdateChecker::ExecuteUpdate),
		SNotificationItem::CS_None
	));
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Dismiss")),
		FText::FromString(TEXT("Dismiss this notification")),
		FSimpleDelegate::CreateLambda([]() {}),
		SNotificationItem::CS_None
	));

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

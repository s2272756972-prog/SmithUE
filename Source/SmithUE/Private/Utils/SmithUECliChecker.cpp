// Copyright 2026, 123dx-svg. MIT License.

#include "Utils/SmithUECliChecker.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "SmithUEModule.h"

// ---------------------------------------------------------------------------
// Module-level constants and state (game-thread-confined writes, no lock)
// ---------------------------------------------------------------------------

static constexpr TCHAR kRecommendedCliVersion[] = TEXT("0.13.0");

/** Last known CLI environment state. Written only on the game thread. */
static FCliInfo GCachedCliInfo;

/** Set to true while an async check is in-flight; cleared on game thread when done. */
static FThreadSafeBool bCliCheckInFlight;

// Static member definition
FOnCliCheckComplete FSmithUECliChecker::OnCliCheckComplete;

// ---------------------------------------------------------------------------
// SmithUECliInternal — pure, testable helpers
// ---------------------------------------------------------------------------

namespace SmithUECliInternal
{

TOptional<FSmithUESemver> ParseSemver(const FString& s)
{
    if (s.IsEmpty())
    {
        return {};
    }

    FString Str = s.TrimStartAndEnd();

    // Strip leading 'v' or 'V'
    if (!Str.IsEmpty() && (Str[0] == TEXT('v') || Str[0] == TEXT('V')))
    {
        Str = Str.Mid(1);
    }

    // Strip build metadata — everything from the first '+' onwards
    int32 PlusIdx = INDEX_NONE;
    if (Str.FindChar(TEXT('+'), PlusIdx))
    {
        Str = Str.Left(PlusIdx);
    }

    // Split off prerelease — everything from the first '-' onwards
    FString Core;
    FString Prerelease;
    int32 DashIdx = INDEX_NONE;
    if (Str.FindChar(TEXT('-'), DashIdx))
    {
        Core       = Str.Left(DashIdx);
        Prerelease = Str.Mid(DashIdx + 1);
    }
    else
    {
        Core = Str;
    }

    // Parse major.minor.patch
    TArray<FString> Parts;
    Core.ParseIntoArray(Parts, TEXT("."), /*bCullEmpty=*/false);
    if (Parts.Num() != 3)
    {
        return {};
    }

    for (const FString& Part : Parts)
    {
        if (Part.IsEmpty())
        {
            return {};
        }
        for (TCHAR Ch : Part)
        {
            if (!FChar::IsDigit(Ch))
            {
                return {};
            }
        }
    }

    FSmithUESemver Result;
    Result.Major      = FCString::Atoi(*Parts[0]);
    Result.Minor      = FCString::Atoi(*Parts[1]);
    Result.Patch      = FCString::Atoi(*Parts[2]);
    Result.Prerelease = MoveTemp(Prerelease);
    return Result;
}

int32 CompareSemver(const FSmithUESemver& a, const FSmithUESemver& b)
{
    if (a.Major != b.Major) { return a.Major < b.Major ? -1 : 1; }
    if (a.Minor != b.Minor) { return a.Minor < b.Minor ? -1 : 1; }
    if (a.Patch != b.Patch) { return a.Patch < b.Patch ? -1 : 1; }

    // Prerelease ordering per semver §11:
    //   a release version (empty prerelease) is HIGHER than any prerelease
    const bool aIsRelease = a.Prerelease.IsEmpty();
    const bool bIsRelease = b.Prerelease.IsEmpty();

    if (aIsRelease && bIsRelease) { return 0; }
    if (aIsRelease)               { return 1;  } // release > prerelease
    if (bIsRelease)               { return -1; } // prerelease < release

    // Both have prerelease — lexicographic comparison
    const int32 Cmp = a.Prerelease.Compare(b.Prerelease, ESearchCase::CaseSensitive);
    if (Cmp < 0) return -1;
    if (Cmp > 0) return 1;
    return 0;
}

TOptional<FString> ClassifyVersionProbe(int32 rc, const FString& out)
{
    if (rc != 0)
    {
        return {};
    }

    FString Trimmed = out.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return {};
    }

    // Must parse as valid semver to be accepted
    if (!ParseSemver(Trimmed).IsSet())
    {
        return {};
    }

    // Return trimmed, stripping any leading 'v'
    if (!Trimmed.IsEmpty() && (Trimmed[0] == TEXT('v') || Trimmed[0] == TEXT('V')))
    {
        Trimmed = Trimmed.Mid(1);
    }

    // Also strip build metadata from the returned string
    int32 PlusIdx = INDEX_NONE;
    if (Trimmed.FindChar(TEXT('+'), PlusIdx))
    {
        Trimmed = Trimmed.Left(PlusIdx);
    }

    return Trimmed;
}

TOptional<FString> ParseNpmLsCliVersion(const FString& json)
{
    if (json.IsEmpty())
    {
        return {};
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return {};
    }

    const TSharedPtr<FJsonObject>* DepsObjPtr = nullptr;
    if (!Root->TryGetObjectField(TEXT("dependencies"), DepsObjPtr)
        || DepsObjPtr == nullptr
        || !(*DepsObjPtr).IsValid())
    {
        return {};
    }
    const TSharedPtr<FJsonObject>& DepsObj = *DepsObjPtr;

    const TSharedPtr<FJsonObject>* CliObjPtr = nullptr;
    if (!DepsObj->TryGetObjectField(TEXT("smithue-cli"), CliObjPtr)
        || CliObjPtr == nullptr
        || !(*CliObjPtr).IsValid())
    {
        return {};
    }
    const TSharedPtr<FJsonObject>& CliObj = *CliObjPtr;

    FString Version;
    if (!CliObj->TryGetStringField(TEXT("version"), Version) || Version.IsEmpty())
    {
        return {};
    }

    return Version;
}

} // namespace SmithUECliInternal

// ---------------------------------------------------------------------------
// Internal helpers (not exported)
// ---------------------------------------------------------------------------

namespace
{

/**
 * Run an executable directly via FPlatformProcess::ExecProcess (like the git
 * pattern in UpdateChecker). Stdout/stderr are captured in OutStdOut/OutStdErr.
 *
 * WHY NOT cmd.exe /c:
 *   UE ExecProcess uses DETACHED_PROCESS which breaks pipe inheritance for
 *   cmd.exe child processes — node/npm inside cmd.exe never write to the pipe,
 *   producing rc=0 but empty stdout. Calling node.exe directly (a real PE
 *   executable) works identically to how git.exe is called in UpdateChecker.
 *
 * WHY node.exe for npm:
 *   npm.cmd is a batch file — ExecProcess cannot launch .cmd files directly.
 *   The solution is to run node.exe with the npm-cli.js script path, which is
 *   the same mechanism npm.cmd uses internally.
 */
int32 RunDirect(const FString& Exe, const FString& Args, FString& OutStdOut, FString& OutStdErr)
{
    int32 rc = -1;
    FPlatformProcess::ExecProcess(*Exe, *Args, &rc, &OutStdOut, &OutStdErr);
    return rc;
}

/**
 * Locate npm-cli.js relative to a node.exe path.
 * npm ships as node_modules/npm/bin/npm-cli.js inside the nodejs install dir.
 * Returns empty string if not found.
 */
FString FindNpmCliJs()
{
    // Search common install locations — %ProgramFiles%\nodejs is most common
    TArray<FString> Candidates;
    FString PF = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles"));
    if (!PF.IsEmpty())
    {
        Candidates.Add(PF / TEXT("nodejs") / TEXT("node_modules") / TEXT("npm") / TEXT("bin") / TEXT("npm-cli.js"));
    }
    // %APPDATA%\npm\node_modules\npm\bin\npm-cli.js (user-global installs)
    FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
    if (!AppData.IsEmpty())
    {
        Candidates.Add(AppData / TEXT("npm") / TEXT("node_modules") / TEXT("npm") / TEXT("bin") / TEXT("npm-cli.js"));
    }
    // Locate via PATH: try resolving node.exe sibling
    {
        FString NodeOut, NodeErr;
        int32 Rc = -1;
        // Use where.exe to find node.exe (where.exe IS a real exe)
        FPlatformProcess::ExecProcess(TEXT("where.exe"), TEXT("node.exe"), &Rc, &NodeOut, &NodeErr);
        if (Rc == 0 && !NodeOut.IsEmpty())
        {
            FString NodePath = NodeOut.TrimStartAndEnd();
            // Handle multiple results — take first line
            int32 NewLine = -1;
            if (NodePath.FindChar(TEXT('\n'), NewLine)) NodePath = NodePath.Left(NewLine).TrimStartAndEnd();
            FString NodeDir = FPaths::GetPath(NodePath);
            Candidates.Add(NodeDir / TEXT("node_modules") / TEXT("npm") / TEXT("bin") / TEXT("npm-cli.js"));
        }
    }
    for (const FString& C : Candidates)
    {
        if (IFileManager::Get().FileExists(*C))
        {
            return C;
        }
    }
    return FString();
}

/** Show a fire-and-forget toast notification. Must be called on the game thread. */
void ShowToast(const FString& Message, float Duration = 6.0f)
{
    FNotificationInfo Info(FText::FromString(Message));
    Info.bFireAndForget  = true;
    Info.ExpireDuration  = Duration;
    FSlateNotificationManager::Get().AddNotification(Info);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FSmithUECliChecker::CheckCliEnvironment()
{
    // AtomicSet returns the PREVIOUS value:
    //   true  → already in-flight, bail out
    //   false → we just acquired the "lock", proceed
    if (bCliCheckInFlight.AtomicSet(true))
    {
        return;
    }

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, []()
    {
        using namespace SmithUECliInternal;

        FCliInfo Info;
        Info.LastCheckTime = FDateTime::UtcNow();

        // 1. Probe node — call node.exe directly (real PE exe, like git.exe in UpdateChecker)
        FString NodeOut, NodeErr;
        const int32 NodeRc = RunDirect(TEXT("node.exe"), TEXT("--version"), NodeOut, NodeErr);
        UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] node probe: rc=%d out='%s' err='%s'"),
               NodeRc, *NodeOut.TrimStartAndEnd(), *NodeErr.TrimStartAndEnd());

        const TOptional<FString> NodeVer = ClassifyVersionProbe(NodeRc, NodeOut);
        if (!NodeVer.IsSet())
        {
            // node not found — report and exit early
            Info.State = ECliState::NoNode;
            Info.bValid = true;

            AsyncTask(ENamedThreads::GameThread, [Info]()
            {
                GCachedCliInfo = Info;
                FSmithUECliChecker::OnCliCheckComplete.Broadcast();
                bCliCheckInFlight = false;
            });
            return;
        }

        Info.NodeVersion = NodeVer.GetValue();

        // 2. Probe npm — use node.exe + npm-cli.js (avoids cmd.exe DETACHED_PROCESS pipe issue)
        FString NpmCliJs = FindNpmCliJs();
        FString NpmOut, NpmErr;
        int32 NpmRc = -1;
        if (NpmCliJs.IsEmpty())
        {
            UE_LOG(LogSmithUE, Warning, TEXT("[SmithUE CLI] npm probe: npm-cli.js not found on PATH"));
        }
        else
        {
            NpmRc = RunDirect(TEXT("node.exe"),
                FString::Printf(TEXT("\"%s\" --version"), *NpmCliJs),
                NpmOut, NpmErr);
            UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] npm probe: rc=%d out='%s' err='%s'"),
                   NpmRc, *NpmOut.TrimStartAndEnd(), *NpmErr.TrimStartAndEnd());
        }

        const TOptional<FString> NpmVer = ClassifyVersionProbe(NpmRc, NpmOut);
        if (NpmVer.IsSet())
        {
            Info.NpmVersion = NpmVer.GetValue();
        }

        // 3. Probe smithue-cli — use node.exe + npm-cli.js (IGNORE rc — parse JSON only)
        FString LsOut, LsErr;
        int32 LsRc = -1;
        if (!NpmCliJs.IsEmpty())
        {
            LsRc = RunDirect(TEXT("node.exe"),
                FString::Printf(TEXT("\"%s\" ls -g --depth=0 smithue-cli --json"), *NpmCliJs),
                LsOut, LsErr);
            UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] npm ls probe: rc=%d out='%s' err='%s'"),
                   LsRc, *LsOut.TrimStartAndEnd(), *LsErr.TrimStartAndEnd());
        }

        const TOptional<FString> CliVer = ParseNpmLsCliVersion(LsOut);

        // ----------------------------------------------------------------
        // 4. Compute state
        // ----------------------------------------------------------------
        if (!CliVer.IsSet())
        {
            Info.State = ECliState::NotInstalled;
        }
        else
        {
            Info.CliVersion = CliVer.GetValue();

            const TOptional<FSmithUESemver> Installed   = ParseSemver(Info.CliVersion);
            const TOptional<FSmithUESemver> Recommended = ParseSemver(FString(kRecommendedCliVersion));

            if (Installed.IsSet() && Recommended.IsSet()
                && CompareSemver(Installed.GetValue(), Recommended.GetValue()) < 0)
            {
                Info.State = ECliState::Outdated;
            }
            else
            {
                Info.State = ECliState::Ready;
            }
        }
        Info.bValid = true;

        // ----------------------------------------------------------------
        // 5. Marshal result back to the game thread
        // ----------------------------------------------------------------
        AsyncTask(ENamedThreads::GameThread, [Info]()
        {
            GCachedCliInfo = Info;
            FSmithUECliChecker::OnCliCheckComplete.Broadcast();
            bCliCheckInFlight = false;
        });
    });
}

void FSmithUECliChecker::ExecuteCliInstall()
{
    if (bCliCheckInFlight.AtomicSet(true))
    {
        return;
    }

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, []()
    {
        // Install via node.exe + npm-cli.js (avoids cmd.exe DETACHED_PROCESS pipe issue)
        const FString NpmCli = FindNpmCliJs();
        FString InstallOut, InstallErr;
        int32 rc = -1;
        if (NpmCli.IsEmpty())
        {
            InstallErr = TEXT("npm-cli.js not found — is Node.js installed?");
        }
        else
        {
            rc = RunDirect(TEXT("node.exe"),
                FString::Printf(TEXT("\"%s\" i -g smithue-cli@latest"), *NpmCli),
                InstallOut, InstallErr);
        }
        UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] install: rc=%d out='%s' err='%s'"),
               rc, *InstallOut.TrimStartAndEnd(), *InstallErr.TrimStartAndEnd());

        if (rc == 0)
        {
            // Clear the flag before re-triggering a fresh environment check
            bCliCheckInFlight = false;
            FSmithUECliChecker::CheckCliEnvironment();
        }
        else
        {
            // Classify the failure from combined stdout + stderr
            const FString Combined = InstallOut + InstallErr;

            FString ToastMsg;
            if (Combined.Contains(TEXT("EACCES"))
                || Combined.Contains(TEXT("permission"), ESearchCase::IgnoreCase))
            {
                ToastMsg = TEXT("smithue-cli install failed: permission denied. "
                                "Try running your terminal as administrator or use a Node version manager (nvm, fnm).");
            }
            else if (Combined.Contains(TEXT("ECONNREFUSED"))
                     || Combined.Contains(TEXT("fetch failed"), ESearchCase::IgnoreCase)
                     || Combined.Contains(TEXT("network"), ESearchCase::IgnoreCase))
            {
                ToastMsg = TEXT("smithue-cli install failed: network error. "
                                "Check your internet connection and npm registry configuration.");
            }
            else
            {
                ToastMsg = TEXT("smithue-cli install failed: npm registry error. "
                                "Run 'npm i -g smithue-cli@latest' manually for details.");
            }

            AsyncTask(ENamedThreads::GameThread, [ToastMsg]()
            {
                ShowToast(ToastMsg);
                bCliCheckInFlight = false;
            });
        }
    });
}

ECliState FSmithUECliChecker::GetState()
{
    return GCachedCliInfo.State;
}

FString FSmithUECliChecker::GetNodeVersion()
{
    return GCachedCliInfo.NodeVersion;
}

FString FSmithUECliChecker::GetNpmVersion()
{
    return GCachedCliInfo.NpmVersion;
}

FString FSmithUECliChecker::GetCliVersion()
{
    return GCachedCliInfo.CliVersion;
}

FDateTime FSmithUECliChecker::GetLastCheckTime()
{
    return GCachedCliInfo.LastCheckTime;
}

bool FSmithUECliChecker::IsCheckInFlight()
{
    return bCliCheckInFlight;
}

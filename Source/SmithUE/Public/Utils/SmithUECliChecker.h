// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"

// ---------------------------------------------------------------------------
// Pure helpers — exposed in a named namespace so Automation tests can reach them
// without a FRIEND_TEST hack.
// ---------------------------------------------------------------------------
namespace SmithUECliInternal
{
    /** Parsed representation of a semantic version string. */
    struct FSmithUESemver
    {
        int32   Major      = 0;
        int32   Minor      = 0;
        int32   Patch      = 0;
        FString Prerelease; // empty = release version
    };

    /**
     * Parse a semver string.
     * - Leading 'v'/'V' is stripped.
     * - Build metadata (everything after '+') is ignored.
     * - Returns empty TOptional on any parse failure.
     */
    SMITHUE_API TOptional<FSmithUESemver> ParseSemver(const FString& s);

    /**
     * Three-way comparison.
     * Returns -1 (a < b), 0 (a == b), +1 (a > b).
     * Prerelease is lower than the equivalent release per semver spec §11.
     */
    SMITHUE_API int32 CompareSemver(const FSmithUESemver& a, const FSmithUESemver& b);

    /**
     * Classify the output of a version probe command (e.g. "node --version").
     * Returns the trimmed version string (leading 'v' stripped) if rc==0 and
     * stdout parses as a valid semver; returns empty TOptional otherwise.
     */
    SMITHUE_API TOptional<FString> ClassifyVersionProbe(int32 rc, const FString& out);

    /**
     * Parse the JSON output of "npm ls -g --depth=0 smithue-cli --json".
     * Extracts dependencies["smithue-cli"].version.
     * Return code from npm is intentionally ignored — JSON is the source of truth.
     */
    SMITHUE_API TOptional<FString> ParseNpmLsCliVersion(const FString& json);

} // namespace SmithUECliInternal

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

DECLARE_MULTICAST_DELEGATE(FOnCliCheckComplete);

/** Overall CLI environment health. */
enum class ECliState : uint8
{
    NoNode,       // node.exe not found on PATH
    NotInstalled, // node found, but smithue-cli not globally installed
    Outdated,     // smithue-cli installed but below the recommended floor
    Ready         // smithue-cli installed and meets the minimum version
};

/** Snapshot of the last successful environment probe. */
struct FCliInfo
{
    FString   NodeVersion;
    FString   NpmVersion;
    FString   CliVersion;
    ECliState State         = ECliState::NoNode;
    FDateTime LastCheckTime;
    bool      bValid        = false;
};

// ---------------------------------------------------------------------------
// Main class
// ---------------------------------------------------------------------------

class SMITHUE_API FSmithUECliChecker
{
public:
    /** Detect node / npm / smithue-cli environment asynchronously. Re-entrancy guarded. */
    static void CheckCliEnvironment();

    /**
     * Install or upgrade smithue-cli globally via npm.
     * ONLY call this on an explicit user action (button press).
     */
    static void ExecuteCliInstall();

    static ECliState GetState();
    static FString   GetNodeVersion();
    static FString   GetNpmVersion();
    static FString   GetCliVersion();
    static FDateTime GetLastCheckTime();
    static bool      IsCheckInFlight();

    static FOnCliCheckComplete OnCliCheckComplete;
};

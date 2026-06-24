// Copyright 2026, 123dx-svg. MIT License.
#include "Commands/SmithUELiveCodingCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_LIVE_CODING
#include "Modules/ModuleManager.h"
// ILiveCodingModule.h already included via the header
#endif

#if WITH_LIVE_CODING
static ILiveCodingModule* GetLC()
{
    return FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
}
#endif

// -------------------------------------------------------------------------
// BuildUnsupportedStatusResponse — always compiled, no engine types
// -------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUELiveCodingCommands::BuildUnsupportedStatusResponse()
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("with_live_coding"), false);
    Data->SetBoolField(TEXT("module_loaded"), false);
    Data->SetBoolField(TEXT("supported"), false);
    Data->SetBoolField(TEXT("can_enable"), false);
    Data->SetBoolField(TEXT("enabled_for_session"), false);
    Data->SetBoolField(TEXT("enabled_by_default"), false);
    Data->SetBoolField(TEXT("has_started"), false);
    Data->SetBoolField(TEXT("is_compiling"), false);
    Data->SetBoolField(TEXT("automatically_compile_new_classes"), false);
    Data->SetStringField(TEXT("enable_error_text"), TEXT(""));
    Data->SetStringField(TEXT("message"), TEXT("Live Coding not supported (compiled out / non-Windows / module not loaded)."));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// -------------------------------------------------------------------------
// RegisterTools
// -------------------------------------------------------------------------
void FSmithUELiveCodingCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("livecoding_status"),
            TEXT("LiveCoding"),
            TEXT("Report Unreal Live Coding support and state (read-only). Safe to call during PIE."),
            {}),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleLiveCodingStatus(Params); });


    Registry.Register(
        FSmithUEToolSchema(
            TEXT("livecoding_compile"),
            TEXT("LiveCoding"),
            TEXT("Trigger an Unreal Live Coding C++ hot-compile and report the result. Blocks until the compile completes. NOTE: the C++ compiler error text is NOT returned by Unreal - on failure use show_console or read the editor log. New UCLASS/UPROPERTY/UFUNCTION/header changes cannot hot-patch and need a full editor rebuild."),
            {
                FSmithUEToolParam(TEXT("show_console"), TEXT("bool"), TEXT("Explicitly pre-open the Live Coding console window (default false). Note: UE may show the console on its own during compile regardless, so false does not guarantee it stays hidden."))
            }),
        [](const TSharedPtr<FJsonObject>& Params) { return HandleLiveCodingCompile(Params); });
}

// -------------------------------------------------------------------------
// HandleLiveCodingStatus
// -------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUELiveCodingCommands::HandleLiveCodingStatus(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_LIVE_CODING
    ILiveCodingModule* LC = GetLC();
    if (LC != nullptr)
    {
        const bool bCanEnable = LC->CanEnableForSession();
        FString EnableErrorText = LC->GetEnableErrorText().ToString();
        if (EnableErrorText.IsEmpty() && !bCanEnable)
        {
            EnableErrorText = TEXT("Live Coding cannot be enabled (commonly: hot-reloaded modules active) - close the editor, rebuild from your IDE, and restart.");
        }

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("with_live_coding"), true);
        Data->SetBoolField(TEXT("module_loaded"), true);
        Data->SetBoolField(TEXT("supported"), true);
        Data->SetBoolField(TEXT("can_enable"), bCanEnable);
        Data->SetBoolField(TEXT("enabled_for_session"), LC->IsEnabledForSession());
        Data->SetBoolField(TEXT("enabled_by_default"), LC->IsEnabledByDefault());
        Data->SetBoolField(TEXT("has_started"), LC->HasStarted());
        Data->SetBoolField(TEXT("is_compiling"), LC->IsCompiling());
        Data->SetBoolField(TEXT("automatically_compile_new_classes"), LC->AutomaticallyCompileNewClasses());
        Data->SetStringField(TEXT("enable_error_text"), EnableErrorText);
        return FSmithUECommonUtils::CreateSuccessResponse(Data);
    }
#endif
    return BuildUnsupportedStatusResponse();
}

// -------------------------------------------------------------------------
// ReadBoolParam — always compiled, no engine types
// -------------------------------------------------------------------------
bool FSmithUELiveCodingCommands::ReadBoolParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, bool Default)
{
    bool b;
    return Params.IsValid() && Params->TryGetBoolField(Field, b) ? b : Default;
}

// -------------------------------------------------------------------------
// BuildUnsupportedCompileResponse — always compiled, no engine types
// -------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUELiveCodingCommands::BuildUnsupportedCompileResponse()
{
    return FSmithUECommonUtils::CreateErrorResponse(
        TEXT("Live Coding not supported (module not loaded / non-Windows build)."),
        TEXT("LIVECODING_UNSUPPORTED"));
}

// -------------------------------------------------------------------------
// MapCompileResult — under #if WITH_LIVE_CODING, pure/testable seam
// -------------------------------------------------------------------------
#if WITH_LIVE_CODING
TSharedPtr<FJsonObject> FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult Result, ILiveCodingModule* LC)
{
    static const FString ReflectionWarning = TEXT("NOTE: new UCLASS/UPROPERTY/UFUNCTION/header changes cannot hot-patch - a full editor rebuild is required for those.");

    switch (Result)
    {
        case ELiveCodingCompileResult::Success:
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("result"), TEXT("success"));
            Data->SetBoolField(TEXT("changed"), true);
            Data->SetStringField(TEXT("reflection_warning"), ReflectionWarning);
            return FSmithUECommonUtils::CreateSuccessResponse(Data);
        }
        case ELiveCodingCompileResult::NoChanges:
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("result"), TEXT("no_changes"));
            Data->SetBoolField(TEXT("changed"), false);
            return FSmithUECommonUtils::CreateSuccessResponse(Data);
        }
        case ELiveCodingCompileResult::InProgress:
        {
            // Defensive only: WaitForCompletion should not produce this
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("result"), TEXT("in_progress"));
            Data->SetStringField(TEXT("message"), TEXT("compile started"));
            return FSmithUECommonUtils::CreateSuccessResponse(Data);
        }
        case ELiveCodingCompileResult::NotStarted:
        {
            FString Err = (LC != nullptr) ? LC->GetEnableErrorText().ToString() : TEXT("");
            if (Err.IsEmpty())
            {
                Err = TEXT("Live Coding is enabled but not started - close the editor, rebuild from your IDE, and restart.");
            }
            return FSmithUECommonUtils::CreateErrorResponse(Err, TEXT("LIVECODING_NOT_STARTED"));
        }
        case ELiveCodingCompileResult::CompileStillActive:
            return FSmithUECommonUtils::CreateErrorResponse(
                TEXT("A compile is already running."),
                TEXT("LIVECODING_BUSY"));
        case ELiveCodingCompileResult::Failure:
            return FSmithUECommonUtils::CreateErrorResponse(
                TEXT("Live Coding compile failed - open the Live Coding console / editor log (LogLiveCoding) for details."),
                TEXT("LIVECODING_COMPILE_FAILED"));
        case ELiveCodingCompileResult::Cancelled:
            return FSmithUECommonUtils::CreateErrorResponse(
                TEXT("Live Coding compile was cancelled."),
                TEXT("LIVECODING_CANCELLED"));
        default:
            return FSmithUECommonUtils::CreateErrorResponse(
                TEXT("Unknown Live Coding compile result."),
                TEXT("LIVECODING_UNKNOWN"));
    }
}
#endif

// -------------------------------------------------------------------------
// HandleLiveCodingCompile
// -------------------------------------------------------------------------
TSharedPtr<FJsonObject> FSmithUELiveCodingCommands::HandleLiveCodingCompile(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_LIVE_CODING
    ILiveCodingModule* LC = GetLC();
    if (!LC)
    {
        return BuildUnsupportedCompileResponse();
    }

    const bool bShowConsole = ReadBoolParam(Params, TEXT("show_console"), false);
    if (bShowConsole)
    {
        LC->ShowConsole();
    }

    ELiveCodingCompileResult Result = ELiveCodingCompileResult::Failure;
    LC->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);

    return MapCompileResult(Result, LC);
#else
    return BuildUnsupportedCompileResponse();
#endif
}

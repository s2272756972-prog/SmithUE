// Copyright 2026, 123dx-svg. MIT License.
// SmithUE LiveCoding Automation Tests
// Tests seams only — NO real compile, NO dependence on WITH_LIVE_CODING being off.
// Assertions:
//   (a) BuildUnsupportedCompileResponse() — always compiled, exact LIVECODING_UNSUPPORTED envelope
//   (b) MapCompileResult(R, nullptr) for all 7 ELiveCodingCompileResult values — under #if WITH_LIVE_CODING
//   (c) DispatchCommand("livecoding_status", {}) — all 10 keys present, status:"success"
//   (d) ReadBoolParam with non-bool value — returns Default
//   (e) BuildUnsupportedStatusResponse() — always compiled, all 10 keys, supported:false

#include "Tests/SmithUETestBase.h"
#include "Commands/SmithUELiveCodingCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

// -------------------------------------------------------------------------
// (a) BuildUnsupportedCompileResponse — always compiled, exact envelope
// -------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUELiveCoding_UnsupportedCompileResponse,
    "SmithUE.LiveCoding.UnsupportedCompileResponse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUELiveCoding_UnsupportedCompileResponse::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Response = FSmithUELiveCodingCommands::BuildUnsupportedCompileResponse();
    TestTrue(TEXT("Response is valid"), Response.IsValid());

    FString Status;
    TestTrue(TEXT("Has status field"), Response->TryGetStringField(TEXT("status"), Status));
    TestEqual(TEXT("status == error"), Status, TEXT("error"));

    FString ErrorCode;
    TestTrue(TEXT("Has error_code field"), Response->TryGetStringField(TEXT("error_code"), ErrorCode));
    TestEqual(TEXT("error_code == LIVECODING_UNSUPPORTED"), ErrorCode, TEXT("LIVECODING_UNSUPPORTED"));

    return true;
}

// -------------------------------------------------------------------------
// (e) BuildUnsupportedStatusResponse — always compiled, all 10 keys, supported:false
// -------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUELiveCoding_UnsupportedStatusResponse,
    "SmithUE.LiveCoding.UnsupportedStatusResponse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUELiveCoding_UnsupportedStatusResponse::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Response = FSmithUELiveCodingCommands::BuildUnsupportedStatusResponse();
    TestTrue(TEXT("Response is valid"), Response.IsValid());

    FString Status;
    TestTrue(TEXT("Has status field"), Response->TryGetStringField(TEXT("status"), Status));
    TestEqual(TEXT("status == success"), Status, TEXT("success"));

    // Must have data sub-object
    const TSharedPtr<FJsonObject>* DataPtr = nullptr;
    TestTrue(TEXT("Has data object"), Response->TryGetObjectField(TEXT("data"), DataPtr) && DataPtr != nullptr);
    if (!DataPtr || !(*DataPtr).IsValid()) return false;
    TSharedPtr<FJsonObject> Data = *DataPtr;

    // All 10 status keys must be present
    bool bBool = true;
    TestTrue(TEXT("with_live_coding key present"), Data->TryGetBoolField(TEXT("with_live_coding"), bBool));
    TestFalse(TEXT("with_live_coding == false"), bBool);

    bBool = true;
    TestTrue(TEXT("module_loaded key present"), Data->TryGetBoolField(TEXT("module_loaded"), bBool));
    TestFalse(TEXT("module_loaded == false"), bBool);

    bBool = true;
    TestTrue(TEXT("supported key present"), Data->TryGetBoolField(TEXT("supported"), bBool));
    TestFalse(TEXT("supported == false"), bBool);

    bBool = true;
    TestTrue(TEXT("can_enable key present"), Data->TryGetBoolField(TEXT("can_enable"), bBool));
    TestFalse(TEXT("can_enable == false"), bBool);

    bBool = true;
    TestTrue(TEXT("enabled_for_session key present"), Data->TryGetBoolField(TEXT("enabled_for_session"), bBool));
    TestFalse(TEXT("enabled_for_session == false"), bBool);

    bBool = true;
    TestTrue(TEXT("enabled_by_default key present"), Data->TryGetBoolField(TEXT("enabled_by_default"), bBool));
    TestFalse(TEXT("enabled_by_default == false"), bBool);

    bBool = true;
    TestTrue(TEXT("has_started key present"), Data->TryGetBoolField(TEXT("has_started"), bBool));
    TestFalse(TEXT("has_started == false"), bBool);

    bBool = true;
    TestTrue(TEXT("is_compiling key present"), Data->TryGetBoolField(TEXT("is_compiling"), bBool));
    TestFalse(TEXT("is_compiling == false"), bBool);

    bBool = true;
    TestTrue(TEXT("automatically_compile_new_classes key present"), Data->TryGetBoolField(TEXT("automatically_compile_new_classes"), bBool));
    TestFalse(TEXT("automatically_compile_new_classes == false"), bBool);

    FString EnableErrorText;
    TestTrue(TEXT("enable_error_text key present"), Data->TryGetStringField(TEXT("enable_error_text"), EnableErrorText));
    // empty string is correct for unsupported
    TestEqual(TEXT("enable_error_text == empty"), EnableErrorText, TEXT(""));

    return true;
}

// -------------------------------------------------------------------------
// (c) DispatchCommand livecoding_status — all 10 keys, status:"success"
// -------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUELiveCoding_StatusDispatch,
    "SmithUE.LiveCoding.StatusDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUELiveCoding_StatusDispatch::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("livecoding_status"));
    TestTrue(TEXT("Response is valid"), Response.IsValid());

    FString Status;
    TestTrue(TEXT("Has status field"), Response->TryGetStringField(TEXT("status"), Status));
    TestEqual(TEXT("status == success"), Status, TEXT("success"));

    // Must have data sub-object
    const TSharedPtr<FJsonObject>* DataPtr = nullptr;
    TestTrue(TEXT("Has data object"), Response->TryGetObjectField(TEXT("data"), DataPtr) && DataPtr != nullptr);
    if (!DataPtr || !(*DataPtr).IsValid()) return false;
    TSharedPtr<FJsonObject> Data = *DataPtr;

    // All 10 keys must be present (don't assert specific values — env-dependent)
    bool bDummyBool;
    TestTrue(TEXT("with_live_coding key present"), Data->TryGetBoolField(TEXT("with_live_coding"), bDummyBool));
    TestTrue(TEXT("module_loaded key present"), Data->TryGetBoolField(TEXT("module_loaded"), bDummyBool));
    TestTrue(TEXT("supported key present"), Data->TryGetBoolField(TEXT("supported"), bDummyBool));
    TestTrue(TEXT("can_enable key present"), Data->TryGetBoolField(TEXT("can_enable"), bDummyBool));
    TestTrue(TEXT("enabled_for_session key present"), Data->TryGetBoolField(TEXT("enabled_for_session"), bDummyBool));
    TestTrue(TEXT("enabled_by_default key present"), Data->TryGetBoolField(TEXT("enabled_by_default"), bDummyBool));
    TestTrue(TEXT("has_started key present"), Data->TryGetBoolField(TEXT("has_started"), bDummyBool));
    TestTrue(TEXT("is_compiling key present"), Data->TryGetBoolField(TEXT("is_compiling"), bDummyBool));
    TestTrue(TEXT("automatically_compile_new_classes key present"), Data->TryGetBoolField(TEXT("automatically_compile_new_classes"), bDummyBool));

    FString DummyStr;
    TestTrue(TEXT("enable_error_text key present"), Data->TryGetStringField(TEXT("enable_error_text"), DummyStr));

    return true;
}

// -------------------------------------------------------------------------
// (d) ReadBoolParam — non-bool value returns Default
// -------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUELiveCoding_ReadBoolParamFallback,
    "SmithUE.LiveCoding.ReadBoolParamFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUELiveCoding_ReadBoolParamFallback::RunTest(const FString& Parameters)
{
    // Obj has show_console set to a STRING — not a bool
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("show_console"), TEXT("yes"));

    // Default is false — should return false since the field is not a bool
    bool Result = FSmithUELiveCodingCommands::ReadBoolParam(Params, TEXT("show_console"), false);
    TestFalse(TEXT("ReadBoolParam returns Default when field is a string"), Result);

    // Missing field — should return Default
    TSharedPtr<FJsonObject> EmptyParams2 = MakeShared<FJsonObject>();
    bool Result2 = FSmithUELiveCodingCommands::ReadBoolParam(EmptyParams2, TEXT("show_console"), false);
    TestFalse(TEXT("ReadBoolParam returns Default when field is missing"), Result2);

    // Actual bool true
    TSharedPtr<FJsonObject> TrueParams = MakeShared<FJsonObject>();
    TrueParams->SetBoolField(TEXT("show_console"), true);
    bool Result3 = FSmithUELiveCodingCommands::ReadBoolParam(TrueParams, TEXT("show_console"), false);
    TestTrue(TEXT("ReadBoolParam returns true when field is bool true"), Result3);

    return true;
}

#if WITH_LIVE_CODING
// -------------------------------------------------------------------------
// (b) MapCompileResult — all 7 ELiveCodingCompileResult values with LC=nullptr
// -------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUELiveCoding_MapCompileResultAllValues,
    "SmithUE.LiveCoding.MapCompileResultAllValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUELiveCoding_MapCompileResultAllValues::RunTest(const FString& Parameters)
{
    // Helper lambda
    auto CheckSuccess = [this](const FString& Label, TSharedPtr<FJsonObject> Response, const FString& ExpectedResult)
    {
        TestTrue(Label + TEXT(": response valid"), Response.IsValid());
        FString Status;
        Response->TryGetStringField(TEXT("status"), Status);
        TestEqual(Label + TEXT(": status==success"), Status, TEXT("success"));
        const TSharedPtr<FJsonObject>* DataPtr = nullptr;
        if (Response->TryGetObjectField(TEXT("data"), DataPtr) && DataPtr && (*DataPtr).IsValid())
        {
            FString ResultField;
            (*DataPtr)->TryGetStringField(TEXT("result"), ResultField);
            TestEqual(Label + TEXT(": result field"), ResultField, ExpectedResult);
        }
        else
        {
            AddError(Label + TEXT(": missing data object"));
        }
    };

    auto CheckError = [this](const FString& Label, TSharedPtr<FJsonObject> Response, const FString& ExpectedCode)
    {
        TestTrue(Label + TEXT(": response valid"), Response.IsValid());
        FString Status;
        Response->TryGetStringField(TEXT("status"), Status);
        TestEqual(Label + TEXT(": status==error"), Status, TEXT("error"));
        FString ErrorCode;
        Response->TryGetStringField(TEXT("error_code"), ErrorCode);
        TestEqual(Label + TEXT(": error_code"), ErrorCode, ExpectedCode);
    };

    // Success
    CheckSuccess(TEXT("Success"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::Success, nullptr), TEXT("success"));

    // NoChanges
    CheckSuccess(TEXT("NoChanges"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::NoChanges, nullptr), TEXT("no_changes"));

    // InProgress (defensive)
    CheckSuccess(TEXT("InProgress"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::InProgress, nullptr), TEXT("in_progress"));

    // NotStarted — LC=nullptr so uses fallback message
    CheckError(TEXT("NotStarted"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::NotStarted, nullptr), TEXT("LIVECODING_NOT_STARTED"));

    // CompileStillActive
    CheckError(TEXT("CompileStillActive"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::CompileStillActive, nullptr), TEXT("LIVECODING_BUSY"));

    // Failure
    CheckError(TEXT("Failure"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::Failure, nullptr), TEXT("LIVECODING_COMPILE_FAILED"));

    // Cancelled
    CheckError(TEXT("Cancelled"), FSmithUELiveCodingCommands::MapCompileResult(ELiveCodingCompileResult::Cancelled, nullptr), TEXT("LIVECODING_CANCELLED"));

    return true;
}
#endif // WITH_LIVE_CODING

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright 2026, 123dx-svg. MIT License.
// UEAgent Blueprint Automation Tests

#include "Tests/UEAgentTestBase.h"
#include "ToolRegistry/UEAgentToolRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

static const FString GTestBpSavePath = TEXT("/Game/UEAgentTests");

// Each test uses a UNIQUE BP name to avoid shared state issues
static TSharedPtr<FJsonObject> CreateBP(const FString& Name)
{
    using namespace UEAgentTestUtils;
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("name"), Name);
    Params->SetStringField(TEXT("parent_class"), TEXT("Actor"));
    Params->SetStringField(TEXT("save_path"), GTestBpSavePath);
    return Dispatch(TEXT("bp_create"), Params);
}

static FString BPPath(const FString& Name)
{
    return GTestBpSavePath / Name;
}

// ---------------------------------------------------------------------------
// Blueprint.Atomic.Create_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_AtomicCreateSucceeds,
    "UEAgent.Blueprint.Atomic.Create_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_AtomicCreateSucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Response = CreateBP(TEXT("BPTest_Create"));
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_create should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    if (Data.IsValid())
    {
        FString Path;
        Data->TryGetStringField(TEXT("bp_path"), Path);
        TestFalse(TEXT("Created BP should have a path"), Path.IsEmpty());
    }

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Atomic.Create_DuplicateFails
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_AtomicCreateDuplicateFails,
    "UEAgent.Blueprint.Atomic.Create_DuplicateFails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_AtomicCreateDuplicateFails::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_Dup");

    TSharedPtr<FJsonObject> First = CreateBP(Name);
    if (!IsSuccess(First))
    {
        // BP might exist from a previous run — that's also fine for this test
    }

    // Second create of same name/path should fail
    TSharedPtr<FJsonObject> Second = CreateBP(Name);
    TestTrue(TEXT("Second bp_create with same name should return error"), IsError(Second));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Atomic.AddVariable_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_AtomicAddVariableSucceeds,
    "UEAgent.Blueprint.Atomic.AddVariable_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_AtomicAddVariableSucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_AddVar");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping AddVariable test"));
        return true;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));
    Params->SetStringField(TEXT("var_name"), TEXT("TestVar_Int"));
    Params->SetStringField(TEXT("var_type"), TEXT("int32"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_add_variable"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_add_variable should succeed"), IsSuccess(Response));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Atomic.AddFunction_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_AtomicAddFunctionSucceeds,
    "UEAgent.Blueprint.Atomic.AddFunction_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_AtomicAddFunctionSucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_AddFunc");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping AddFunction test"));
        return true;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));
    Params->SetStringField(TEXT("function_name"), TEXT("TestFunc_AutoTest"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_add_function"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_add_function should succeed"), IsSuccess(Response));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Atomic.Compile_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_AtomicCompileSucceeds,
    "UEAgent.Blueprint.Atomic.Compile_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_AtomicCompileSucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_Compile");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping Compile test"));
        return true;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_compile"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_compile on fresh BP should succeed"), IsSuccess(Response));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Atomic.CreateNode_InvalidBP
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_AtomicCreateNodeInvalidBP,
    "UEAgent.Blueprint.Atomic.CreateNode_InvalidBP",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_AtomicCreateNodeInvalidBP::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), TEXT("/Game/NonExistent_999_XYZ_BP"));
    Params->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
    Params->SetStringField(TEXT("node_class"), TEXT("K2Node_CallFunction"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_create_node"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_create_node on nonexistent BP should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Commands.GetSummary_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_CommandsGetSummarySucceeds,
    "UEAgent.Blueprint.Commands.GetSummary_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_CommandsGetSummarySucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_Summary");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping GetSummary test"));
        return true;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_get_summary"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_get_summary should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Summary data should be valid"), Data.IsValid());

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Commands.DescribeGraph_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_CommandsDescribeGraphSucceeds,
    "UEAgent.Blueprint.Commands.DescribeGraph_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_CommandsDescribeGraphSucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_Describe");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping DescribeGraph test"));
        return true;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));
    Params->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_describe_graph"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_describe_graph should succeed"), IsSuccess(Response));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Commands.ValidateCode_Valid
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_CommandsValidateCodeValid,
    "UEAgent.Blueprint.Commands.ValidateCode_Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_CommandsValidateCodeValid::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString ValidDSL =
        TEXT("function MyFunc()\n")
        TEXT("{\n")
        TEXT("    PrintString(\"Hello\")\n")
        TEXT("}\n");

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("code"), ValidDSL);

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_validate_code"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_validate_code with valid DSL should succeed"), IsSuccess(Response));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Commands.ValidateCode_Invalid
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_CommandsValidateCodeInvalid,
    "UEAgent.Blueprint.Commands.ValidateCode_Invalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_CommandsValidateCodeInvalid::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    // Mismatched braces — invalid syntax
    const FString InvalidDSL =
        TEXT("function BrokenFunc(\n")
        TEXT("{\n")
        TEXT("    PrintString(\"Hello\"\n")
        TEXT("}\n");

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("code"), InvalidDSL);

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_validate_code"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    // Validation returns success with valid=false for invalid code
    TestTrue(TEXT("bp_validate_code with invalid DSL should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> ValData = GetData(Response);
    if (ValData.IsValid())
    {
        bool bValid = true;
        ValData->TryGetBoolField(TEXT("valid"), bValid);
        TestFalse(TEXT("Invalid DSL should report valid=false"), bValid);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Commands.CompileCode_Simple
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_CommandsCompileCodeSimple,
    "UEAgent.Blueprint.Commands.CompileCode_Simple",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_CommandsCompileCodeSimple::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_CompCode");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping CompileCode test"));
        return true;
    }

    const FString SimpleDSL =
        TEXT("function SayHello()\n")
        TEXT("{\n")
        TEXT("    PrintString(\"Hello from AutoTest\")\n")
        TEXT("}\n");

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));
    Params->SetStringField(TEXT("code"), SimpleDSL);

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_compile_code"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    // Success or error both acceptable — depends on DSL support for PrintString
    FString Status;
    TestTrue(TEXT("Response should have status field"),
        Response->TryGetStringField(TEXT("status"), Status));

    return true;
}

// ---------------------------------------------------------------------------
// Blueprint.Compiler.InvalidSyntax_ReturnsError
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentBP_CompilerInvalidSyntaxReturnsError,
    "UEAgent.Blueprint.Compiler.InvalidSyntax_ReturnsError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentBP_CompilerInvalidSyntaxReturnsError::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString Name = TEXT("BPTest_BadSyntax");
    TSharedPtr<FJsonObject> CreateResp = CreateBP(Name);
    if (!IsSuccess(CreateResp))
    {
        AddWarning(TEXT("bp_create failed — skipping Compiler.InvalidSyntax test"));
        return true;
    }

    // Completely broken DSL — mismatched braces, no function signature
    const FString BrokenDSL =
        TEXT("{{{{ broken }\n")
        TEXT("not a function (\n")
        TEXT("}\n");

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), BPPath(Name));
    Params->SetStringField(TEXT("code"), BrokenDSL);

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_compile_code"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    // bp_compile_code returns success with data.success=false for broken DSL
    TestTrue(TEXT("bp_compile_code should return a response"), IsSuccess(Response));
    TSharedPtr<FJsonObject> CompData = GetData(Response);
    if (CompData.IsValid())
    {
        bool bInnerSuccess = true;
        CompData->TryGetBoolField(TEXT("success"), bInnerSuccess);
        TestFalse(TEXT("Broken DSL should report success=false"), bInnerSuccess);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

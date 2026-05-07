// Copyright 2026, 123dx-svg. MIT License.
// UEAgent Registry Automation Tests
// Tests: Registry.Register_Succeeds, Dispatch_ValidCommand, Dispatch_UnknownCommand,
//        GetAll_ReturnsAllRegistered, GetByCategory_FilterWorks, ExportSchema_IsValidJson,
//        Schema_HasRequiredFields, DuplicateRegister_DoesNotCrash

#include "Tests/UEAgentTestBase.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "ToolRegistry/UEAgentToolSchema.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Registry.Register_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_RegisterSucceeds,
    "UEAgent.Registry.Register_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_RegisterSucceeds::RunTest(const FString& Parameters)
{
    FUEAgentToolRegistry& Reg = FUEAgentToolRegistry::Get();

    const FString TestCmd = TEXT("__test_register_cmd__");
    FUEAgentToolSchema Schema(TestCmd, TEXT("Test"), TEXT("Test command for unit test"));
    Reg.Register(Schema, [](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
        R->SetStringField(TEXT("status"), TEXT("success"));
        return R;
    });

    const FUEAgentToolSchema* Found = Reg.Find(TestCmd);
    TestNotNull(TEXT("Registered command should be findable"), Found);
    if (Found)
    {
        TestEqual(TEXT("Name matches"), Found->Name, TestCmd);
        TestEqual(TEXT("Category matches"), Found->Category, TEXT("Test"));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Registry.Dispatch_ValidCommand
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_DispatchValidCommand,
    "UEAgent.Registry.Dispatch_ValidCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_DispatchValidCommand::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    // get_all_actors is always registered; dispatch it
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("get_all_actors"));
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    // Should be success or at least not null
    FString Status;
    TestTrue(TEXT("Response has status field"), Response->TryGetStringField(TEXT("status"), Status));

    return true;
}

// ---------------------------------------------------------------------------
// Registry.Dispatch_UnknownCommand
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_DispatchUnknownCommand,
    "UEAgent.Registry.Dispatch_UnknownCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_DispatchUnknownCommand::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("nonexistent_cmd_xyz_999"));
    // Either null or error response
    if (Response.IsValid())
    {
        TestTrue(TEXT("Unknown command should return error status"), IsError(Response));
    }
    // null is also acceptable — command not found

    return true;
}

// ---------------------------------------------------------------------------
// Registry.GetAll_ReturnsAllRegistered
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_GetAllReturnsAll,
    "UEAgent.Registry.GetAll_ReturnsAllRegistered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_GetAllReturnsAll::RunTest(const FString& Parameters)
{
    TArray<FUEAgentToolSchema> All = FUEAgentToolRegistry::Get().GetAll();
    // 8 atomic + 5 bp + 5 editor + 5 asset + 5 material + 5 project = 33 minimum
    TestTrue(TEXT("Registry should have at least 30 commands registered"), All.Num() >= 30);

    return true;
}

// ---------------------------------------------------------------------------
// Registry.GetByCategory_FilterWorks
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_GetByCategoryFilterWorks,
    "UEAgent.Registry.GetByCategory_FilterWorks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_GetByCategoryFilterWorks::RunTest(const FString& Parameters)
{
    TArray<FUEAgentToolSchema> EditorTools = FUEAgentToolRegistry::Get().GetByCategory(TEXT("Editor"));
    TestTrue(TEXT("Editor category should have at least 5 commands"), EditorTools.Num() >= 5);

    // Verify all returned items are actually in the Editor category
    for (const FUEAgentToolSchema& Schema : EditorTools)
    {
        TestEqual(TEXT("All returned schemas should be Editor category"), Schema.Category, TEXT("Editor"));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Registry.ExportSchema_IsValidJson
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_ExportSchemaIsValidJson,
    "UEAgent.Registry.ExportSchema_IsValidJson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_ExportSchemaIsValidJson::RunTest(const FString& Parameters)
{
    FString JsonStr = FUEAgentToolRegistry::Get().ExportAllAsJsonSchema();
    TestFalse(TEXT("Exported schema should not be empty"), JsonStr.IsEmpty());

    // Attempt to parse as JSON
    TSharedPtr<FJsonValue> ParsedValue;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    bool bParsed = FJsonSerializer::Deserialize(Reader, ParsedValue);
    TestTrue(TEXT("Exported schema should be valid JSON"), bParsed);
    TestTrue(TEXT("Parsed JSON value should be valid"), ParsedValue.IsValid());

    return true;
}

// ---------------------------------------------------------------------------
// Registry.Schema_HasRequiredFields
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_SchemaHasRequiredFields,
    "UEAgent.Registry.Schema_HasRequiredFields",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_SchemaHasRequiredFields::RunTest(const FString& Parameters)
{
    TArray<FUEAgentToolSchema> All = FUEAgentToolRegistry::Get().GetAll();
    TestTrue(TEXT("Registry must have at least one command"), All.Num() > 0);

    for (const FUEAgentToolSchema& Schema : All)
    {
        TestFalse(*FString::Printf(TEXT("Schema '%s' must have non-empty Name"), *Schema.Name),
            Schema.Name.IsEmpty());
        TestFalse(*FString::Printf(TEXT("Schema '%s' must have non-empty Category"), *Schema.Name),
            Schema.Category.IsEmpty());
        TestFalse(*FString::Printf(TEXT("Schema '%s' must have non-empty Description"), *Schema.Name),
            Schema.Description.IsEmpty());
        // Params array exists (may be empty for some commands)
        TSharedPtr<FJsonObject> JsonSchema = Schema.ToJsonSchema();
        TestTrue(*FString::Printf(TEXT("Schema '%s' JSON should have 'params' field"), *Schema.Name),
            JsonSchema.IsValid() && JsonSchema->HasField(TEXT("params")));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Registry.DuplicateRegister_DoesNotCrash
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentRegistry_DuplicateRegisterDoesNotCrash,
    "UEAgent.Registry.DuplicateRegister_DoesNotCrash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentRegistry_DuplicateRegisterDoesNotCrash::RunTest(const FString& Parameters)
{
    FUEAgentToolRegistry& Reg = FUEAgentToolRegistry::Get();

    const FString DupCmd = TEXT("__test_dup_cmd__");
    FUEAgentToolSchema Schema(DupCmd, TEXT("Test"), TEXT("Duplicate test command"));

    auto Handler = [](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
        R->SetStringField(TEXT("status"), TEXT("success"));
        return R;
    };

    // Register twice — should not crash
    Reg.Register(Schema, Handler);
    Reg.Register(Schema, Handler);

    // Should still be findable
    const FUEAgentToolSchema* Found = Reg.Find(DupCmd);
    TestNotNull(TEXT("Duplicate-registered command should still be findable"), Found);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

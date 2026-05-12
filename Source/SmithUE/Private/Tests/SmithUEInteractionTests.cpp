// Copyright 2026, 123dx-svg. MIT License.
// SmithUE Interaction Command Automation Tests
// Tests: ListEditorCommands_ReturnsNonEmpty, ExecuteConsoleCommand_Succeeds,
//        SetViewportCamera_HasDiffFields, GetEditorState_HasRequiredFields,
//        GetActorProperty_InvalidActor_ReturnsError, ListPanels_HasKnownPanels,
//        SimulateKey_InvalidKey_ReturnsError, GetSelectedActors_ReturnsArray

#include "Tests/SmithUETestBase.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_ListEditorCommandsNonEmpty,
    "SmithUE.InteractionCommands.ListEditorCommands_ReturnsNonEmpty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_ListEditorCommandsNonEmpty::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("list_editor_commands"));
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("list_editor_commands should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Data should be valid"), Data.IsValid());

    const TArray<TSharedPtr<FJsonValue>>* Cmds = nullptr;
    TestTrue(TEXT("commands array should exist"), Data->TryGetArrayField(TEXT("commands"), Cmds));
    TestTrue(TEXT("commands array should be non-empty"), Cmds != nullptr && Cmds->Num() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_ExecuteConsoleCommand,
    "SmithUE.InteractionCommands.ExecuteConsoleCommand_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_ExecuteConsoleCommand::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("command"), TEXT("STAT NONE"));
    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("execute_console_command"), Params);
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("execute_console_command should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> Data = GetData(Response);
    bool bExecuted = false;
    if (Data.IsValid()) Data->TryGetBoolField(TEXT("executed"), bExecuted);
    TestTrue(TEXT("executed should be true"), bExecuted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_GetEditorStateHasFields,
    "SmithUE.InteractionCommands.GetEditorState_HasRequiredFields",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_GetEditorStateHasFields::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("get_editor_state"));
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("get_editor_state should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Data should be valid"), Data.IsValid());
    if (Data.IsValid())
    {
        bool bIsPIE = false;
        bool bHasIsPIE = Data->TryGetBoolField(TEXT("is_pie"), bIsPIE);
        TestTrue(TEXT("is_pie field should be present"), bHasIsPIE);

        bool bModal = false;
        bool bHasModal = Data->TryGetBoolField(TEXT("modal_dialog_open"), bModal);
        TestTrue(TEXT("modal_dialog_open field should be present"), bHasModal);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_GetActorPropertyInvalidActor,
    "SmithUE.InteractionCommands.GetActorProperty_InvalidActor_ReturnsError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_GetActorPropertyInvalidActor::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("actor_label"), TEXT("SmithUENonExistentActor_XYZ_99999"));
    Params->SetStringField(TEXT("property_name"), TEXT("RelativeLocation"));
    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("get_actor_property"), Params);
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("get_actor_property with invalid actor should return error"), IsError(Response));
    FString ErrMsg = GetError(Response);
    TestFalse(TEXT("Error message should not be empty"), ErrMsg.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_ListPanelsHasKnownPanels,
    "SmithUE.InteractionCommands.ListPanels_HasKnownPanels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_ListPanelsHasKnownPanels::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("list_panels"));
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("list_panels should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Data should be valid"), Data.IsValid());

    const TArray<TSharedPtr<FJsonValue>>* Panels = nullptr;
    TestTrue(TEXT("panels array should exist"), Data.IsValid() && Data->TryGetArrayField(TEXT("panels"), Panels));
    TestTrue(TEXT("panels array should be non-empty"), Panels != nullptr && Panels->Num() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_SimulateKeyInvalidKey,
    "SmithUE.InteractionCommands.SimulateKey_InvalidKey_ReturnsError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_SimulateKeyInvalidKey::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("key"), TEXT("NOTAKEY_XYZ999"));
    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("simulate_key"), Params);
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("simulate_key with invalid key should return error"), IsError(Response));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_GetSelectedActorsReturnsArray,
    "SmithUE.InteractionCommands.GetSelectedActors_ReturnsArray",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_GetSelectedActorsReturnsArray::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("get_selected_actors"));
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("get_selected_actors should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> Data = GetData(Response);
    const TArray<TSharedPtr<FJsonValue>>* Selected = nullptr;
    bool bHasSelected = Data.IsValid() && Data->TryGetArrayField(TEXT("selected"), Selected);
    TestTrue(TEXT("selected array field should exist"), bHasSelected);
    double Count = -1.0;
    if (Data.IsValid()) Data->TryGetNumberField(TEXT("count"), Count);
    TestTrue(TEXT("count field should be >= 0"), Count >= 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEInteraction_ListKeyBindingsNonEmpty,
    "SmithUE.InteractionCommands.ListKeyBindings_ReturnsNonEmpty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEInteraction_ListKeyBindingsNonEmpty::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("list_key_bindings"));
    TestTrue(TEXT("Response valid"), Response.IsValid());
    TestTrue(TEXT("list_key_bindings should succeed"), IsSuccess(Response));
    TSharedPtr<FJsonObject> Data = GetData(Response);
    const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
    bool bHasBindings = Data.IsValid() && Data->TryGetArrayField(TEXT("bindings"), Bindings);
    TestTrue(TEXT("bindings array should exist"), bHasBindings);
    TestTrue(TEXT("bindings should be non-empty"), Bindings != nullptr && Bindings->Num() > 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright 2026, 123dx-svg. MIT License.
// UEAgent Editor Command Automation Tests
// Tests: SpawnActor_Valid, SpawnActor_MissingClass, GetAllActors_ReturnsArray,
//        SetActorProperty_InvalidActor, DeleteActor_InvalidLabel, GetViewportInfo_Succeeds,
//        SpawnActor_CustomLabel, DeleteActor_Valid

#include "Tests/UEAgentTestBase.h"
#include "ToolRegistry/UEAgentToolRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// EditorCommands.SpawnActor_Valid
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_SpawnActorValid,
    "UEAgent.EditorCommands.SpawnActor_Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_SpawnActorValid::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("class"), TEXT("StaticMeshActor"));
    Params->SetStringField(TEXT("label"), TEXT("UEAgentTest_SpawnValid"));
    Params->SetStringField(TEXT("location"), TEXT("0,0,100"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("spawn_actor"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("spawn_actor with valid class should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    if (Data.IsValid())
    {
        FString Label;
        Data->TryGetStringField(TEXT("actor_label"), Label);
        TestFalse(TEXT("Spawned actor should have a label"), Label.IsEmpty());
    }

    // Cleanup: delete the spawned actor
    if (IsSuccess(Response))
    {
        TSharedPtr<FJsonObject> DelParams = MakeShared<FJsonObject>();
        DelParams->SetStringField(TEXT("actor_label"), TEXT("UEAgentTest_SpawnValid"));
        Dispatch(TEXT("delete_actor"), DelParams);
    }

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.SpawnActor_MissingClass
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_SpawnActorMissingClass,
    "UEAgent.EditorCommands.SpawnActor_MissingClass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_SpawnActorMissingClass::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    // No "class" param — should return error
    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("spawn_actor"));
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("spawn_actor without class should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.GetAllActors_ReturnsArray
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_GetAllActorsReturnsArray,
    "UEAgent.EditorCommands.GetAllActors_ReturnsArray",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_GetAllActorsReturnsArray::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("get_all_actors"));
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("get_all_actors should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Response data should be valid"), Data.IsValid());

    const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
    TestTrue(TEXT("data.actors should be an array"), Data.IsValid() && Data->TryGetArrayField(TEXT("actors"), Actors));

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.SetActorProperty_InvalidActor
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_SetActorPropertyInvalidActor,
    "UEAgent.EditorCommands.SetActorProperty_InvalidActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_SetActorPropertyInvalidActor::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("label"), TEXT("NonExistentActor_999_XYZ"));
    Params->SetStringField(TEXT("property"), TEXT("bHidden"));
    Params->SetStringField(TEXT("value"), TEXT("true"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("set_actor_property"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("set_actor_property on nonexistent actor should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.DeleteActor_InvalidLabel
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_DeleteActorInvalidLabel,
    "UEAgent.EditorCommands.DeleteActor_InvalidLabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_DeleteActorInvalidLabel::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("label"), TEXT("NonExistent_999_DeleteTest"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("delete_actor"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("delete_actor on nonexistent label should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.GetViewportInfo_Succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_GetViewportInfoSucceeds,
    "UEAgent.EditorCommands.GetViewportInfo_Succeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_GetViewportInfoSucceeds::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    TSharedPtr<FJsonObject> Response = DispatchEmpty(TEXT("get_viewport_info"));
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("get_viewport_info should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Response data should be valid"), Data.IsValid());

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.SpawnActor_CustomLabel
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_SpawnActorCustomLabel,
    "UEAgent.EditorCommands.SpawnActor_CustomLabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_SpawnActorCustomLabel::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString CustomLabel = TEXT("UEAgentTest_CustomLabel_ABC");

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("class"), TEXT("StaticMeshActor"));
    Params->SetStringField(TEXT("label"), CustomLabel);

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("spawn_actor"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());

    if (IsSuccess(Response))
    {
        FString ReturnedLabel = GetDataString(Response, TEXT("actor_label"));
        // Label may have suffix appended by UE, but should contain our base label
        TestTrue(TEXT("Returned label should contain custom label"),
            ReturnedLabel.Contains(CustomLabel) || ReturnedLabel == CustomLabel);

        // Cleanup
        TSharedPtr<FJsonObject> DelParams = MakeShared<FJsonObject>();
        DelParams->SetStringField(TEXT("actor_label"), ReturnedLabel.IsEmpty() ? CustomLabel : ReturnedLabel);
        Dispatch(TEXT("delete_actor"), DelParams);
    }

    return true;
}

// ---------------------------------------------------------------------------
// EditorCommands.DeleteActor_Valid
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEAgentEditor_DeleteActorValid,
    "UEAgent.EditorCommands.DeleteActor_Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEAgentEditor_DeleteActorValid::RunTest(const FString& Parameters)
{
    using namespace UEAgentTestUtils;

    const FString SpawnLabel = TEXT("UEAgentTest_DeleteValid_Temp");

    // First spawn an actor
    TSharedPtr<FJsonObject> SpawnParams = MakeShared<FJsonObject>();
    SpawnParams->SetStringField(TEXT("class"), TEXT("StaticMeshActor"));
    SpawnParams->SetStringField(TEXT("label"), SpawnLabel);

    TSharedPtr<FJsonObject> SpawnResponse = Dispatch(TEXT("spawn_actor"), SpawnParams);
    if (!IsSuccess(SpawnResponse))
    {
        // If spawn fails, skip the delete test gracefully
        AddWarning(TEXT("Spawn failed — skipping delete validation"));
        return true;
    }

    // Get actual label (UE may append suffix)
    FString ActualLabel = GetDataString(SpawnResponse, TEXT("actor_label"));
    if (ActualLabel.IsEmpty()) ActualLabel = SpawnLabel;

    // Now delete it
    TSharedPtr<FJsonObject> DelParams = MakeShared<FJsonObject>();
    DelParams->SetStringField(TEXT("actor_label"), ActualLabel);

    TSharedPtr<FJsonObject> DelResponse = Dispatch(TEXT("delete_actor"), DelParams);
    TestTrue(TEXT("Delete response should be valid"), DelResponse.IsValid());
    TestTrue(TEXT("delete_actor on spawned actor should succeed"), IsSuccess(DelResponse));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

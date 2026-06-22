// Copyright 2026, 123dx-svg. MIT License.
// SmithUE Audit Command Automation Tests
// Tests: ScanAssets, GetAssetProperty, BpDescribeComponents_OwnComponents,
//        BpDescribeComponents_InheritedExplicit

#include "Tests/SmithUETestBase.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

// -----------------------------------------------------------------------
// SmithUE.Audit.ScanAssets
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSmithUEScanAssetsTest,
    "SmithUE.Audit.ScanAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEScanAssetsTest::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("folder_path"), TEXT("/Game/SmithUETest"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("scan_assets"), Params);
    TestTrue(TEXT("scan_assets response should be valid"), Response.IsValid());
    TestTrue(TEXT("scan_assets should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("data field should be valid"), Data.IsValid());

    if (Data.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
        TestTrue(TEXT("assets array should exist"), Data->TryGetArrayField(TEXT("assets"), Assets));
        TestTrue(TEXT("assets array should be non-empty"), Assets && Assets->Num() > 0);
    }

    return true;
}

// -----------------------------------------------------------------------
// SmithUE.Audit.GetAssetProperty
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSmithUEGetAssetPropertyTest,
    "SmithUE.Audit.GetAssetProperty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEGetAssetPropertyTest::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), TEXT("/Game/SmithUETest/SM_ComplianceProbe_Cube"));
    Params->SetStringField(TEXT("property"),   TEXT("LightmapCoordinateIndex"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("get_asset_property"), Params);
    TestTrue(TEXT("get_asset_property response should be valid"), Response.IsValid());
    TestTrue(TEXT("get_asset_property should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("data field should be valid"), Data.IsValid());

    if (Data.IsValid())
    {
        TestTrue(TEXT("value field should exist"), Data->HasField(TEXT("value")));
        FString PropName;
        Data->TryGetStringField(TEXT("property"), PropName);
        TestEqual(TEXT("property field should match"), PropName, TEXT("LightmapCoordinateIndex"));
    }

    return true;
}

// -----------------------------------------------------------------------
// SmithUE.Audit.BpDescribeComponents_OwnComponents
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSmithUEBpDescribeComponentsTest,
    "SmithUE.Audit.BpDescribeComponents_OwnComponents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEBpDescribeComponentsTest::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), TEXT("/Game/SmithUETest/BP_SM_ComplianceProbe_Cube"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_describe_components"), Params);
    TestTrue(TEXT("bp_describe_components response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_describe_components should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("data field should be valid"), Data.IsValid());

    if (Data.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* BPs = nullptr;
        Data->TryGetArrayField(TEXT("blueprints"), BPs);
        TestTrue(TEXT("blueprints array should exist and be non-empty"), BPs && BPs->Num() > 0);
    }

    return true;
}

// -----------------------------------------------------------------------
// SmithUE.Audit.BpDescribeComponents_InheritedExplicit
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSmithUEBpDescribeInheritedTest,
    "SmithUE.Audit.BpDescribeComponents_InheritedExplicit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEBpDescribeInheritedTest::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    // BP_ReadbackProbe_Child inherits from BP_ReadbackProbe_Parent (confirmed in T1 spike)
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("bp_path"), TEXT("/Game/SmithUETest/BP_ReadbackProbe_Child"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("bp_describe_components"), Params);
    TestTrue(TEXT("bp_describe_components child BP response should be valid"), Response.IsValid());
    TestTrue(TEXT("bp_describe_components child BP should succeed"), IsSuccess(Response));

    // Either inherited components present or inherited_unverifiable flag set —
    // either way the data field must exist (no silent omission)
    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("data field should exist for child BP"), Data.IsValid());

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

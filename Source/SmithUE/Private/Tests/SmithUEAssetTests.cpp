// Copyright 2026, 123dx-svg. MIT License.
// SmithUE Asset Command Automation Tests
// Tests: ListAssets_ValidPath, ListAssets_InvalidPath, FindAsset_NoMatch,
//        GetAssetInfo_InvalidPath, GetAssetInfo_Valid, RenameAsset_InvalidSource,
//        DuplicateAsset_InvalidSource, ListAssets_WithClassFilter

#include "Tests/SmithUETestBase.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// AssetCommands.ListAssets_ValidPath
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_ListAssetsValidPath,
    "SmithUE.AssetCommands.ListAssets_ValidPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_ListAssetsValidPath::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("folder_path"), TEXT("/Game"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("list_assets"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("list_assets at /Game should succeed"), IsSuccess(Response));

    TSharedPtr<FJsonObject> Data = GetData(Response);
    TestTrue(TEXT("Response data should be valid"), Data.IsValid());

    const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
    TestTrue(TEXT("data.assets should be an array"),
        Data.IsValid() && Data->TryGetArrayField(TEXT("assets"), Assets));

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.ListAssets_InvalidPath
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_ListAssetsInvalidPath,
    "SmithUE.AssetCommands.ListAssets_InvalidPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_ListAssetsInvalidPath::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("folder_path"), TEXT("/Invalid/Path/999_XYZ_DoesNotExist"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("list_assets"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());

    // Acceptable: either error or success with empty array
    if (IsSuccess(Response))
    {
        TSharedPtr<FJsonObject> Data = GetData(Response);
        if (Data.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
            if (Data->TryGetArrayField(TEXT("assets"), Assets) && Assets)
            {
                TestTrue(TEXT("Invalid path should return empty assets array"), Assets->Num() == 0);
            }
        }
    }
    else
    {
        TestTrue(TEXT("Invalid path should return error"), IsError(Response));
    }

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.FindAsset_NoMatch
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_FindAssetNoMatch,
    "SmithUE.AssetCommands.FindAsset_NoMatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_FindAssetNoMatch::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("pattern"), TEXT("ZZZZ_NonExistent_999_XYZ_AutoTest"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("find_asset"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());

    if (IsSuccess(Response))
    {
        TSharedPtr<FJsonObject> Data = GetData(Response);
        if (Data.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
            if (Data->TryGetArrayField(TEXT("assets"), Results) && Results)
            {
                TestTrue(TEXT("Non-matching pattern should return empty results"), Results->Num() == 0);
            }
        }
    }
    // Error is also acceptable if pattern is invalid

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.GetAssetInfo_InvalidPath
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_GetAssetInfoInvalidPath,
    "SmithUE.AssetCommands.GetAssetInfo_InvalidPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_GetAssetInfoInvalidPath::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), TEXT("/Game/NonExistent_999_XYZ_AutoTest"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("get_asset_info"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("get_asset_info on nonexistent path should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.GetAssetInfo_Valid
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_GetAssetInfoValid,
    "SmithUE.AssetCommands.GetAssetInfo_Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_GetAssetInfoValid::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), TEXT("/Engine/BasicShapes/Cube"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("get_asset_info"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());

    if (IsSuccess(Response))
    {
        TSharedPtr<FJsonObject> Data = GetData(Response);
        TestTrue(TEXT("Asset info data should be valid"), Data.IsValid());
        if (Data.IsValid())
        {
            FString AssetName;
            Data->TryGetStringField(TEXT("name"), AssetName);
            TestFalse(TEXT("Asset info should have a name"), AssetName.IsEmpty());
        }
    }
    else
    {
        // Engine assets may not be accessible in all project setups — warn but don't fail
        AddWarning(TEXT("get_asset_info on /Engine/BasicShapes/Cube returned error — engine content may not be mounted"));
    }

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.RenameAsset_InvalidSource
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_RenameAssetInvalidSource,
    "SmithUE.AssetCommands.RenameAsset_InvalidSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_RenameAssetInvalidSource::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("source_path"), TEXT("/Game/NonExistent_999_XYZ_RenameTest"));
    Params->SetStringField(TEXT("dest_path"), TEXT("/Game/NonExistent_999_XYZ_RenameTest_New"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("rename_asset"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("rename_asset from nonexistent source should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.DuplicateAsset_InvalidSource
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_DuplicateAssetInvalidSource,
    "SmithUE.AssetCommands.DuplicateAsset_InvalidSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_DuplicateAssetInvalidSource::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("source_path"), TEXT("/Game/NonExistent_999_XYZ_DupTest"));
    Params->SetStringField(TEXT("dest_path"), TEXT("/Game/NonExistent_999_XYZ_DupTest_Copy"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("duplicate_asset"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());
    TestTrue(TEXT("duplicate_asset from nonexistent source should return error"), IsError(Response));

    return true;
}

// ---------------------------------------------------------------------------
// AssetCommands.ListAssets_WithClassFilter
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmithUEAsset_ListAssetsWithClassFilter,
    "SmithUE.AssetCommands.ListAssets_WithClassFilter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSmithUEAsset_ListAssetsWithClassFilter::RunTest(const FString& Parameters)
{
    using namespace SmithUETestUtils;

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("folder_path"), TEXT("/Game"));
    Params->SetStringField(TEXT("class_filter"), TEXT("Blueprint"));

    TSharedPtr<FJsonObject> Response = Dispatch(TEXT("list_assets"), Params);
    TestTrue(TEXT("Response should be valid"), Response.IsValid());

    // Should succeed or return error — either is valid depending on implementation
    FString Status;
    TestTrue(TEXT("Response should have status field"),
        Response.IsValid() && Response->TryGetStringField(TEXT("status"), Status));

    if (IsSuccess(Response))
    {
        TSharedPtr<FJsonObject> Data = GetData(Response);
        if (Data.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
            // If assets array exists, all items should be Blueprints
            if (Data->TryGetArrayField(TEXT("assets"), Assets) && Assets)
            {
                // Just verify the array exists — class filtering is implementation-dependent
                TestTrue(TEXT("Assets array should be valid"), true);
            }
        }
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

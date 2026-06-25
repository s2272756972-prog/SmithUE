#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class SMITHUE_API FSmithUECommonUtils
{
public:
	static TSharedPtr<FJsonObject> CreateSuccessResponse(TSharedPtr<FJsonObject> Data);
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const FString& SimpleMessage);
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorCode = TEXT("INTERNAL_ERROR"));
	static FString SerializeJson(const TSharedPtr<FJsonObject>& JsonObject);
	static TSharedPtr<FJsonObject> ParseJson(const FString& JsonString);
	static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, FVector Default = FVector::ZeroVector);
	static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, FRotator Default = FRotator::ZeroRotator);
	static bool ValidateRequiredParams(const TSharedPtr<FJsonObject>& Params, const TArray<FString>& RequiredFields, FString& OutError);

	/**
	 * Normalize a Content Browser path to a real internal package path.
	 * Converts virtual paths ("/All/Game/BP" -> "/Game/BP", "/All/Plugins/Foo/BP" -> "/Foo/BP")
	 * via UContentBrowserDataSubsystem::TryConvertVirtualPath — the ONLY correct mapping, since
	 * plugin paths cannot be derived by stripping a fixed "/All" prefix.
	 * Already-internal paths pass through unchanged.
	 * Returns true if an authoritative conversion produced a real path; otherwise OutReal is a
	 * best-effort fallback (project-content only) or the original value (never silently dropped).
	 */
	static bool NormalizeContentBrowserPath(const FString& InPath, FString& OutReal);
};

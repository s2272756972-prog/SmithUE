#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class SMITHUE_API FSmithUECommonUtils
{
public:
	static TSharedPtr<FJsonObject> CreateSuccessResponse(TSharedPtr<FJsonObject> Data);
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const FString& SimpleMessage);
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage);
	static FString SerializeJson(const TSharedPtr<FJsonObject>& JsonObject);
	static TSharedPtr<FJsonObject> ParseJson(const FString& JsonString);
	static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, FVector Default = FVector::ZeroVector);
	static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, FRotator Default = FRotator::ZeroRotator);
	static bool ValidateRequiredParams(const TSharedPtr<FJsonObject>& Params, const TArray<FString>& RequiredFields, FString& OutError);
};

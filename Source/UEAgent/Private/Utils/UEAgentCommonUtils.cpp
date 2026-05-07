#include "Utils/UEAgentCommonUtils.h"

#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TSharedPtr<FJsonObject> FUEAgentCommonUtils::CreateSuccessResponse(TSharedPtr<FJsonObject> Data)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("success"));
	if (!Data.IsValid())
	{
		Data = MakeShared<FJsonObject>();
	}
	Response->SetObjectField(TEXT("data"), Data);
	return Response;
}

TSharedPtr<FJsonObject> FUEAgentCommonUtils::CreateSuccessResponse(const FString& SimpleMessage)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("message"), SimpleMessage);
	return CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentCommonUtils::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("error"));
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}

FString FUEAgentCommonUtils::SerializeJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return TEXT("{}");
	}

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
	{
		return TEXT("{}");
	}
	return Output;
}

TSharedPtr<FJsonObject> FUEAgentCommonUtils::ParseJson(const FString& JsonString)
{
	TSharedPtr<FJsonObject> Result;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonString);
	if (FJsonSerializer::Deserialize(Reader, Result) && Result.IsValid())
	{
		return Result;
	}
	return nullptr;
}

FVector FUEAgentCommonUtils::GetVectorFromJson(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, FVector Default)
{
	if (!Obj.IsValid() || !Obj->HasField(FieldName))
	{
		return Default;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Obj->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
	{
		return FVector((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
	}

	const TSharedPtr<FJsonObject>* VectorObjPtr = nullptr;
	if (Obj->TryGetObjectField(FieldName, VectorObjPtr) && VectorObjPtr && VectorObjPtr->IsValid())
	{
		const TSharedPtr<FJsonObject>& VectorObj = *VectorObjPtr;
		double X = Default.X;
		double Y = Default.Y;
		double Z = Default.Z;
		VectorObj->TryGetNumberField(TEXT("x"), X);
		VectorObj->TryGetNumberField(TEXT("y"), Y);
		VectorObj->TryGetNumberField(TEXT("z"), Z);
		return FVector(X, Y, Z);
	}

	return Default;
}

FRotator FUEAgentCommonUtils::GetRotatorFromJson(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, FRotator Default)
{
	if (!Obj.IsValid() || !Obj->HasField(FieldName))
	{
		return Default;
	}

	const TSharedPtr<FJsonObject>* RotatorObjPtr = nullptr;
	if (Obj->TryGetObjectField(FieldName, RotatorObjPtr) && RotatorObjPtr && RotatorObjPtr->IsValid())
	{
		const TSharedPtr<FJsonObject>& RotatorObj = *RotatorObjPtr;
		double Pitch = Default.Pitch;
		double Yaw = Default.Yaw;
		double Roll = Default.Roll;
		RotatorObj->TryGetNumberField(TEXT("pitch"), Pitch);
		RotatorObj->TryGetNumberField(TEXT("yaw"), Yaw);
		RotatorObj->TryGetNumberField(TEXT("roll"), Roll);
		return FRotator(Pitch, Yaw, Roll);
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Obj->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
	{
		return FRotator((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
	}

	return Default;
}

bool FUEAgentCommonUtils::ValidateRequiredParams(const TSharedPtr<FJsonObject>& Params, const TArray<FString>& RequiredFields, FString& OutError)
{
	if (!Params.IsValid())
	{
		OutError = TEXT("Missing required parameter: <params>");
		return false;
	}

	for (const FString& Field : RequiredFields)
	{
		if (!Params->HasField(Field))
		{
			OutError = FString::Printf(TEXT("Missing required parameter: %s"), *Field);
			return false;
		}
	}

	OutError.Reset();
	return true;
}

// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Helper macros and utilities for UEAgent tests
namespace UEAgentTestUtils
{
    // Create a params object with string fields
    inline TSharedPtr<FJsonObject> MakeParams(std::initializer_list<TPair<FString, FString>> Fields)
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        for (const auto& Pair : Fields)
        {
            Params->SetStringField(Pair.Key, Pair.Value);
        }
        return Params;
    }

    // Create an empty params object
    inline TSharedPtr<FJsonObject> EmptyParams()
    {
        return MakeShared<FJsonObject>();
    }

    // Check response is success
    inline bool IsSuccess(const TSharedPtr<FJsonObject>& Response)
    {
        FString Status;
        return Response.IsValid()
            && Response->TryGetStringField(TEXT("status"), Status)
            && Status == TEXT("success");
    }

    // Check response is error
    inline bool IsError(const TSharedPtr<FJsonObject>& Response)
    {
        FString Status;
        return Response.IsValid()
            && Response->TryGetStringField(TEXT("status"), Status)
            && Status == TEXT("error");
    }

    // Get data object from response
    inline TSharedPtr<FJsonObject> GetData(const TSharedPtr<FJsonObject>& Response)
    {
        const TSharedPtr<FJsonObject>* Data = nullptr;
        if (Response.IsValid() && Response->TryGetObjectField(TEXT("data"), Data))
        {
            return *Data;
        }
        return nullptr;
    }

    // Get error message from response
    inline FString GetError(const TSharedPtr<FJsonObject>& Response)
    {
        FString Msg;
        if (Response.IsValid())
        {
            Response->TryGetStringField(TEXT("error"), Msg);
        }
        return Msg;
    }

    // Dispatch a command through the registry
    inline TSharedPtr<FJsonObject> Dispatch(const FString& Command, TSharedPtr<FJsonObject> Params)
    {
        return FUEAgentToolRegistry::Get().DispatchCommand(Command, Params);
    }

    // Dispatch with no params
    inline TSharedPtr<FJsonObject> DispatchEmpty(const FString& Command)
    {
        return FUEAgentToolRegistry::Get().DispatchCommand(Command, EmptyParams());
    }

    // Check if data has an array field
    inline bool DataHasArray(const TSharedPtr<FJsonObject>& Response, const FString& FieldName)
    {
        TSharedPtr<FJsonObject> Data = GetData(Response);
        if (!Data.IsValid()) return false;
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        return Data->TryGetArrayField(FieldName, Arr) && Arr != nullptr;
    }

    // Get string field from data
    inline FString GetDataString(const TSharedPtr<FJsonObject>& Response, const FString& FieldName)
    {
        TSharedPtr<FJsonObject> Data = GetData(Response);
        FString Val;
        if (Data.IsValid())
        {
            Data->TryGetStringField(FieldName, Val);
        }
        return Val;
    }
}

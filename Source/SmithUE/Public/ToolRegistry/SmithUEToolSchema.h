// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FSmithUEToolParam
{
	FString Name;
	FString Type;
	FString Description;
	bool bRequired;
	FString DefaultValue;
	FString ItemsType;
	TArray<FString> AllowedValues;
	FString Example;

	FSmithUEToolParam(FString InName, FString InType, FString InDesc, bool bInRequired = false, FString InDefault = FString(), FString InItemsType = FString())
		: Name(MoveTemp(InName))
		, Type(MoveTemp(InType))
		, Description(MoveTemp(InDesc))
		, bRequired(bInRequired)
		, DefaultValue(MoveTemp(InDefault))
		, ItemsType(MoveTemp(InItemsType))
	{
	}

	FSmithUEToolParam& SetExample(FString InExample) { Example = MoveTemp(InExample); return *this; }
	FSmithUEToolParam& SetAllowedValues(TArray<FString> InValues) { AllowedValues = MoveTemp(InValues); return *this; }

	TSharedPtr<FJsonObject> ToJsonSchema() const
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		JsonObject->SetStringField(TEXT("name"), Name);
		JsonObject->SetStringField(TEXT("type"), Type);
		JsonObject->SetStringField(TEXT("description"), Description);
		JsonObject->SetBoolField(TEXT("required"), bRequired);
		if (!DefaultValue.IsEmpty())
		{
			JsonObject->SetStringField(TEXT("default"), DefaultValue);
		}
		if (!ItemsType.IsEmpty())
		{
			JsonObject->SetStringField(TEXT("itemsType"), ItemsType);
		}
		if (AllowedValues.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> EnumArray;
			for (const FString& Val : AllowedValues)
			{
				EnumArray.Add(MakeShared<FJsonValueString>(Val));
			}
			JsonObject->SetArrayField(TEXT("allowedValues"), EnumArray);
		}
		if (!Example.IsEmpty())
		{
			JsonObject->SetStringField(TEXT("example"), Example);
		}
		return JsonObject;
	}
};

struct FSmithUEToolSchema
{
	FString Name;
	FString Category;
	FString Description;
	TArray<FSmithUEToolParam> Params;

	FSmithUEToolSchema(FString InName, FString InCategory, FString InDesc, TArray<FSmithUEToolParam> InParams = {})
		: Name(MoveTemp(InName))
		, Category(MoveTemp(InCategory))
		, Description(MoveTemp(InDesc))
		, Params(MoveTemp(InParams))
	{
	}

	TSharedPtr<FJsonObject> ToJsonSchema() const
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		JsonObject->SetStringField(TEXT("name"), Name);
		JsonObject->SetStringField(TEXT("category"), Category);
		JsonObject->SetStringField(TEXT("description"), Description);

		TArray<TSharedPtr<FJsonValue>> JsonParams;
		JsonParams.Reserve(Params.Num());
		for (const FSmithUEToolParam& Param : Params)
		{
			JsonParams.Add(MakeShared<FJsonValueObject>(Param.ToJsonSchema()));
		}
		JsonObject->SetArrayField(TEXT("params"), JsonParams);
		return JsonObject;
	}
};

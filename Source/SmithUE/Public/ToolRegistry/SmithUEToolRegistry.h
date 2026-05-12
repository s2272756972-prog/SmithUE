// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "ToolRegistry/SmithUEToolSchema.h"

using FSmithUECommandHandler = TFunction<TSharedPtr<FJsonObject>(const TSharedPtr<FJsonObject>&)>;

class FSmithUEToolRegistry
{
public:
	static FSmithUEToolRegistry& Get();
	static void RegisterBuiltinCommands();

	void Register(const FSmithUEToolSchema& Schema, FSmithUECommandHandler Handler);
	const FSmithUEToolSchema* Find(const FString& Name) const;
	TSharedPtr<FJsonObject> DispatchCommand(const FString& Name, const TSharedPtr<FJsonObject>& Params) const;
	TArray<FSmithUEToolSchema> GetAll() const;
	TArray<FSmithUEToolSchema> GetByCategory(const FString& Category) const;
	FString ExportAllAsJsonSchema() const;
	void Reset();

private:
	TMap<FString, FSmithUEToolSchema> Tools;
	TMap<FString, FSmithUECommandHandler> Handlers;
};

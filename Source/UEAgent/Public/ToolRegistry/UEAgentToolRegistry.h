// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "ToolRegistry/UEAgentToolSchema.h"

using FUEAgentCommandHandler = TFunction<TSharedPtr<FJsonObject>(const TSharedPtr<FJsonObject>&)>;

class FUEAgentToolRegistry
{
public:
	static FUEAgentToolRegistry& Get();
	static void RegisterBuiltinCommands();

	void Register(const FUEAgentToolSchema& Schema, FUEAgentCommandHandler Handler);
	const FUEAgentToolSchema* Find(const FString& Name) const;
	TSharedPtr<FJsonObject> DispatchCommand(const FString& Name, const TSharedPtr<FJsonObject>& Params) const;
	TArray<FUEAgentToolSchema> GetAll() const;
	TArray<FUEAgentToolSchema> GetByCategory(const FString& Category) const;
	FString ExportAllAsJsonSchema() const;
	void Reset();

private:
	TMap<FString, FUEAgentToolSchema> Tools;
	TMap<FString, FUEAgentCommandHandler> Handlers;
};

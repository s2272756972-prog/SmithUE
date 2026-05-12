#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FSmithUEToolRegistry;

class SMITHUE_API FSmithUEDispatcher
{
public:
	static FSmithUEDispatcher& Get();

	FString DispatchSync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params);
	FString DispatchAsync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params);
	FString GetAsyncResult(const FString& TaskId);

private:
	FSmithUEDispatcher() = default;

	FString GenerateTaskId();

	FCriticalSection AsyncTaskLock;
	TMap<FString, FString> AsyncTaskResults;
	TSet<FString> ActiveTasks;
};

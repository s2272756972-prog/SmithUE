#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FUEAgentToolRegistry;

class UEAGENT_API FUEAgentDispatcher
{
public:
	static FUEAgentDispatcher& Get();

	FString DispatchSync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params);
	FString DispatchAsync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params);
	FString GetAsyncResult(const FString& TaskId);

private:
	FUEAgentDispatcher() = default;

	FString GenerateTaskId();

	FCriticalSection AsyncTaskLock;
	TMap<FString, FString> AsyncTaskResults;
	TSet<FString> ActiveTasks;
};

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"

class FSmithUEToolRegistry;

struct FAsyncTaskEntry
{
	FString TaskId;
	double StartTime = 0.0;       // FPlatformTime::Seconds() at dispatch
	double CompletedTime = -1.0;  // -1 = still running; set when result arrives
	FString ResultJson;           // empty = still running; non-empty = done
};

class SMITHUE_API FSmithUEDispatcher
{
public:
	static FSmithUEDispatcher& Get();

	FString DispatchSync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params);
	FString DispatchAsync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params);
	FString GetAsyncResult(const FString& TaskId);

	static constexpr float TaskTimeoutSeconds = 60.0f;
	static constexpr float TaskTTLSeconds = 300.0f;

private:
	FSmithUEDispatcher();
	~FSmithUEDispatcher();

	FString GenerateTaskId();
	void StartGCTicker();
	void StopGCTicker();

	FCriticalSection AsyncTaskLock;
	TMap<FString, FAsyncTaskEntry> AsyncTaskEntries;
	FDelegateHandle GCTickerHandle;
};

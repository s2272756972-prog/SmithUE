#include "Utils/SmithUEDispatcher.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

FSmithUEDispatcher::FSmithUEDispatcher()
{
	StartGCTicker();
}

FSmithUEDispatcher::~FSmithUEDispatcher()
{
	StopGCTicker();
}

FSmithUEDispatcher& FSmithUEDispatcher::Get()
{
	static FSmithUEDispatcher Instance;
	return Instance;
}

FString FSmithUEDispatcher::DispatchSync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
{
	if (IsInGameThread())
	{
		TSharedPtr<FJsonObject> Result = FSmithUEToolRegistry::Get().DispatchCommand(CommandName, Params);
		return FSmithUECommonUtils::SerializeJson(Result);
	}

	TPromise<FString> Promise;
	TFuture<FString> Future = Promise.GetFuture();
	AsyncTask(ENamedThreads::GameThread, [&Promise, CommandName, Params]()
	{
		TSharedPtr<FJsonObject> Result = FSmithUEToolRegistry::Get().DispatchCommand(CommandName, Params);
		Promise.SetValue(FSmithUECommonUtils::SerializeJson(Result));
	});
	return Future.Get();
}

FString FSmithUEDispatcher::DispatchAsync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
{
	const FString TaskId = GenerateTaskId();
	{
		FScopeLock Lock(&AsyncTaskLock);
		FAsyncTaskEntry Entry;
		Entry.TaskId = TaskId;
		Entry.StartTime = FPlatformTime::Seconds();
		AsyncTaskEntries.Add(TaskId, Entry);
	}

	AsyncTask(ENamedThreads::GameThread, [this, TaskId, CommandName, Params]()
	{
		TSharedPtr<FJsonObject> Result = FSmithUEToolRegistry::Get().DispatchCommand(CommandName, Params);
		const FString ResultJson = FSmithUECommonUtils::SerializeJson(Result);

		FScopeLock Lock(&AsyncTaskLock);
		if (FAsyncTaskEntry* Entry = AsyncTaskEntries.Find(TaskId))
		{
			Entry->ResultJson = ResultJson;
			Entry->CompletedTime = FPlatformTime::Seconds();
		}
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
}

FString FSmithUEDispatcher::GetAsyncResult(const FString& TaskId)
{
	FScopeLock Lock(&AsyncTaskLock);

	const FAsyncTaskEntry* Entry = AsyncTaskEntries.Find(TaskId);
	if (!Entry)
	{
		return FSmithUECommonUtils::SerializeJson(
			FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId)));
	}

	if (Entry->CompletedTime < 0.0)
	{
		// Still running
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("completed"), false);
		return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
	}

	// Completed (or timed out — ResultJson contains the result or error)
	TSharedPtr<FJsonObject> ParsedResult = FSmithUECommonUtils::ParseJson(Entry->ResultJson);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("completed"), true);
	if (ParsedResult.IsValid())
	{
		Data->SetObjectField(TEXT("result"), ParsedResult);
	}
	else
	{
		Data->SetStringField(TEXT("result"), Entry->ResultJson);
	}
	return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
}

void FSmithUEDispatcher::StartGCTicker()
{
	GCTickerHandle = FTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
		{
			const double Now = FPlatformTime::Seconds();
			FScopeLock Lock(&AsyncTaskLock);

			TArray<FString> ToRemove;
			for (auto& Pair : AsyncTaskEntries)
			{
				FAsyncTaskEntry& Entry = Pair.Value;

				// Timeout: still running but exceeded TaskTimeoutSeconds
				if (Entry.CompletedTime < 0.0 && (Now - Entry.StartTime) > TaskTimeoutSeconds)
				{
					// Store timeout error as result
					TSharedPtr<FJsonObject> ErrObj = FSmithUECommonUtils::CreateErrorResponse(
						FString::Printf(TEXT("Command execution timed out after %.0fs"), TaskTimeoutSeconds));
					Entry.ResultJson = FSmithUECommonUtils::SerializeJson(ErrObj);
					Entry.CompletedTime = Now;
					UE_LOG(LogTemp, Warning, TEXT("[SmithUE] Async task %s timed out"), *Entry.TaskId);
				}

				// GC: completed more than TaskTTLSeconds ago
				if (Entry.CompletedTime >= 0.0 && (Now - Entry.CompletedTime) > TaskTTLSeconds)
				{
					ToRemove.Add(Pair.Key);
				}
			}

			for (const FString& Key : ToRemove)
			{
				AsyncTaskEntries.Remove(Key);
			}

			return true; // keep ticking
		}),
		5.0f // tick every 5 seconds
	);
}

void FSmithUEDispatcher::StopGCTicker()
{
	if (GCTickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(GCTickerHandle);
		GCTickerHandle = FDelegateHandle();
	}
}

FString FSmithUEDispatcher::GenerateTaskId()
{
	return FGuid::NewGuid().ToString();
}

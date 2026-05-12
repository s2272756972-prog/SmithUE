#include "Utils/SmithUEDispatcher.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

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
		ActiveTasks.Add(TaskId);
		AsyncTaskResults.Add(TaskId, FString());
	}

	AsyncTask(ENamedThreads::GameThread, [this, TaskId, CommandName, Params]()
	{
		TSharedPtr<FJsonObject> Result = FSmithUEToolRegistry::Get().DispatchCommand(CommandName, Params);
		const FString ResultJson = FSmithUECommonUtils::SerializeJson(Result);

		FScopeLock Lock(&AsyncTaskLock);
		AsyncTaskResults.Add(TaskId, ResultJson);
		ActiveTasks.Remove(TaskId);
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
}

FString FSmithUEDispatcher::GetAsyncResult(const FString& TaskId)
{
	FScopeLock Lock(&AsyncTaskLock);

	if (const FString* StoredResult = AsyncTaskResults.Find(TaskId))
	{
		if (StoredResult->IsEmpty())
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("completed"), false);
			return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
		}

		TSharedPtr<FJsonObject> ParsedResult = FSmithUECommonUtils::ParseJson(*StoredResult);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("completed"), true);
		if (ParsedResult.IsValid())
		{
			Data->SetObjectField(TEXT("result"), ParsedResult);
		}
		else
		{
			Data->SetStringField(TEXT("result"), *StoredResult);
		}
		return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
	}

	if (ActiveTasks.Contains(TaskId))
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("completed"), false);
		return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateSuccessResponse(Data));
	}

	return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId)));
}

FString FSmithUEDispatcher::GenerateTaskId()
{
	return FGuid::NewGuid().ToString();
}

#include "Utils/UEAgentDispatcher.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "Utils/UEAgentCommonUtils.h"

FUEAgentDispatcher& FUEAgentDispatcher::Get()
{
	static FUEAgentDispatcher Instance;
	return Instance;
}

FString FUEAgentDispatcher::DispatchSync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
{
	if (IsInGameThread())
	{
		TSharedPtr<FJsonObject> Result = FUEAgentToolRegistry::Get().DispatchCommand(CommandName, Params);
		return FUEAgentCommonUtils::SerializeJson(Result);
	}

	TPromise<FString> Promise;
	TFuture<FString> Future = Promise.GetFuture();
	AsyncTask(ENamedThreads::GameThread, [&Promise, CommandName, Params]()
	{
		TSharedPtr<FJsonObject> Result = FUEAgentToolRegistry::Get().DispatchCommand(CommandName, Params);
		Promise.SetValue(FUEAgentCommonUtils::SerializeJson(Result));
	});
	return Future.Get();
}

FString FUEAgentDispatcher::DispatchAsync(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
{
	const FString TaskId = GenerateTaskId();
	{
		FScopeLock Lock(&AsyncTaskLock);
		ActiveTasks.Add(TaskId);
		AsyncTaskResults.Add(TaskId, FString());
	}

	AsyncTask(ENamedThreads::GameThread, [this, TaskId, CommandName, Params]()
	{
		TSharedPtr<FJsonObject> Result = FUEAgentToolRegistry::Get().DispatchCommand(CommandName, Params);
		const FString ResultJson = FUEAgentCommonUtils::SerializeJson(Result);

		FScopeLock Lock(&AsyncTaskLock);
		AsyncTaskResults.Add(TaskId, ResultJson);
		ActiveTasks.Remove(TaskId);
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateSuccessResponse(Data));
}

FString FUEAgentDispatcher::GetAsyncResult(const FString& TaskId)
{
	FScopeLock Lock(&AsyncTaskLock);

	if (const FString* StoredResult = AsyncTaskResults.Find(TaskId))
	{
		if (StoredResult->IsEmpty())
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("completed"), false);
			return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateSuccessResponse(Data));
		}

		TSharedPtr<FJsonObject> ParsedResult = FUEAgentCommonUtils::ParseJson(*StoredResult);
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
		return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateSuccessResponse(Data));
	}

	if (ActiveTasks.Contains(TaskId))
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("completed"), false);
		return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateSuccessResponse(Data));
	}

	return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId)));
}

FString FUEAgentDispatcher::GenerateTaskId()
{
	return FGuid::NewGuid().ToString();
}

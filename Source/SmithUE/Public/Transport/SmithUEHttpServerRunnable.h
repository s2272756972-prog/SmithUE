#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"

class FSocket;
class USmithUEHttpServer;

class FSmithUEHttpServerRunnable : public FRunnable
{
public:
	struct FParsedHttpRequest
	{
		FString Method;
		FString Path;
		FString Protocol;
		TMap<FString, FString> Headers;
		FString Body;
		int64 ContentLength = 0;
	};

	/** Maximum number of concurrent worker threads that can handle HTTP requests. */
	static constexpr int32 MaxConcurrentWorkers = 4;

	FSmithUEHttpServerRunnable(USmithUEHttpServer* InServer, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FSmithUEHttpServerRunnable();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	int32 GetActiveWorkerCount() const { return ActiveWorkerCount.GetValue(); }

	/** Returns true if the request requires execution on the game thread (UObject access etc.)
	 *  Returns false for lightweight read-only requests (/ready, ping, list_tools) that can
	 *  be served directly from a worker thread without any game-thread involvement. */
	static bool IsGameThreadRequired(const FParsedHttpRequest& Request);

private:
	USmithUEHttpServer* Server;
	TSharedPtr<FSocket> ListenerSocket;
	FThreadSafeCounter ActiveWorkerCount;
	FThreadSafeBool bStopping;
};

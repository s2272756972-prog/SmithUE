#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FSocket;
class USmithUEHttpServer;

class FSmithUEHttpServerRunnable : public FRunnable
{
public:
	FSmithUEHttpServerRunnable(USmithUEHttpServer* InServer, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FSmithUEHttpServerRunnable();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	USmithUEHttpServer* Server;
	TSharedPtr<FSocket> ListenerSocket;
	bool bStopping;
};

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FSocket;
class UUEAgentHttpServer;

class FUEAgentHttpServerRunnable : public FRunnable
{
public:
	FUEAgentHttpServerRunnable(UUEAgentHttpServer* InServer, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FUEAgentHttpServerRunnable();

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	UUEAgentHttpServer* Server;
	TSharedPtr<FSocket> ListenerSocket;
	bool bStopping;
};

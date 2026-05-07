#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Templates/Atomic.h"

class FSocket;
class UUEAgentTcpServer;

class UEAGENT_API FUEAgentTcpServerRunnable : public FRunnable
{
public:
	using FAtomicBool = TAtomic<bool>;

	FUEAgentTcpServerRunnable(UUEAgentTcpServer* InServer, FSocket* InListenerSocket);
	virtual ~FUEAgentTcpServerRunnable() override;

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	bool HandleClientMessage();
	void CloseClientSocket();

	UUEAgentTcpServer* Server = nullptr;
	FSocket* ListenerSocket = nullptr;
	FSocket* ClientSocket = nullptr;
	FAtomicBool bStopping = false;
};

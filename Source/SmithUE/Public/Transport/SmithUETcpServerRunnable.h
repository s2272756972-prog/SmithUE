#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Templates/Atomic.h"

class FSocket;
class USmithUETcpServer;

class SMITHUE_API FSmithUETcpServerRunnable : public FRunnable
{
public:
	using FAtomicBool = TAtomic<bool>;

	FSmithUETcpServerRunnable(USmithUETcpServer* InServer, FSocket* InListenerSocket);
	virtual ~FSmithUETcpServerRunnable() override;

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	bool HandleClientMessage();
	void CloseClientSocket();

	USmithUETcpServer* Server = nullptr;
	FSocket* ListenerSocket = nullptr;
	FSocket* ClientSocket = nullptr;
	FAtomicBool bStopping = false;
};

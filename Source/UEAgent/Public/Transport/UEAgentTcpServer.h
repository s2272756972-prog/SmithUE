#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Transport/IUEAgentTransport.h"

#include "UEAgentTcpServer.generated.h"

class FSocket;
class FRunnableThread;
class FUEAgentTcpServerRunnable;

UCLASS()
class UEAGENT_API UUEAgentTcpServer : public UEditorSubsystem, public IUEAgentTransport
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual bool Start() override;
	virtual void Stop() override;
	virtual bool IsRunning() const override;

	bool StartServer();
	void StopServer();

private:
	void ParseCommandLinePort();
	void DestroyListenerSocket();

	static constexpr uint16 DefaultPort = 13720;

	FSocket* ListenerSocket = nullptr;
	FRunnableThread* ServerThread = nullptr;
	bool bIsRunning = false;
	uint16 Port = DefaultPort;
	FUEAgentTcpServerRunnable* ServerRunnable = nullptr;
};

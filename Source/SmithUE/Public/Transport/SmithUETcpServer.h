#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Transport/ISmithUETransport.h"

#include "SmithUETcpServer.generated.h"

class FSocket;
class FRunnableThread;
class FSmithUETcpServerRunnable;

UCLASS()
class SMITHUE_API USmithUETcpServer : public UEditorSubsystem, public ISmithUETransport
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
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
	FSmithUETcpServerRunnable* ServerRunnable = nullptr;
};

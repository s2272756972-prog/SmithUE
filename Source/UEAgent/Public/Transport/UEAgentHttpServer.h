#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEAgentHttpServer.generated.h"

class FSocket;
class FRunnableThread;

UCLASS()
class UEAGENT_API UUEAgentHttpServer : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UUEAgentHttpServer();
	virtual ~UUEAgentHttpServer();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void StartServer();
	void StopServer();
	bool IsRunning() const { return bIsRunning; }

private:
	TSharedPtr<FSocket> ListenerSocket;
	FRunnableThread* ServerThread;
	bool bIsRunning;
	uint16 Port;
};

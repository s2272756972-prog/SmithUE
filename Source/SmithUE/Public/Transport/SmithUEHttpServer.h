#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "SmithUEHttpServer.generated.h"

class FSocket;
class FRunnableThread;

UCLASS()
class SMITHUE_API USmithUEHttpServer : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	USmithUEHttpServer();
	virtual ~USmithUEHttpServer();

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

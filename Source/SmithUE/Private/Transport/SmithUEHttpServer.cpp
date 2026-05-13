#include "Transport/SmithUEHttpServer.h"

#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Transport/SmithUEHttpServerRunnable.h"
#include "SmithUEModule.h"

USmithUEHttpServer::USmithUEHttpServer()
	: ServerThread(nullptr)
	, bIsRunning(false)
	, Port(13721)
{
}

USmithUEHttpServer::~USmithUEHttpServer()
{
}

bool USmithUEHttpServer::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet();
}

void USmithUEHttpServer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	int32 OverridePort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("SmithUEhttpport="), OverridePort) && OverridePort > 0 && OverridePort <= MAX_uint16)
	{
		Port = static_cast<uint16>(OverridePort);
	}

	StartServer();
}

void USmithUEHttpServer::Deinitialize()
{
	StopServer();
	Super::Deinitialize();
}

void USmithUEHttpServer::StartServer()
{
	if (bIsRunning)
	{
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to acquire socket subsystem for SmithUE HTTP server"));
		return;
	}

	FSocket* RawListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("SmithUEHttpListener"), false);
	TSharedPtr<FSocket> NewListenerSocket;
	if (RawListenerSocket != nullptr)
	{
		NewListenerSocket = MakeShareable(RawListenerSocket, ::FSocketDeleter(SocketSubsystem));
	}

	if (!NewListenerSocket.IsValid())
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to create SmithUE HTTP listener socket"));
		return;
	}

	NewListenerSocket->SetReuseAddr(true);
	NewListenerSocket->SetNonBlocking(true);

	FIPv4Address Address;
	FIPv4Address::Parse(TEXT("127.0.0.1"), Address);
	const FIPv4Endpoint Endpoint(Address, Port);
	if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()) || !NewListenerSocket->Listen(5))
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to bind/listen SmithUE HTTP server on 127.0.0.1:%d"), Port);
		NewListenerSocket.Reset();
		return;
	}

	ListenerSocket = NewListenerSocket;
	bIsRunning = true;
	ServerThread = FRunnableThread::Create(new FSmithUEHttpServerRunnable(this, ListenerSocket), TEXT("SmithUEHttpServerThread"));
	if (ServerThread == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to create SmithUE HTTP server thread"));
		StopServer();
		return;
	}

	UE_LOG(LogSmithUE, Log, TEXT("SmithUE HTTP server listening on 127.0.0.1:%d"), Port);
}

void USmithUEHttpServer::StopServer()
{
	if (!bIsRunning)
	{
		return;
	}

	bIsRunning = false;

	if (ServerThread != nullptr)
	{
		ServerThread->Kill(true);
		delete ServerThread;
		ServerThread = nullptr;
	}

	if (ListenerSocket.IsValid())
	{
		ListenerSocket.Reset();
	}
}

#include "Transport/UEAgentTcpServer.h"

#include "Transport/UEAgentTcpServerRunnable.h"
#include "UEAgentModule.h"

#include "HAL/RunnableThread.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

void UUEAgentTcpServer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ParseCommandLinePort();
	StartServer();
}

void UUEAgentTcpServer::Deinitialize()
{
	StopServer();

	Super::Deinitialize();
}

bool UUEAgentTcpServer::Start()
{
	return StartServer();
}

void UUEAgentTcpServer::Stop()
{
	StopServer();
}

bool UUEAgentTcpServer::IsRunning() const
{
	return bIsRunning;
}

bool UUEAgentTcpServer::StartServer()
{
	if (bIsRunning)
	{
		UE_LOG(LogUEAgent, Verbose, TEXT("UEAgent TCP server already running on port %d."), Port);
		return true;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to acquire socket subsystem for UEAgent TCP server."));
		return false;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UEAgentTcpListener"), false);
	if (ListenerSocket == nullptr)
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to create UEAgent TCP listener socket."));
		return false;
	}

	if (!ListenerSocket->SetReuseAddr(true))
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to enable SO_REUSEADDR on UEAgent TCP listener socket."));
		DestroyListenerSocket();
		return false;
	}

	if (!ListenerSocket->SetNonBlocking(true))
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to set UEAgent TCP listener socket to non-blocking mode."));
		DestroyListenerSocket();
		return false;
	}

	TSharedRef<FInternetAddr> ListenAddress = SocketSubsystem->CreateInternetAddr();
	ListenAddress->SetAnyAddress();
	ListenAddress->SetPort(Port);

	if (!ListenerSocket->Bind(*ListenAddress))
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to bind UEAgent TCP listener socket to port %d."), Port);
		DestroyListenerSocket();
		return false;
	}

	if (!ListenerSocket->Listen(5))
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to listen on UEAgent TCP port %d."), Port);
		DestroyListenerSocket();
		return false;
	}

	ServerRunnable = new FUEAgentTcpServerRunnable(this, ListenerSocket);
	ServerThread = FRunnableThread::Create(ServerRunnable, TEXT("UEAgentTcpServerThread"));
	if (ServerThread == nullptr)
	{
		UE_LOG(LogUEAgent, Error, TEXT("Failed to create UEAgent TCP server thread."));
		delete ServerRunnable;
		ServerRunnable = nullptr;
		DestroyListenerSocket();
		return false;
	}

	bIsRunning = true;
	UE_LOG(LogUEAgent, Log, TEXT("UEAgent TCP server started on port %d."), Port);
	return true;
}

void UUEAgentTcpServer::StopServer()
{
	if (!bIsRunning && ServerThread == nullptr && ListenerSocket == nullptr && ServerRunnable == nullptr)
	{
		return;
	}

	UE_LOG(LogUEAgent, Log, TEXT("Stopping UEAgent TCP server..."));

	bIsRunning = false;

	if (ServerRunnable != nullptr)
	{
		ServerRunnable->Stop();
	}

	if (ServerThread != nullptr)
	{
		ServerThread->Kill(true);
		delete ServerThread;
		ServerThread = nullptr;
	}

	delete ServerRunnable;
	ServerRunnable = nullptr;

	DestroyListenerSocket();

	UE_LOG(LogUEAgent, Log, TEXT("UEAgent TCP server stopped."));
}

void UUEAgentTcpServer::ParseCommandLinePort()
{
	int32 RequestedPort = 0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ueagentport="), RequestedPort))
	{
		return;
	}

	if (RequestedPort <= 0 || RequestedPort > MAX_uint16)
	{
		UE_LOG(LogUEAgent, Warning, TEXT("Ignoring invalid -ueagentport value: %d. Using default port %d."), RequestedPort, DefaultPort);
		return;
	}

	Port = static_cast<uint16>(RequestedPort);
	UE_LOG(LogUEAgent, Log, TEXT("UEAgent TCP port overridden via command line: %d."), Port);
}

void UUEAgentTcpServer::DestroyListenerSocket()
{
	if (ListenerSocket == nullptr)
	{
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	ListenerSocket->Close();
	if (SocketSubsystem != nullptr)
	{
		SocketSubsystem->DestroySocket(ListenerSocket);
	}
	ListenerSocket = nullptr;
}

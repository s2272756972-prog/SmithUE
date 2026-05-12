#include "Transport/SmithUETcpServer.h"

#include "Transport/SmithUETcpServerRunnable.h"
#include "SmithUEModule.h"

#include "HAL/RunnableThread.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

void USmithUETcpServer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ParseCommandLinePort();
	StartServer();
}

void USmithUETcpServer::Deinitialize()
{
	StopServer();

	Super::Deinitialize();
}

bool USmithUETcpServer::Start()
{
	return StartServer();
}

void USmithUETcpServer::Stop()
{
	StopServer();
}

bool USmithUETcpServer::IsRunning() const
{
	return bIsRunning;
}

bool USmithUETcpServer::StartServer()
{
	if (bIsRunning)
	{
		UE_LOG(LogSmithUE, Verbose, TEXT("SmithUE TCP server already running on port %d."), Port);
		return true;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to acquire socket subsystem for SmithUE TCP server."));
		return false;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("SmithUETcpListener"), false);
	if (ListenerSocket == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to create SmithUE TCP listener socket."));
		return false;
	}

	if (!ListenerSocket->SetReuseAddr(true))
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to enable SO_REUSEADDR on SmithUE TCP listener socket."));
		DestroyListenerSocket();
		return false;
	}

	if (!ListenerSocket->SetNonBlocking(true))
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to set SmithUE TCP listener socket to non-blocking mode."));
		DestroyListenerSocket();
		return false;
	}

	TSharedRef<FInternetAddr> ListenAddress = SocketSubsystem->CreateInternetAddr();
	ListenAddress->SetAnyAddress();
	ListenAddress->SetPort(Port);

	if (!ListenerSocket->Bind(*ListenAddress))
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to bind SmithUE TCP listener socket to port %d."), Port);
		DestroyListenerSocket();
		return false;
	}

	if (!ListenerSocket->Listen(5))
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to listen on SmithUE TCP port %d."), Port);
		DestroyListenerSocket();
		return false;
	}

	ServerRunnable = new FSmithUETcpServerRunnable(this, ListenerSocket);
	ServerThread = FRunnableThread::Create(ServerRunnable, TEXT("SmithUETcpServerThread"));
	if (ServerThread == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to create SmithUE TCP server thread."));
		delete ServerRunnable;
		ServerRunnable = nullptr;
		DestroyListenerSocket();
		return false;
	}

	bIsRunning = true;
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE TCP server started on port %d."), Port);
	return true;
}

void USmithUETcpServer::StopServer()
{
	if (!bIsRunning && ServerThread == nullptr && ListenerSocket == nullptr && ServerRunnable == nullptr)
	{
		return;
	}

	UE_LOG(LogSmithUE, Log, TEXT("Stopping SmithUE TCP server..."));

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

	UE_LOG(LogSmithUE, Log, TEXT("SmithUE TCP server stopped."));
}

void USmithUETcpServer::ParseCommandLinePort()
{
	int32 RequestedPort = 0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SmithUEport="), RequestedPort))
	{
		return;
	}

	if (RequestedPort <= 0 || RequestedPort > MAX_uint16)
	{
		UE_LOG(LogSmithUE, Warning, TEXT("Ignoring invalid -SmithUEport value: %d. Using default port %d."), RequestedPort, DefaultPort);
		return;
	}

	Port = static_cast<uint16>(RequestedPort);
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE TCP port overridden via command line: %d."), Port);
}

void USmithUETcpServer::DestroyListenerSocket()
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

#include "Transport/UEAgentTcpServerRunnable.h"

#include "Transport/UEAgentTcpServer.h"
#include "UEAgentModule.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Utils/UEAgentDispatcher.h"

#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	constexpr uint32 MaxPayloadLength = 16 * 1024 * 1024;
	constexpr int64 SocketWaitTimeoutMs = 250;

	void DestroySocket(FSocket*& Socket)
	{
		if (Socket == nullptr)
		{
			return;
		}

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		Socket->Close();
		if (SocketSubsystem != nullptr)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}

	bool ReadExactly(FSocket* Socket, uint8* Buffer, int32 NumBytes)
	{
		if (Socket == nullptr || Buffer == nullptr || NumBytes < 0)
		{
			return false;
		}

		int32 TotalRead = 0;
		while (TotalRead < NumBytes)
		{
			if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(SocketWaitTimeoutMs)))
			{
				if (Socket->GetConnectionState() != SCS_Connected)
				{
					return false;
				}
				continue;
			}

			int32 BytesRead = 0;
			if (!Socket->Recv(Buffer + TotalRead, NumBytes - TotalRead, BytesRead))
			{
				return false;
			}

			if (BytesRead <= 0)
			{
				return false;
			}

			TotalRead += BytesRead;
		}

		return true;
	}

	bool SendExactly(FSocket* Socket, const uint8* Buffer, int32 NumBytes)
	{
		if (Socket == nullptr || Buffer == nullptr || NumBytes < 0)
		{
			return false;
		}

		int32 TotalSent = 0;
		while (TotalSent < NumBytes)
		{
			if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(SocketWaitTimeoutMs)))
			{
				if (Socket->GetConnectionState() != SCS_Connected)
				{
					return false;
				}
				continue;
			}

			int32 BytesSent = 0;
			if (!Socket->Send(Buffer + TotalSent, NumBytes - TotalSent, BytesSent))
			{
				return false;
			}

			if (BytesSent <= 0)
			{
				return false;
			}

			TotalSent += BytesSent;
		}

		return true;
	}

	bool SendFramed(FSocket* Socket, const FString& JsonResponse)
	{
		FTCHARToUTF8 Utf8(*JsonResponse);
		const uint32 PayloadLength = static_cast<uint32>(Utf8.Length());

		uint8 Header[4];
		Header[0] = static_cast<uint8>((PayloadLength >> 0) & 0xFF);
		Header[1] = static_cast<uint8>((PayloadLength >> 8) & 0xFF);
		Header[2] = static_cast<uint8>((PayloadLength >> 16) & 0xFF);
		Header[3] = static_cast<uint8>((PayloadLength >> 24) & 0xFF);

		if (!SendExactly(Socket, Header, UE_ARRAY_COUNT(Header)))
		{
			return false;
		}

		if (PayloadLength == 0)
		{
			return true;
		}

		return SendExactly(Socket, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	uint32 ParsePayloadLength(const uint8 Header[4])
	{
		return static_cast<uint32>(Header[0]) |
			(static_cast<uint32>(Header[1]) << 8) |
			(static_cast<uint32>(Header[2]) << 16) |
			(static_cast<uint32>(Header[3]) << 24);
	}

	FString Utf8BytesToString(const TArray<uint8>& Payload)
	{
		if (Payload.IsEmpty())
		{
			return FString();
		}

		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Payload.GetData()), Payload.Num());
		return FString(Converter.Length(), Converter.Get());
	}

	FString MakeErrorJson(const FString& ErrorMessage)
	{
		return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateErrorResponse(ErrorMessage));
	}
}

FUEAgentTcpServerRunnable::FUEAgentTcpServerRunnable(UUEAgentTcpServer* InServer, FSocket* InListenerSocket)
	: Server(InServer)
	, ListenerSocket(InListenerSocket)
	, ClientSocket(nullptr)
	, bStopping(false)
{
}

FUEAgentTcpServerRunnable::~FUEAgentTcpServerRunnable()
{
	CloseClientSocket();
}

bool FUEAgentTcpServerRunnable::Init()
{
	return ListenerSocket != nullptr;
}

uint32 FUEAgentTcpServerRunnable::Run()
{
	while (!bStopping.Load())
	{
		if (ClientSocket == nullptr)
		{
			bool bHasPendingConnection = false;
			if (ListenerSocket != nullptr && ListenerSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
			{
				FSocket* AcceptedClient = ListenerSocket->Accept(TEXT("UEAgentTcpClient"));
				if (AcceptedClient != nullptr)
				{
					CloseClientSocket();
					ClientSocket = AcceptedClient;
					ClientSocket->SetNonBlocking(false);
					ClientSocket->SetNoDelay(true);
					UE_LOG(LogUEAgent, Log, TEXT("UEAgent TCP client connected."));
				}
				else
				{
					UE_LOG(LogUEAgent, Warning, TEXT("UEAgent TCP listener reported a pending connection, but Accept failed."));
				}
			}

			if (ClientSocket == nullptr)
			{
				FPlatformProcess::Sleep(0.05f);
				continue;
			}
		}

		if (!HandleClientMessage())
		{
			UE_LOG(LogUEAgent, Log, TEXT("UEAgent TCP client disconnected."));
			CloseClientSocket();
		}
	}

	CloseClientSocket();
	return 0;
}

void FUEAgentTcpServerRunnable::Stop()
{
	bStopping.Store(true);
}

void FUEAgentTcpServerRunnable::Exit()
{
	CloseClientSocket();
}

bool FUEAgentTcpServerRunnable::HandleClientMessage()
{
	check(ClientSocket != nullptr);

	uint8 Header[4] = {0, 0, 0, 0};
	if (!ReadExactly(ClientSocket, Header, UE_ARRAY_COUNT(Header)))
	{
		return false;
	}

	const uint32 PayloadLength = ParsePayloadLength(Header);
	if (PayloadLength > MaxPayloadLength)
	{
		UE_LOG(LogUEAgent, Warning, TEXT("UEAgent TCP received oversized payload: %u bytes."), PayloadLength);
		return SendFramed(ClientSocket, MakeErrorJson(FString::Printf(TEXT("Payload too large: %u bytes"), PayloadLength)));
	}

	TArray<uint8> Payload;
	Payload.SetNumUninitialized(static_cast<int32>(PayloadLength));
	if (PayloadLength > 0 && !ReadExactly(ClientSocket, Payload.GetData(), static_cast<int32>(PayloadLength)))
	{
		return false;
	}

	const FString RequestJson = Utf8BytesToString(Payload);
	const TSharedPtr<FJsonObject> RequestObject = FUEAgentCommonUtils::ParseJson(RequestJson);
	if (!RequestObject.IsValid())
	{
		UE_LOG(LogUEAgent, Warning, TEXT("UEAgent TCP received invalid JSON payload."));
		return SendFramed(ClientSocket, MakeErrorJson(TEXT("Invalid JSON payload.")));
	}

	FString CommandName;
	if (!RequestObject->TryGetStringField(TEXT("command"), CommandName) || CommandName.IsEmpty())
	{
		return SendFramed(ClientSocket, MakeErrorJson(TEXT("Missing required field: command")));
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
	if (RequestObject->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject != nullptr && ParamsObject->IsValid())
	{
		Params = *ParamsObject;
	}

	const FString ResponseJson = FUEAgentDispatcher::Get().DispatchSync(CommandName, Params);
	if (!SendFramed(ClientSocket, ResponseJson.IsEmpty() ? FString(TEXT("{}")) : ResponseJson))
	{
		UE_LOG(LogUEAgent, Warning, TEXT("Failed to send framed UEAgent TCP response."));
		return false;
	}

	return true;
}

void FUEAgentTcpServerRunnable::CloseClientSocket()
{
	DestroySocket(ClientSocket);
}

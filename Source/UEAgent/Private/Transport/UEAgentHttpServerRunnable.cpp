#include "Transport/UEAgentHttpServerRunnable.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Transport/UEAgentHttpServer.h"
#include "UEAgentModule.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Utils/UEAgentDispatcher.h"

namespace UEAgentHttpServer::Private
{
	constexpr int32 ReceiveBufferSize = 8 * 1024;
	struct FParsedHttpRequest
	{
		FString Method;
		FString Path;
		FString Protocol;
		TMap<FString, FString> Headers;
		FString Body;
		int32 ContentLength = 0;
	};

	int32 FindHeaderTerminator(const TArray<uint8>& Data)
	{
		for (int32 Index = 0; Index <= Data.Num() - 4; ++Index)
		{
			if (Data[Index] == '\r' && Data[Index + 1] == '\n' && Data[Index + 2] == '\r' && Data[Index + 3] == '\n')
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FString BytesToString(const uint8* Data, const int32 Length)
	{
		TArray<ANSICHAR> NullTerminated;
		NullTerminated.SetNumUninitialized(Length + 1);
		if (Length > 0)
		{
			FMemory::Memcpy(NullTerminated.GetData(), Data, Length);
		}
		NullTerminated[Length] = '\0';
		return UTF8_TO_TCHAR(NullTerminated.GetData());
	}

	bool ParseRequestHead(const FString& HeadText, FParsedHttpRequest& OutRequest)
	{
		TArray<FString> Lines;
		HeadText.ParseIntoArray(Lines, TEXT("\r\n"), true);
		if (Lines.Num() == 0)
		{
			return false;
		}

		TArray<FString> RequestLineParts;
		Lines[0].ParseIntoArray(RequestLineParts, TEXT(" "), true);
		if (RequestLineParts.Num() < 3)
		{
			return false;
		}

		OutRequest.Method = RequestLineParts[0];
		OutRequest.Path = RequestLineParts[1];
		OutRequest.Protocol = RequestLineParts[2];

		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			const FString& Line = Lines[Index];
			FString Key;
			FString Value;
			if (!Line.Split(TEXT(":"), &Key, &Value))
			{
				continue;
			}

			Key.TrimStartAndEndInline();
			Key.ToLowerInline();
			Value.TrimStartAndEndInline();
			OutRequest.Headers.Add(Key, Value);
		}

		if (const FString* ContentLengthValue = OutRequest.Headers.Find(TEXT("content-length")))
		{
			OutRequest.ContentLength = FMath::Max(FCString::Atoi(**ContentLengthValue), 0);
		}

		return true;
	}

	bool ReceiveHttpRequest(FSocket& ClientSocket, const bool& bStopping, FParsedHttpRequest& OutRequest)
	{
		TArray<uint8> ReceivedData;
		TArray<uint8> Chunk;
		Chunk.SetNumUninitialized(ReceiveBufferSize);

		int32 HeaderEndIndex = INDEX_NONE;
		int32 ExpectedTotalBytes = INDEX_NONE;
		const double TimeoutSeconds = 30.0;
		const double StartTime = FPlatformTime::Seconds();

		while (!bStopping)
		{
			if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
			{
				return false;
			}

			int32 BytesRead = 0;
			if (!ClientSocket.Recv(Chunk.GetData(), Chunk.Num(), BytesRead))
			{
				const ESocketErrors LastError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
				if (LastError == SE_EWOULDBLOCK || LastError == SE_EINTR)
				{
					FPlatformProcess::Sleep(0.01f);
					continue;
				}
				return false;
			}

			if (BytesRead <= 0)
			{
				// On non-blocking sockets, BytesRead==0 may mean "no data yet" rather than "closed"
				// If we're still expecting more data (body after 100-continue), keep waiting
				if (ExpectedTotalBytes != INDEX_NONE && ReceivedData.Num() < ExpectedTotalBytes)
				{
					FPlatformProcess::Sleep(0.01f);
					continue;
				}
				return false;
			}

			ReceivedData.Append(Chunk.GetData(), BytesRead);

			if (HeaderEndIndex == INDEX_NONE)
			{
				HeaderEndIndex = FindHeaderTerminator(ReceivedData);
				if (HeaderEndIndex != INDEX_NONE)
				{
					const FString HeadText = BytesToString(ReceivedData.GetData(), HeaderEndIndex);
					if (!ParseRequestHead(HeadText, OutRequest))
					{
						return false;
					}
					// Handle Expect: 100-continue (PowerShell, curl etc.)
					if (const FString* ExpectValue = OutRequest.Headers.Find(TEXT("expect")))
					{
						if (ExpectValue->Contains(TEXT("100-continue")))
						{
							const FString ContinueResponse = TEXT("HTTP/1.1 100 Continue\r\n\r\n");
							FTCHARToUTF8 Utf8Continue(*ContinueResponse);
							int32 Sent = 0;
							ClientSocket.Send(reinterpret_cast<const uint8*>(Utf8Continue.Get()), Utf8Continue.Length(), Sent);
						}
					}
					ExpectedTotalBytes = HeaderEndIndex + 4 + OutRequest.ContentLength;
				}
			}

			if (ExpectedTotalBytes != INDEX_NONE && ReceivedData.Num() >= ExpectedTotalBytes)
			{
				if (OutRequest.ContentLength > 0)
				{
					OutRequest.Body = BytesToString(ReceivedData.GetData() + HeaderEndIndex + 4, OutRequest.ContentLength);
				}
				return true;
			}
		}

		return false;
	}

	FString BuildHttpResponse(const int32 StatusCode, const FString& StatusText, const FString& Body)
	{
		FTCHARToUTF8 Utf8Body(*Body);
		return FString::Printf(
			TEXT("HTTP/1.1 %d %s\r\n")
			TEXT("Content-Type: application/json; charset=utf-8\r\n")
			TEXT("Content-Length: %d\r\n")
			TEXT("Access-Control-Allow-Origin: *\r\n")
			TEXT("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n")
			TEXT("Access-Control-Allow-Headers: Content-Type\r\n")
			TEXT("Connection: close\r\n")
			TEXT("\r\n")
			TEXT("%s"),
			StatusCode,
			*StatusText,
			Utf8Body.Length(),
			*Body);
	}

	FString BuildNoContentResponse()
	{
		return TEXT("HTTP/1.1 204 No Content\r\n"
			"Content-Length: 0\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
			"Access-Control-Allow-Headers: Content-Type\r\n"
			"Connection: close\r\n"
			"\r\n");
	}

	FString MakeErrorBody(const FString& Message)
	{
		return FUEAgentCommonUtils::SerializeJson(FUEAgentCommonUtils::CreateErrorResponse(Message));
	}

	FString ExtractTaskIdFromPath(const FString& Path)
	{
		static const FString Prefix = TEXT("/api/v1/async/");
		if (!Path.StartsWith(Prefix))
		{
			return FString();
		}
		return Path.RightChop(Prefix.Len());
	}

	bool SendAll(FSocket& ClientSocket, const FString& Response)
	{
		FTCHARToUTF8 Utf8Response(*Response);
		int32 TotalSent = 0;
		while (TotalSent < Utf8Response.Length())
		{
			int32 BytesSent = 0;
			if (!ClientSocket.Send(reinterpret_cast<const uint8*>(Utf8Response.Get()) + TotalSent, Utf8Response.Length() - TotalSent, BytesSent))
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

	struct FRouteResult
	{
		int32 StatusCode = 200;
		FString StatusText = TEXT("OK");
		FString Body;
	};

	FRouteResult RouteRequest(const FParsedHttpRequest& Request)
	{
		FRouteResult Result;

		if (Request.Method.Equals(TEXT("OPTIONS"), ESearchCase::IgnoreCase))
		{
			Result.StatusCode = 204;
			Result.StatusText = TEXT("No Content");
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/api/v1/health"))
		{
			Result.Body = FUEAgentDispatcher::Get().DispatchSync(TEXT("ping"), MakeShared<FJsonObject>());
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/api/v1/tools"))
		{
			Result.Body = FUEAgentDispatcher::Get().DispatchSync(TEXT("list_tools"), MakeShared<FJsonObject>());
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path.StartsWith(TEXT("/api/v1/async/")))
		{
			const FString TaskId = ExtractTaskIdFromPath(Request.Path);
			if (TaskId.IsEmpty())
			{
				Result.StatusCode = 404;
				Result.StatusText = TEXT("Not Found");
				Result.Body = MakeErrorBody(TEXT("Route not found"));
				return Result;
			}

			Result.Body = FUEAgentDispatcher::Get().GetAsyncResult(TaskId);
			return Result;
		}

		if (Request.Method.Equals(TEXT("POST"), ESearchCase::IgnoreCase) && (Request.Path == TEXT("/api/v1/execute") || Request.Path == TEXT("/api/v1/async")))
		{
			const TSharedPtr<FJsonObject> RequestJson = FUEAgentCommonUtils::ParseJson(Request.Body);
			if (!RequestJson.IsValid())
			{
				Result.StatusCode = 400;
				Result.StatusText = TEXT("Bad Request");
				Result.Body = MakeErrorBody(TEXT("Invalid JSON body"));
				return Result;
			}

			FString CommandName;
			if (!RequestJson->TryGetStringField(TEXT("command"), CommandName) || CommandName.IsEmpty())
			{
				Result.StatusCode = 400;
				Result.StatusText = TEXT("Bad Request");
				Result.Body = MakeErrorBody(TEXT("Missing 'command' field"));
				return Result;
			}

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* ParamsField = nullptr;
			if (RequestJson->TryGetObjectField(TEXT("params"), ParamsField) && ParamsField != nullptr && ParamsField->IsValid())
			{
				Params = *ParamsField;
			}

			Result.Body = Request.Path == TEXT("/api/v1/async")
				? FUEAgentDispatcher::Get().DispatchAsync(CommandName, Params)
				: FUEAgentDispatcher::Get().DispatchSync(CommandName, Params);
			return Result;
		}

		Result.StatusCode = 404;
		Result.StatusText = TEXT("Not Found");
		Result.Body = MakeErrorBody(TEXT("Route not found"));
		return Result;
	}
}

FUEAgentHttpServerRunnable::FUEAgentHttpServerRunnable(UUEAgentHttpServer* InServer, TSharedPtr<FSocket> InListenerSocket)
	: Server(InServer)
	, ListenerSocket(InListenerSocket)
	, bStopping(false)
{
}

FUEAgentHttpServerRunnable::~FUEAgentHttpServerRunnable()
{
}

bool FUEAgentHttpServerRunnable::Init()
{
	return Server != nullptr && ListenerSocket.IsValid();
}

uint32 FUEAgentHttpServerRunnable::Run()
{
	while (!bStopping)
	{
		bool bPendingConnection = false;
		if (!ListenerSocket.IsValid() || !ListenerSocket->HasPendingConnection(bPendingConnection) || !bPendingConnection)
		{
			FPlatformProcess::Sleep(0.05f);
			continue;
		}

		FSocket* RawClientSocket = ListenerSocket->Accept(TEXT("UEAgentHttpClient"));
		TSharedPtr<FSocket> ClientSocket;
		if (RawClientSocket != nullptr)
		{
			ClientSocket = MakeShareable(RawClientSocket, ::FSocketDeleter(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)));
		}

		if (!ClientSocket.IsValid())
		{
			FPlatformProcess::Sleep(0.05f);
			continue;
		}

		ClientSocket->SetNoDelay(true);
		ClientSocket->SetNonBlocking(true);

		UEAgentHttpServer::Private::FParsedHttpRequest Request;
		FString Response;
		if (UEAgentHttpServer::Private::ReceiveHttpRequest(*ClientSocket, bStopping, Request))
		{
			const UEAgentHttpServer::Private::FRouteResult RouteResult = UEAgentHttpServer::Private::RouteRequest(Request);
			Response = RouteResult.StatusCode == 204
				? UEAgentHttpServer::Private::BuildNoContentResponse()
				: UEAgentHttpServer::Private::BuildHttpResponse(RouteResult.StatusCode, RouteResult.StatusText, RouteResult.Body);
		}
		else
		{
			Response = UEAgentHttpServer::Private::BuildHttpResponse(400, TEXT("Bad Request"), UEAgentHttpServer::Private::MakeErrorBody(TEXT("Failed to parse HTTP request")));
		}

		if (!UEAgentHttpServer::Private::SendAll(*ClientSocket, Response))
		{
			UE_LOG(LogUEAgent, Verbose, TEXT("Failed to send HTTP response to UEAgent client"));
		}

		ClientSocket->Close();
	}

	return 0;
}

void FUEAgentHttpServerRunnable::Stop()
{
	bStopping = true;
}

void FUEAgentHttpServerRunnable::Exit()
{
}

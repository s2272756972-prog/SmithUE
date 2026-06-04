#include "Transport/SmithUEHttpServerRunnable.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Transport/SmithUEHttpServer.h"
#include "SmithUEModule.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Utils/SmithUEDispatcher.h"

namespace SmithUEHttpServer::Private
{
	constexpr int32 ReceiveBufferSize = 8 * 1024;
	struct FParsedHttpRequest
	{
		FString Method;
		FString Path;
		FString Protocol;
		TMap<FString, FString> Headers;
		FString Body;
		int64 ContentLength = 0;
	};

	enum class EHttpRequestReceiveResult
	{
		Success,
		ParseError,
		PayloadTooLarge,
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
			OutRequest.ContentLength = FMath::Max(FCString::Atoi64(**ContentLengthValue), 0LL);
		}

		return true;
	}

	EHttpRequestReceiveResult ReceiveHttpRequest(FSocket& ClientSocket, const bool& bStopping, FParsedHttpRequest& OutRequest)
	{
		TArray<uint8> ReceivedData;
		TArray<uint8> Chunk;
		Chunk.SetNumUninitialized(ReceiveBufferSize);

		int32 HeaderEndIndex = INDEX_NONE;
		int64 ExpectedTotalBytes = INDEX_NONE;
		const double TimeoutSeconds = 30.0;
		const double StartTime = FPlatformTime::Seconds();
		constexpr int64 MaxBodyBytes = 10LL * 1024LL * 1024LL;

		while (!bStopping)
		{
			if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
			{
				return EHttpRequestReceiveResult::ParseError;
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
				return EHttpRequestReceiveResult::ParseError;
			}

			if (BytesRead <= 0)
			{
				// On non-blocking sockets, BytesRead==0 may mean "no data yet" rather than "closed"
				// If we're still expecting more data (body after 100-continue), keep waiting
			if (ExpectedTotalBytes != INDEX_NONE && static_cast<int64>(ReceivedData.Num()) < ExpectedTotalBytes)
				{
					FPlatformProcess::Sleep(0.01f);
					continue;
				}
				return EHttpRequestReceiveResult::ParseError;
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
						return EHttpRequestReceiveResult::ParseError;
					}
					if (OutRequest.ContentLength > MaxBodyBytes)
					{
						return EHttpRequestReceiveResult::PayloadTooLarge;
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

			if (ExpectedTotalBytes != INDEX_NONE && static_cast<int64>(ReceivedData.Num()) >= ExpectedTotalBytes)
			{
				if (OutRequest.ContentLength > 0)
				{
					OutRequest.Body = BytesToString(ReceivedData.GetData() + HeaderEndIndex + 4, OutRequest.ContentLength);
				}
				return EHttpRequestReceiveResult::Success;
			}
		}

		return EHttpRequestReceiveResult::ParseError;
	}

	FString BuildHttpResponse(const int32 StatusCode, const FString& StatusText, const FString& Body)
	{
		FTCHARToUTF8 Utf8Body(*Body);
		return FString::Printf(
			TEXT("HTTP/1.1 %d %s\r\n")
			TEXT("Content-Type: application/json; charset=utf-8\r\n")
			TEXT("Content-Length: %d\r\n")
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
			"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
			"Access-Control-Allow-Headers: Content-Type\r\n"
			"Connection: close\r\n"
			"\r\n");
	}

	FString MakeErrorBody(const FString& Message, const FString& ErrorCode = TEXT("INTERNAL_ERROR"))
	{
		return FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(Message, ErrorCode));
	}

	bool IsNidString(const FString& Value)
	{
		return Value.StartsWith(TEXT("N")) && Value.Mid(1).IsNumeric();
	}

	bool JsonValueContainsNid(const TSharedPtr<FJsonValue>& Value);

	bool JsonObjectContainsNid(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			if (JsonValueContainsNid(Pair.Value))
			{
				return true;
			}
		}

		return false;
	}

	bool JsonValueContainsNid(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		switch (Value->Type)
		{
		case EJson::String:
			return IsNidString(Value->AsString());
		case EJson::Array:
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				if (JsonValueContainsNid(Item))
				{
					return true;
				}
			}
			return false;
		case EJson::Object:
			return JsonObjectContainsNid(Value->AsObject());
		default:
			return false;
		}
	}

	FString DispatchNidCommandSyncOnGameThread(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
	{
		check(!IsInGameThread());

		TPromise<FString> Promise;
		TFuture<FString> Future = Promise.GetFuture();
		AsyncTask(ENamedThreads::GameThread, [CommandName, Params, Promise = MoveTemp(Promise)]() mutable
		{
			check(IsInGameThread());
			TSharedPtr<FJsonObject> Result = FSmithUEToolRegistry::Get().DispatchCommand(CommandName, Params);
			Promise.SetValue(FSmithUECommonUtils::SerializeJson(Result));
		});

		return Future.Get();
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

	FRouteResult RouteRequest(const FParsedHttpRequest& Request, USmithUEHttpServer* InServer)
	{
		FRouteResult Result;

		if (Request.Method.Equals(TEXT("OPTIONS"), ESearchCase::IgnoreCase))
		{
			Result.StatusCode = 204;
			Result.StatusText = TEXT("No Content");
			return Result;
		}

		// ------------------------------------------------------------------
		//  GET /ready  — startup guard probe; non-blocking atomic read
		// ------------------------------------------------------------------
		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/ready"))
		{
			const bool bReady = InServer ? static_cast<bool>(InServer->bIsReady) : false;
			if (!bReady)
			{
				Result.StatusCode = 503;
				Result.StatusText = TEXT("Service Unavailable");
				Result.Body       = TEXT("{\"ready\":false}");
				return Result;
			}

			// Plugin version — safe to query from any thread
			FString PluginVersion = TEXT("unknown");
			if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE")))
			{
				PluginVersion = Plugin->GetDescriptor().VersionName;
			}

			// pie_active must be read on the GameThread
			check(!IsInGameThread());
			TPromise<bool> PiePromise;
			TFuture<bool>  PieFuture = PiePromise.GetFuture();
			AsyncTask(ENamedThreads::GameThread, [Promise = MoveTemp(PiePromise)]() mutable
			{
				check(IsInGameThread());
				const bool bPie = GEditor != nullptr && GEditor->PlayWorld != nullptr;
				Promise.SetValue(bPie);
			});
			const bool bPieActive = PieFuture.Get();

			Result.Body = FString::Printf(
				TEXT("{\"ready\":true,\"version\":\"%s\",\"pie_active\":%s}"),
				*PluginVersion,
				bPieActive ? TEXT("true") : TEXT("false"));
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/api/v1/health"))
		{
			Result.Body = FSmithUEDispatcher::Get().DispatchSync(TEXT("ping"), MakeShared<FJsonObject>());
			return Result;
		}

		if (Request.Method.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && Request.Path == TEXT("/api/v1/tools"))
		{
			Result.Body = FSmithUEDispatcher::Get().DispatchSync(TEXT("list_tools"), MakeShared<FJsonObject>());
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

			Result.Body = FSmithUEDispatcher::Get().GetAsyncResult(TaskId);
			return Result;
		}

		if (Request.Method.Equals(TEXT("POST"), ESearchCase::IgnoreCase) && (Request.Path == TEXT("/api/v1/execute") || Request.Path == TEXT("/api/v1/async")))
		{
			const TSharedPtr<FJsonObject> RequestJson = FSmithUECommonUtils::ParseJson(Request.Body);
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

			// ------------------------------------------------------------------
			//  Startup guard: block non-ping commands until editor is ready
			// ------------------------------------------------------------------
			if (Request.Path == TEXT("/api/v1/execute"))
			{
				const bool bReady = InServer ? static_cast<bool>(InServer->bIsReady) : false;
				if (!bReady && !CommandName.Equals(TEXT("ping"), ESearchCase::IgnoreCase))
				{
					Result.StatusCode = 503;
					Result.StatusText = TEXT("Service Unavailable");
					Result.Body       = MakeErrorBody(TEXT("Editor not ready"), TEXT("EDITOR_NOT_READY"));
					return Result;
				}
			}

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* ParamsField = nullptr;
			if (RequestJson->TryGetObjectField(TEXT("params"), ParamsField) && ParamsField != nullptr && ParamsField->IsValid())
			{
				Params = *ParamsField;
			}

			if (Request.Path == TEXT("/api/v1/async"))
			{
				Result.Body = FSmithUEDispatcher::Get().DispatchAsync(CommandName, Params);
			}
			else if (JsonObjectContainsNid(Params) && !IsInGameThread())
			{
				Result.Body = DispatchNidCommandSyncOnGameThread(CommandName, Params);
			}
			else
			{
				Result.Body = FSmithUEDispatcher::Get().DispatchSync(CommandName, Params);
			}
			return Result;
		}

		Result.StatusCode = 404;
		Result.StatusText = TEXT("Not Found");
		Result.Body = MakeErrorBody(TEXT("Route not found"));
		return Result;
	}
}

FSmithUEHttpServerRunnable::FSmithUEHttpServerRunnable(USmithUEHttpServer* InServer, TSharedPtr<FSocket> InListenerSocket)
	: Server(InServer)
	, ListenerSocket(InListenerSocket)
	, bStopping(false)
{
}

FSmithUEHttpServerRunnable::~FSmithUEHttpServerRunnable()
{
}

bool FSmithUEHttpServerRunnable::Init()
{
	return Server != nullptr && ListenerSocket.IsValid();
}

uint32 FSmithUEHttpServerRunnable::Run()
{
	while (!bStopping)
	{
		bool bPendingConnection = false;
		if (!ListenerSocket.IsValid() || !ListenerSocket->HasPendingConnection(bPendingConnection) || !bPendingConnection)
		{
			FPlatformProcess::Sleep(0.05f);
			continue;
		}

		FSocket* RawClientSocket = ListenerSocket->Accept(TEXT("SmithUEHttpClient"));
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

		SmithUEHttpServer::Private::FParsedHttpRequest Request;
		FString Response;
		const SmithUEHttpServer::Private::EHttpRequestReceiveResult ReceiveResult = SmithUEHttpServer::Private::ReceiveHttpRequest(*ClientSocket, bStopping, Request);
		if (ReceiveResult == SmithUEHttpServer::Private::EHttpRequestReceiveResult::Success)
		{
			const SmithUEHttpServer::Private::FRouteResult RouteResult = SmithUEHttpServer::Private::RouteRequest(Request, Server);
			Response = RouteResult.StatusCode == 204
				? SmithUEHttpServer::Private::BuildNoContentResponse()
				: SmithUEHttpServer::Private::BuildHttpResponse(RouteResult.StatusCode, RouteResult.StatusText, RouteResult.Body);
		}
		else if (ReceiveResult == SmithUEHttpServer::Private::EHttpRequestReceiveResult::PayloadTooLarge)
		{
			Response = SmithUEHttpServer::Private::BuildHttpResponse(413, TEXT("Payload Too Large"),
				FSmithUECommonUtils::SerializeJson(FSmithUECommonUtils::CreateErrorResponse(TEXT("payload too large"), TEXT("PAYLOAD_TOO_LARGE"))));
		}
		else
		{
			Response = SmithUEHttpServer::Private::BuildHttpResponse(400, TEXT("Bad Request"), SmithUEHttpServer::Private::MakeErrorBody(TEXT("Failed to parse HTTP request")));
		}

		if (!SmithUEHttpServer::Private::SendAll(*ClientSocket, Response))
		{
			UE_LOG(LogSmithUE, Verbose, TEXT("Failed to send HTTP response to SmithUE client"));
		}

		ClientSocket->Close();
	}

	return 0;
}

void FSmithUEHttpServerRunnable::Stop()
{
	bStopping = true;
}

void FSmithUEHttpServerRunnable::Exit()
{
}

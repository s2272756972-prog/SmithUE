// Copyright 2026, 123dx-svg. MIT License.

#include "ToolRegistry/SmithUEToolRegistry.h"

#include "Dom/JsonValue.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Transport/SmithUEConnectionManager.h"

namespace
{
	uint16 GetPortFromCommandLine(const TCHAR* FlagName, uint16 DefaultPort)
	{
		int32 RequestedPort = 0;
		if (!FParse::Value(FCommandLine::Get(), FlagName, RequestedPort) || RequestedPort <= 0 || RequestedPort > MAX_uint16)
		{
			return DefaultPort;
		}

		return static_cast<uint16>(RequestedPort);
	}

	void AppendToolJsonArray(const TArray<FSmithUEToolSchema>& Tools, TArray<TSharedPtr<FJsonValue>>& OutJsonTools)
	{
		OutJsonTools.Reserve(Tools.Num());
		for (const FSmithUEToolSchema& Tool : Tools)
		{
			OutJsonTools.Add(MakeShared<FJsonValueObject>(Tool.ToJsonSchema()));
		}
	}

}

FSmithUEToolRegistry& FSmithUEToolRegistry::Get()
{
	static FSmithUEToolRegistry Instance;
	return Instance;
}

void FSmithUEToolRegistry::Register(const FSmithUEToolSchema& Schema, FSmithUECommandHandler Handler)
{
	Tools.Add(Schema.Name, Schema);
	Handlers.Add(Schema.Name, MoveTemp(Handler));
}

const FSmithUEToolSchema* FSmithUEToolRegistry::Find(const FString& Name) const
{
	return Tools.Find(Name);
}

TSharedPtr<FJsonObject> FSmithUEToolRegistry::DispatchCommand(const FString& Name, const TSharedPtr<FJsonObject>& Params) const
{
	if (const FSmithUECommandHandler* Handler = Handlers.Find(Name))
	{
		return (*Handler)(Params);
	}

	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetStringField(TEXT("status"), TEXT("error"));
	Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *Name));
	return Error;
}

TArray<FSmithUEToolSchema> FSmithUEToolRegistry::GetAll() const
{
	TArray<FSmithUEToolSchema> Result;
	Result.Reserve(Tools.Num());
	for (const TPair<FString, FSmithUEToolSchema>& Pair : Tools)
	{
		Result.Add(Pair.Value);
	}
	return Result;
}

TArray<FSmithUEToolSchema> FSmithUEToolRegistry::GetByCategory(const FString& Category) const
{
	TArray<FSmithUEToolSchema> Result;
	for (const TPair<FString, FSmithUEToolSchema>& Pair : Tools)
	{
		if (Pair.Value.Category == Category)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

FString FSmithUEToolRegistry::ExportAllAsJsonSchema() const
{
	TArray<TSharedPtr<FJsonValue>> JsonTools;
	JsonTools.Reserve(Tools.Num());
	for (const TPair<FString, FSmithUEToolSchema>& Pair : Tools)
	{
		JsonTools.Add(MakeShared<FJsonValueObject>(Pair.Value.ToJsonSchema()));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("tools"), JsonTools);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

void FSmithUEToolRegistry::Reset()
{
	Tools.Empty();
	Handlers.Empty();
}

void FSmithUEToolRegistry::RegisterBuiltinCommands()
{
	FSmithUEToolRegistry& Registry = Get();

	if (Registry.Find(TEXT("ping")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(TEXT("ping"), TEXT("System"), TEXT("Test server connectivity")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("message"), TEXT("pong"));
				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}

	if (Registry.Find(TEXT("list_tools")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(
				TEXT("list_tools"),
				TEXT("System"),
				TEXT("List all available commands with schemas"),
				{
					FSmithUEToolParam(TEXT("category"), TEXT("string"), TEXT("Optional category filter"), false)
				}),
			[](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
			{
				FString Category;
				if (Params.IsValid())
				{
					Params->TryGetStringField(TEXT("category"), Category);
				}
				const TArray<FSmithUEToolSchema> Tools = Category.IsEmpty()
					? FSmithUEToolRegistry::Get().GetAll()
					: FSmithUEToolRegistry::Get().GetByCategory(Category);
				TArray<TSharedPtr<FJsonValue>> JsonTools;
				AppendToolJsonArray(Tools, JsonTools);

				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("protocol_version"), TEXT("1.0"));
				Data->SetArrayField(TEXT("tools"), JsonTools);
				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}

	if (Registry.Find(TEXT("get_protocol_info")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(TEXT("get_protocol_info"), TEXT("System"), TEXT("Get protocol and transport information")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));

				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("protocol_version"), TEXT("1.0"));
				Data->SetStringField(TEXT("server_name"), TEXT("SmithUE"));
				Data->SetStringField(TEXT("ue_version"), TEXT("5.2"));

				TArray<TSharedPtr<FJsonValue>> SupportedDomains;
				for (const TCHAR* Domain : {TEXT("Editor"), TEXT("Asset"), TEXT("Material"), TEXT("Project"), TEXT("Blueprint")})
				{
					SupportedDomains.Add(MakeShared<FJsonValueString>(FString(Domain)));
				}
				Data->SetArrayField(TEXT("supported_domains"), SupportedDomains);
				Data->SetNumberField(TEXT("tcp_port"), GetPortFromCommandLine(TEXT("SmithUEport="), 13720));
				Data->SetNumberField(TEXT("http_port"), GetPortFromCommandLine(TEXT("SmithUEhttpport="), 13721));
				Data->SetStringField(TEXT("framing_type"), TEXT("length_prefix_le32"));

				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}

	// --- Session management commands ---
	if (Registry.Find(TEXT("register_session")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(
				TEXT("register_session"),
				TEXT("System"),
				TEXT("Register an MCP client session for connection tracking"),
				{
					FSmithUEToolParam(TEXT("client_name"), TEXT("string"), TEXT("Client application name (e.g. OpenCode, Claude Code)"), true)
				}),
			[](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
			{
				FString ClientName;
				if (Params.IsValid())
				{
					Params->TryGetStringField(TEXT("client_name"), ClientName);
				}

				const FString SessionId = FSmithUEConnectionManager::Get().RegisterSession(ClientName);

				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("session_id"), SessionId);
				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}

	if (Registry.Find(TEXT("unregister_session")) == nullptr)
	{
		Registry.Register(
			FSmithUEToolSchema(
				TEXT("unregister_session"),
				TEXT("System"),
				TEXT("Unregister an MCP client session"),
				{
					FSmithUEToolParam(TEXT("session_id"), TEXT("string"), TEXT("Session ID returned by register_session"), true)
				}),
			[](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
			{
				FString SessionId;
				if (Params.IsValid())
				{
					Params->TryGetStringField(TEXT("session_id"), SessionId);
				}

				FSmithUEConnectionManager::Get().UnregisterSession(SessionId);

				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));
				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("message"), TEXT("session unregistered"));
				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}
}

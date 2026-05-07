// Copyright 2026, 123dx-svg. MIT License.

#include "ToolRegistry/UEAgentToolRegistry.h"

#include "Dom/JsonValue.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

	void AppendToolJsonArray(const TArray<FUEAgentToolSchema>& Tools, TArray<TSharedPtr<FJsonValue>>& OutJsonTools)
	{
		OutJsonTools.Reserve(Tools.Num());
		for (const FUEAgentToolSchema& Tool : Tools)
		{
			OutJsonTools.Add(MakeShared<FJsonValueObject>(Tool.ToJsonSchema()));
		}
	}

}

FUEAgentToolRegistry& FUEAgentToolRegistry::Get()
{
	static FUEAgentToolRegistry Instance;
	return Instance;
}

void FUEAgentToolRegistry::Register(const FUEAgentToolSchema& Schema, FUEAgentCommandHandler Handler)
{
	Tools.Add(Schema.Name, Schema);
	Handlers.Add(Schema.Name, MoveTemp(Handler));
}

const FUEAgentToolSchema* FUEAgentToolRegistry::Find(const FString& Name) const
{
	return Tools.Find(Name);
}

TSharedPtr<FJsonObject> FUEAgentToolRegistry::DispatchCommand(const FString& Name, const TSharedPtr<FJsonObject>& Params) const
{
	if (const FUEAgentCommandHandler* Handler = Handlers.Find(Name))
	{
		return (*Handler)(Params);
	}

	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetStringField(TEXT("status"), TEXT("error"));
	Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *Name));
	return Error;
}

TArray<FUEAgentToolSchema> FUEAgentToolRegistry::GetAll() const
{
	TArray<FUEAgentToolSchema> Result;
	Result.Reserve(Tools.Num());
	for (const TPair<FString, FUEAgentToolSchema>& Pair : Tools)
	{
		Result.Add(Pair.Value);
	}
	return Result;
}

TArray<FUEAgentToolSchema> FUEAgentToolRegistry::GetByCategory(const FString& Category) const
{
	TArray<FUEAgentToolSchema> Result;
	for (const TPair<FString, FUEAgentToolSchema>& Pair : Tools)
	{
		if (Pair.Value.Category == Category)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

FString FUEAgentToolRegistry::ExportAllAsJsonSchema() const
{
	TArray<TSharedPtr<FJsonValue>> JsonTools;
	JsonTools.Reserve(Tools.Num());
	for (const TPair<FString, FUEAgentToolSchema>& Pair : Tools)
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

void FUEAgentToolRegistry::Reset()
{
	Tools.Empty();
	Handlers.Empty();
}

void FUEAgentToolRegistry::RegisterBuiltinCommands()
{
	FUEAgentToolRegistry& Registry = Get();

	if (Registry.Find(TEXT("ping")) == nullptr)
	{
		Registry.Register(
			FUEAgentToolSchema(TEXT("ping"), TEXT("System"), TEXT("Test server connectivity")),
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
			FUEAgentToolSchema(
				TEXT("list_tools"),
				TEXT("System"),
				TEXT("List all available commands with schemas"),
				{
					FUEAgentToolParam(TEXT("category"), TEXT("string"), TEXT("Optional category filter"), false)
				}),
			[](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
			{
				FString Category;
				if (Params.IsValid())
				{
					Params->TryGetStringField(TEXT("category"), Category);
				}
				const TArray<FUEAgentToolSchema> Tools = Category.IsEmpty()
					? FUEAgentToolRegistry::Get().GetAll()
					: FUEAgentToolRegistry::Get().GetByCategory(Category);
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
			FUEAgentToolSchema(TEXT("get_protocol_info"), TEXT("System"), TEXT("Get protocol and transport information")),
			[](const TSharedPtr<FJsonObject>&) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				Response->SetStringField(TEXT("status"), TEXT("success"));

				TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("protocol_version"), TEXT("1.0"));
				Data->SetStringField(TEXT("server_name"), TEXT("UEAgent"));
				Data->SetStringField(TEXT("ue_version"), TEXT("5.2"));

				TArray<TSharedPtr<FJsonValue>> SupportedDomains;
				for (const TCHAR* Domain : {TEXT("Editor"), TEXT("Asset"), TEXT("Material"), TEXT("Project"), TEXT("Blueprint")})
				{
					SupportedDomains.Add(MakeShared<FJsonValueString>(FString(Domain)));
				}
				Data->SetArrayField(TEXT("supported_domains"), SupportedDomains);
				Data->SetNumberField(TEXT("tcp_port"), GetPortFromCommandLine(TEXT("ueagentport="), 13720));
				Data->SetNumberField(TEXT("http_port"), GetPortFromCommandLine(TEXT("ueagenthttpport="), 13721));
				Data->SetStringField(TEXT("framing_type"), TEXT("length_prefix_le32"));

				Response->SetObjectField(TEXT("data"), Data);
				return Response;
			});
	}
}

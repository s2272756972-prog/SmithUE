// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "ToolRegistry/SmithUEToolSchema.h"

using FSmithUECommandHandler = TFunction<TSharedPtr<FJsonObject>(const TSharedPtr<FJsonObject>&)>;

/** Per-graph N-id → GUID session map. Stored in ToolRegistry singleton (in-memory only). */
struct FSmithUENidSession
{
	/** GraphPath → (NidIndex → NodeGuid) */
	TMap<FString, TMap<int32, FGuid>> NidMap;
	/** GraphPath → bIsStale */
	TMap<FString, bool> StaleFlags;

	void StoreNids(const FString& GraphPath, const TMap<int32, FGuid>& Nids)
	{
		TMap<int32, FGuid>& Entry = NidMap.FindOrAdd(GraphPath);
		Entry.Empty();
		int32 Count = 0;
		for (const TPair<int32, FGuid>& Pair : Nids)
		{
			if (Count >= 1000) break;
			Entry.Add(Pair.Key, Pair.Value);
			Count++;
		}
		StaleFlags.FindOrAdd(GraphPath) = false;
	}

	FGuid ResolveNid(const FString& GraphPath, const FString& NidStr, bool& bOutIsStale) const
	{
		bOutIsStale = false;
		if (!NidStr.StartsWith(TEXT("N")) || !NidStr.Mid(1).IsNumeric())
		{
			return FGuid();
		}
		const int32 Index = FCString::Atoi(*NidStr.Mid(1));
		const bool* bStale = StaleFlags.Find(GraphPath);
		if (bStale && *bStale)
		{
			bOutIsStale = true;
			return FGuid();
		}
		const TMap<int32, FGuid>* GraphMap = NidMap.Find(GraphPath);
		if (!GraphMap)
		{
			return FGuid();
		}
		const FGuid* Found = GraphMap->Find(Index);
		return Found ? *Found : FGuid();
	}

	void MarkStale(const FString& GraphPath)
	{
		StaleFlags.FindOrAdd(GraphPath) = true;
	}

	bool IsStale(const FString& GraphPath) const
	{
		const bool* bStale = StaleFlags.Find(GraphPath);
		return bStale && *bStale;
	}

	void ClearGraph(const FString& GraphPath)
	{
		NidMap.Remove(GraphPath);
		StaleFlags.Remove(GraphPath);
	}
};

struct FSmithUEPerCommandMetrics
{
	int32 Count = 0;
	int64 TotalResponseBytes = 0;
};

struct FSmithUECommandMetrics
{
	int32 CallCount = 0;
	int64 TotalRequestBytes = 0;
	int64 TotalResponseBytes = 0;
	int32 RetryCount = 0;
	double TotalExecutionMs = 0.0;
	TMap<FString, FSmithUEPerCommandMetrics> PerCommand;
	FString LastCommandName;
	int64 LastRequestBytes = 0;
	int64 LastResponseBytes = 0;
	double LastExecutionMs = 0.0;
	bool bLastCommandWasError = false;
};

class FSmithUEToolRegistry
{
public:
	static FSmithUEToolRegistry& Get();
	static void RegisterBuiltinCommands();

	void Register(const FSmithUEToolSchema& Schema, FSmithUECommandHandler Handler);
	const FSmithUEToolSchema* Find(const FString& Name) const;
	TSharedPtr<FJsonObject> DispatchCommand(const FString& Name, const TSharedPtr<FJsonObject>& Params);
	TArray<FSmithUEToolSchema> GetAll() const;
	TArray<FSmithUEToolSchema> GetByCategory(const FString& Category) const;
	FString ExportAllAsJsonSchema() const;
	void Reset();

	/** N-id session storage — maps GraphPath → (NidIndex → NodeGuid). */
	FSmithUENidSession NidSession;

	/** Per-session command metrics (in-memory only, reset on editor restart). */
	FSmithUECommandMetrics Metrics;

private:
	TMap<FString, FSmithUEToolSchema> Tools;
	TMap<FString, FSmithUECommandHandler> Handlers;
};

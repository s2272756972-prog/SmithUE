// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * Tracks active MCP client sessions.
 * Thread-safe singleton — written from HTTP thread, read from Game thread (Slate).
 */
class SMITHUE_API FSmithUEConnectionManager
{
public:
	struct FClientSession
	{
		FString SessionId;
		FString ClientName;   // e.g. "OpenCode", "Claude Code", "Cline"
		FDateTime ConnectedAt;
		FDateTime LastHeartbeat;
	};

	static FSmithUEConnectionManager& Get();

	/** Register a new client session. Returns generated session id. */
	FString RegisterSession(const FString& ClientName);

	/** Unregister a client session by id. */
	void UnregisterSession(const FString& SessionId);

	/** Update heartbeat timestamp (called on every request with X-SmithUE-Session header). */
	void TouchSession(const FString& SessionId);

	/** Purge sessions that haven't sent a heartbeat in TimeoutSeconds. */
	void PurgeStale(double TimeoutSeconds = 30.0);

	/** Get a snapshot of all active sessions (thread-safe copy). */
	TArray<FClientSession> GetSessions() const;

	/** Total number of active sessions. */
	int32 GetSessionCount() const;

	/** Whether any session is active. */
	bool HasActiveSession() const;

private:
	mutable FCriticalSection Lock;
	TMap<FString, FClientSession> Sessions;
};

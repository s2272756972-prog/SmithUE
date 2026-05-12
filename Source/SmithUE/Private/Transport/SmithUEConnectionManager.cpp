// Copyright 2026, 123dx-svg. MIT License.

#include "Transport/SmithUEConnectionManager.h"
#include "SmithUEModule.h"
#include "Misc/Guid.h"

FSmithUEConnectionManager& FSmithUEConnectionManager::Get()
{
	static FSmithUEConnectionManager Instance;
	return Instance;
}

FString FSmithUEConnectionManager::RegisterSession(const FString& ClientName)
{
	FScopeLock ScopeLock(&Lock);

	const FString SessionId = FGuid::NewGuid().ToString(EGuidFormats::Short);

	FClientSession Session;
	Session.SessionId = SessionId;
	Session.ClientName = ClientName.IsEmpty() ? TEXT("Unknown") : ClientName;
	Session.ConnectedAt = FDateTime::UtcNow();
	Session.LastHeartbeat = Session.ConnectedAt;

	const FString LogName = Session.ClientName;
	Sessions.Add(SessionId, MoveTemp(Session));

	UE_LOG(LogSmithUE, Log, TEXT("Session registered: %s (client: %s)"), *SessionId, *LogName);
	return SessionId;
}

void FSmithUEConnectionManager::UnregisterSession(const FString& SessionId)
{
	FScopeLock ScopeLock(&Lock);

	if (Sessions.Remove(SessionId) > 0)
	{
		UE_LOG(LogSmithUE, Log, TEXT("Session unregistered: %s"), *SessionId);
	}
}

void FSmithUEConnectionManager::TouchSession(const FString& SessionId)
{
	FScopeLock ScopeLock(&Lock);

	if (FClientSession* Found = Sessions.Find(SessionId))
	{
		Found->LastHeartbeat = FDateTime::UtcNow();
	}
}

void FSmithUEConnectionManager::PurgeStale(double TimeoutSeconds)
{
	FScopeLock ScopeLock(&Lock);

	const FDateTime Now = FDateTime::UtcNow();
	TArray<FString> StaleIds;

	for (const auto& Pair : Sessions)
	{
		if ((Now - Pair.Value.LastHeartbeat).GetTotalSeconds() > TimeoutSeconds)
		{
			StaleIds.Add(Pair.Key);
		}
	}

	for (const FString& Id : StaleIds)
	{
		UE_LOG(LogSmithUE, Log, TEXT("Session expired: %s"), *Id);
		Sessions.Remove(Id);
	}
}

TArray<FSmithUEConnectionManager::FClientSession> FSmithUEConnectionManager::GetSessions() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FClientSession> Result;
	Sessions.GenerateValueArray(Result);
	return Result;
}

int32 FSmithUEConnectionManager::GetSessionCount() const
{
	FScopeLock ScopeLock(&Lock);
	return Sessions.Num();
}

bool FSmithUEConnectionManager::HasActiveSession() const
{
	FScopeLock ScopeLock(&Lock);
	return Sessions.Num() > 0;
}

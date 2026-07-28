// Copyright 2026, 123dx-svg. MIT License.

#include "Utils/SmithUEProgress.h"
#include "Misc/DateTime.h"

FSmithUEProgress& FSmithUEProgress::Get()
{
	static FSmithUEProgress Instance;
	return Instance;
}

void FSmithUEProgress::Begin(const FString& InOperation, int32 InTotal)
{
	JobId.fetch_add(1);
	Processed.store(0);
	Total.store(InTotal);
	const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
	StartUnix.store(Now);
	LastUpdateUnix.store(Now);
	{
		FScopeLock ScopeLock(&Lock);
		this->Operation = InOperation;
		this->CurrentItem.Reset();
	}
	bActive = true;
}

void FSmithUEProgress::Update(int32 InProcessed, const FString& InCurrentItem)
{
	Processed.store(InProcessed);
	LastUpdateUnix.store(FDateTime::UtcNow().ToUnixTimestamp());
	if (!InCurrentItem.IsEmpty())
	{
		FScopeLock ScopeLock(&Lock);
		CurrentItem = InCurrentItem;
	}
}

void FSmithUEProgress::Tick(const FString& InCurrentItem)
{
	Processed.fetch_add(1);
	LastUpdateUnix.store(FDateTime::UtcNow().ToUnixTimestamp());
	if (!InCurrentItem.IsEmpty())
	{
		FScopeLock ScopeLock(&Lock);
		CurrentItem = InCurrentItem;
	}
}

void FSmithUEProgress::End()
{
	bActive = false;
	LastUpdateUnix.store(FDateTime::UtcNow().ToUnixTimestamp());
	FScopeLock ScopeLock(&Lock);
	CurrentItem.Reset();
}

FString FSmithUEProgress::GetOperation() const
{
	FScopeLock ScopeLock(&Lock);
	return Operation;
}

FString FSmithUEProgress::GetCurrentItem() const
{
	FScopeLock ScopeLock(&Lock);
	return CurrentItem;
}

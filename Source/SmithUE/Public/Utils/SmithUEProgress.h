// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include <atomic>

/**
 * Lightweight, worker-safe progress beacon for long game-thread batch commands
 * (resave_packages / move_folder / fixup_redirectors / resolve_redirectors ...).
 *
 * The command itself still runs synchronously on the game thread (the HTTP call
 * will time out for big batches), but it publishes live progress here. The
 * `get_job_status` tool is registered WORKER-SAFE, so a poller can read真实进度
 * from the HTTP worker thread WHILE the game thread is busy — replacing the
 * previous "grep the editor log and guess" workflow.
 *
 * Threading: writers are on the game thread; readers are on HTTP worker threads.
 * All state is atomics + one CriticalSection-guarded FString, mirroring the
 * DialogWatcher contract (never touches GEditor/UObject on the read path).
 */
class FSmithUEProgress
{
public:
	static FSmithUEProgress& Get();

	// ---- writer side (game thread) ----
	/** Begin a job. Increments the job id and resets counters. */
	void Begin(const FString& Operation, int32 Total);
	/** Set absolute processed count (and optionally the current item label). */
	void Update(int32 Processed, const FString& CurrentItem = FString());
	/** Increment processed by one. */
	void Tick(const FString& CurrentItem = FString());
	/** Finish the current job. */
	void End();

	// ---- reader side (any thread) ----
	bool IsActive() const { return bActive; }
	int64 GetJobId() const { return JobId.load(); }
	int32 GetProcessed() const { return Processed.load(); }
	int32 GetTotal() const { return Total.load(); }
	FString GetOperation() const;
	FString GetCurrentItem() const;
	int64 GetStartUnixSeconds() const { return StartUnix.load(); }
	int64 GetLastUpdateUnixSeconds() const { return LastUpdateUnix.load(); }

private:
	FThreadSafeBool bActive{ false };
	std::atomic<int64> JobId{ 0 };
	std::atomic<int32> Processed{ 0 };
	std::atomic<int32> Total{ 0 };
	std::atomic<int64> StartUnix{ 0 };
	std::atomic<int64> LastUpdateUnix{ 0 };

	mutable FCriticalSection Lock;
	FString Operation;
	FString CurrentItem;
};

/** RAII helper: Begin on construct, End on destruct (exception/early-return safe). */
struct FSmithUEProgressScope
{
	FSmithUEProgressScope(const FString& Operation, int32 Total)
	{
		FSmithUEProgress::Get().Begin(Operation, Total);
	}
	~FSmithUEProgressScope()
	{
		FSmithUEProgress::Get().End();
	}
};

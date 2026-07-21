// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Ticker.h"
#include <atomic>

class SWindow;

/**
 * Watches for blocking Slate MODAL dialogs (e.g. the level "Save As" prompt) and
 * can respond to them EVEN WHILE the game thread is blocked inside
 * FSlateApplication::AddModalWindow's nested loop.
 *
 * Why this is needed: when a modal window is up, the game thread spins in a nested
 * Slate loop and normal FTSTicker / game-thread AsyncTasks do NOT run, so any
 * SmithUE tool marshalled to the game thread will hang until the dialog is closed
 * by a human. The ONLY delegate the engine broadcasts inside that loop is
 * FSlateApplication::OnModalLoopTickEvent (see SlateApplication.cpp AddModalWindow),
 * so we hook it to observe/close the dialog.
 *
 * Threading contract (PITFALLS #10):
 *  - Hook callbacks (OnModalLoopTick / OnNormalTick) run on the GAME THREAD.
 *  - All public getters/requesters are THREAD-SAFE and are meant to be called from
 *    the HTTP worker thread (the dialog tools are registered worker-safe), so they
 *    keep working while the game thread is jammed by a modal.
 *  - Worker-thread paths touch ONLY atomics + a CriticalSection-guarded FString.
 *    They never touch GEditor / UObject / Slate.
 */
class FSmithUEDialogWatcher
{
public:
	enum class EResponse : int32
	{
		None = 0,
		Cancel = 1, // reliably close/destroy the modal window (safe unblock)
		Accept = 2, // best-effort: focus + synthesize Enter (default action)
	};

	static FSmithUEDialogWatcher& Get();

	/** Register the Slate modal-loop hook + a normal ticker. Call on game thread. */
	void Initialize();
	/** Unregister hooks. Call on game thread. */
	void Shutdown();

	// ---- Thread-safe queries (any thread) ----
	bool IsModalActive() const { return bModalActive; }
	FString GetActiveTitle() const;
	FString GetActiveType() const;
	int32 GetDismissedCount() const { return DismissedCounter.GetValue(); }
	EResponse GetAutoResponse() const { return static_cast<EResponse>(AutoResponseMode.load()); }

	// ---- Thread-safe requests (any thread) ----
	void SetAutoResponse(EResponse Mode) { AutoResponseMode.store(static_cast<int32>(Mode)); }
	/** Queue a one-shot dismissal applied on the next modal-loop tick. */
	void RequestDismiss(EResponse Response)
	{
		OneShotResponse.store(static_cast<int32>(Response == EResponse::None ? EResponse::Cancel : Response));
	}

	static const TCHAR* ResponseToString(EResponse R);
	static EResponse ResponseFromString(const FString& S, bool& bOutValid);

private:
	void OnModalLoopTick(float DeltaTime); // game thread, during modal loop
	bool OnNormalTick(float DeltaTime);    // game thread, normal loop (resets state)

	FThreadSafeBool bModalActive{ false };
	std::atomic<int32> AutoResponseMode{ 0 };
	std::atomic<int32> OneShotResponse{ 0 };
	FThreadSafeCounter DismissedCounter;

	mutable FCriticalSection StateLock;
	FString ActiveTitle;
	FString ActiveType;

	/** Window we have already actioned this modal session (avoids Enter spam / double count). */
	TWeakPtr<SWindow> HandledWindow;

	FDelegateHandle ModalTickHandle;
	FTSTicker::FDelegateHandle NormalTickHandle;
	bool bInitialized = false;
};

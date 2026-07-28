// Copyright 2026, 123dx-svg. MIT License.

#include "Dialog/SmithUEDialogWatcher.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "SmithUEModule.h"

namespace
{
	FString WindowTypeToString(EWindowType Type)
	{
		switch (Type)
		{
		case EWindowType::Normal:            return TEXT("Normal");
		case EWindowType::Menu:              return TEXT("Menu");
		case EWindowType::ToolTip:           return TEXT("ToolTip");
		case EWindowType::Notification:      return TEXT("Notification");
		case EWindowType::CursorDecorator:   return TEXT("CursorDecorator");
		default:                             return TEXT("Unknown");
		}
	}

	/** Best-effort: focus the window and synthesize an Enter key (the default action of most dialogs). */
	void SendEnterKey(const TSharedRef<SWindow>& Win)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& App = FSlateApplication::Get();
		App.SetKeyboardFocus(Win, EFocusCause::SetDirectly);

		const uint32 EnterCharCode = TCHAR('\r');
		FKeyEvent KeyDown(EKeys::Enter, FModifierKeysState(), 0, /*bIsRepeat*/ false, EnterCharCode, EnterCharCode);
		App.ProcessKeyDownEvent(KeyDown);
		FKeyEvent KeyUp(EKeys::Enter, FModifierKeysState(), 0, /*bIsRepeat*/ false, EnterCharCode, EnterCharCode);
		App.ProcessKeyUpEvent(KeyUp);
	}

	/** Recursively collect the visible text of a widget subtree (first STextBlock found wins per branch). */
	FString ExtractWidgetText(const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetTypeAsString() == TEXT("STextBlock"))
		{
			return StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
		}
		FChildren* Children = Widget->GetChildren();
		if (Children)
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				const FString Text = ExtractWidgetText(Children->GetChildAt(i));
				if (!Text.IsEmpty())
				{
					return Text;
				}
			}
		}
		return FString();
	}

	/** Recursively collect all SButtons (with their labels) inside a widget subtree. */
	void CollectButtons(const TSharedRef<SWidget>& Widget, TArray<TPair<TSharedRef<SButton>, FString>>& OutButtons)
	{
		if (Widget->GetTypeAsString() == TEXT("SButton"))
		{
			TSharedRef<SButton> Button = StaticCastSharedRef<SButton>(Widget);
			OutButtons.Emplace(Button, ExtractWidgetText(Widget));
			return; // buttons rarely nest; treat as leaf
		}
		FChildren* Children = Widget->GetChildren();
		if (Children)
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				CollectButtons(Children->GetChildAt(i), OutButtons);
			}
		}
	}
}

FSmithUEDialogWatcher& FSmithUEDialogWatcher::Get()
{
	static FSmithUEDialogWatcher Instance;
	return Instance;
}

void FSmithUEDialogWatcher::Initialize()
{
	check(IsInGameThread());
	if (bInitialized)
	{
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		ModalTickHandle = FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FSmithUEDialogWatcher::OnModalLoopTick);
	}

	NormalTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FSmithUEDialogWatcher::OnNormalTick), 0.0f);

	bInitialized = true;
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE dialog watcher initialized (modal-loop hook %s)."),
		ModalTickHandle.IsValid() ? TEXT("armed") : TEXT("unavailable"));
}

void FSmithUEDialogWatcher::Shutdown()
{
	check(IsInGameThread());
	if (!bInitialized)
	{
		return;
	}

	if (ModalTickHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetOnModalLoopTickEvent().Remove(ModalTickHandle);
	}
	ModalTickHandle.Reset();

	if (NormalTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NormalTickHandle);
		NormalTickHandle.Reset();
	}

	bInitialized = false;
}

FString FSmithUEDialogWatcher::GetActiveTitle() const
{
	FScopeLock Lock(&StateLock);
	return ActiveTitle;
}

FString FSmithUEDialogWatcher::GetActiveType() const
{
	FScopeLock Lock(&StateLock);
	return ActiveType;
}

TArray<FString> FSmithUEDialogWatcher::GetActiveButtons() const
{
	FScopeLock Lock(&StateLock);
	return ActiveButtons;
}

void FSmithUEDialogWatcher::RequestClickButton(const FString& ButtonText)
{
	{
		FScopeLock Lock(&StateLock);
		PendingClickText = ButtonText;
	}
	bClickRequested.store(true);
}

void FSmithUEDialogWatcher::OnModalLoopTick(float /*DeltaTime*/)
{
	// Runs on the game thread inside the modal message loop.
	bModalActive = true;

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	TSharedPtr<SWindow> Win = FSlateApplication::Get().GetActiveModalWindow();
	if (!Win.IsValid())
	{
		return;
	}

	{
		FScopeLock Lock(&StateLock);
		ActiveTitle = Win->GetTitle().ToString();
		ActiveType = WindowTypeToString(Win->GetType());
	}

	// Collect button labels for reporting + click-by-text support.
	TArray<TPair<TSharedRef<SButton>, FString>> Buttons;
	CollectButtons(Win.ToSharedRef(), Buttons);
	{
		FScopeLock Lock(&StateLock);
		ActiveButtons.Reset();
		for (const TPair<TSharedRef<SButton>, FString>& Pair : Buttons)
		{
			ActiveButtons.Add(Pair.Value);
		}
	}

	// One-shot button click by label takes priority over generic responses.
	if (bClickRequested.load())
	{
		FString ClickText;
		{
			FScopeLock Lock(&StateLock);
			ClickText = PendingClickText;
		}

		TSharedPtr<SButton> Target;
		for (const TPair<TSharedRef<SButton>, FString>& Pair : Buttons) // exact match first
		{
			if (Pair.Value.Equals(ClickText, ESearchCase::IgnoreCase))
			{
				Target = Pair.Key;
				break;
			}
		}
		if (!Target.IsValid())
		{
			for (const TPair<TSharedRef<SButton>, FString>& Pair : Buttons) // then substring
			{
				if (Pair.Value.Contains(ClickText, ESearchCase::IgnoreCase))
				{
					Target = Pair.Key;
					break;
				}
			}
		}

		bClickRequested.store(false);
		if (Target.IsValid())
		{
			UE_LOG(LogSmithUE, Log, TEXT("Dialog watcher: clicking button '%s' on modal '%s'."), *ClickText, *Win->GetTitle().ToString());
			Target->SimulateClick();
			HandledWindow = Win;
			DismissedCounter.Increment();
			return;
		}
		UE_LOG(LogSmithUE, Warning, TEXT("Dialog watcher: no button matching '%s' on modal '%s' (available: %d). Falling through."), *ClickText, *Win->GetTitle().ToString(), Buttons.Num());
	}

	// Determine desired response: one-shot request wins, else persistent auto mode.
	int32 Resp = OneShotResponse.exchange(0);
	if (Resp == 0)
	{
		Resp = AutoResponseMode.load();
	}
	if (Resp == 0)
	{
		return; // observe only
	}

	const bool bAlreadyTried = HandledWindow.IsValid() && HandledWindow.Pin() == Win;

	if (static_cast<EResponse>(Resp) == EResponse::Accept && !bAlreadyTried)
	{
		// First attempt: try to trigger the dialog's default action.
		SendEnterKey(Win.ToSharedRef());
		HandledWindow = Win;
		DismissedCounter.Increment();
	}
	else
	{
		// Cancel, or Accept-fallback when Enter did not close the window last tick.
		Win->RequestDestroyWindow();
		if (!bAlreadyTried)
		{
			DismissedCounter.Increment();
		}
		HandledWindow = Win;
	}
}

bool FSmithUEDialogWatcher::OnNormalTick(float /*DeltaTime*/)
{
	// Runs on the game thread only when NOT inside a modal loop -> clear state.
	if (bModalActive)
	{
		bModalActive = false;
		FScopeLock Lock(&StateLock);
		ActiveTitle.Reset();
		ActiveType.Reset();
		ActiveButtons.Reset();
	}
	HandledWindow.Reset();
	return true; // keep ticking
}

const TCHAR* FSmithUEDialogWatcher::ResponseToString(EResponse R)
{
	switch (R)
	{
	case EResponse::Cancel: return TEXT("cancel");
	case EResponse::Accept: return TEXT("accept");
	default:                return TEXT("off");
	}
}

FSmithUEDialogWatcher::EResponse FSmithUEDialogWatcher::ResponseFromString(const FString& S, bool& bOutValid)
{
	bOutValid = true;
	if (S.Equals(TEXT("cancel"), ESearchCase::IgnoreCase) || S.Equals(TEXT("close"), ESearchCase::IgnoreCase) || S.Equals(TEXT("dismiss"), ESearchCase::IgnoreCase))
	{
		return EResponse::Cancel;
	}
	if (S.Equals(TEXT("accept"), ESearchCase::IgnoreCase) || S.Equals(TEXT("ok"), ESearchCase::IgnoreCase) || S.Equals(TEXT("confirm"), ESearchCase::IgnoreCase))
	{
		return EResponse::Accept;
	}
	if (S.Equals(TEXT("off"), ESearchCase::IgnoreCase) || S.IsEmpty() || S.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		return EResponse::None;
	}
	bOutValid = false;
	return EResponse::None;
}

// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEDialogCommands.h"

#include "Dialog/SmithUEDialogWatcher.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "Dom/JsonObject.h"

using EResp = FSmithUEDialogWatcher::EResponse;

void FSmithUEDialogCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(
		FSmithUEToolSchema(
			TEXT("get_active_dialog"),
			TEXT("Dialog"),
			TEXT("Report whether a blocking modal editor dialog is currently open (title/type), the armed auto-response mode, and how many dialogs SmithUE has auto-dismissed. WORKER-SAFE: this still responds while a modal dialog has jammed the game thread. Read-only, no mutation."),
			{}),
		&FSmithUEDialogCommands::HandleGetActiveDialog);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("dismiss_active_dialog"),
			TEXT("Dialog"),
			TEXT("Close a modal editor dialog that is blocking the game thread (e.g. an unexpected 'Save As'/confirm prompt). WORKER-SAFE. success = QUEUED, not finished: the close is applied on the next modal-loop tick; poll get_active_dialog (modal_active=false) to confirm. button_text clicks a SPECIFIC button by its label (case-insensitive; exact then substring; check get_active_dialog 'buttons') — use this for dialogs whose default button is NOT the one you want (e.g. OkCancel confirms that default to Cancel). response=cancel (default) reliably destroys/closes the window; response=accept is BEST-EFFORT (focus + Enter = default action) and falls back to close if Enter does not dismiss it."),
			{
				FSmithUEToolParam(TEXT("response"), TEXT("string"), TEXT("How to respond: 'cancel' (default, reliably closes) or 'accept' (best-effort default action). Ignored when button_text is provided."), /*required*/ false, TEXT("cancel"))
					.SetAllowedValues({ TEXT("cancel"), TEXT("accept"), TEXT("confirm") }),
				FSmithUEToolParam(TEXT("button_text"), TEXT("string"), TEXT("Click the button whose label matches this text (case-insensitive; exact match preferred, then substring). See get_active_dialog 'buttons' for available labels."), /*required*/ false)
			}),
		&FSmithUEDialogCommands::HandleDismissActiveDialog);

	Registry.Register(
		FSmithUEToolSchema(
			TEXT("set_dialog_auto_response"),
			TEXT("Dialog"),
			TEXT("Arm a persistent auto-responder so ANY modal dialog that opens is answered automatically (prevents automation from hanging on unexpected prompts). WORKER-SAFE — arm this BEFORE running tools that might pop a modal (e.g. level_save on an unsaved level). mode=off (default, disarmed) | cancel (reliably close every modal) | accept (best-effort default action, falls back to close)."),
			{
				FSmithUEToolParam(TEXT("mode"), TEXT("string"), TEXT("Auto-response mode: 'off' (disarm), 'cancel' (auto-close every modal), or 'accept' (best-effort default action)"), /*required*/ true)
					.SetAllowedValues({ TEXT("off"), TEXT("cancel"), TEXT("accept"), TEXT("confirm") })
			}),
		&FSmithUEDialogCommands::HandleSetDialogAutoResponse);
}

TSharedPtr<FJsonObject> FSmithUEDialogCommands::HandleGetActiveDialog(const TSharedPtr<FJsonObject>& /*Params*/)
{
	FSmithUEDialogWatcher& W = FSmithUEDialogWatcher::Get();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const bool bActive = W.IsModalActive();
	Data->SetBoolField(TEXT("modal_active"), bActive);
	Data->SetStringField(TEXT("title"), bActive ? W.GetActiveTitle() : FString());
	Data->SetStringField(TEXT("type"), bActive ? W.GetActiveType() : FString());
	{
		TArray<TSharedPtr<FJsonValue>> ButtonArray;
		if (bActive)
		{
			for (const FString& Label : W.GetActiveButtons())
			{
				ButtonArray.Add(MakeShared<FJsonValueString>(Label));
			}
		}
		Data->SetArrayField(TEXT("buttons"), ButtonArray);
	}
	Data->SetStringField(TEXT("auto_response"), FSmithUEDialogWatcher::ResponseToString(W.GetAutoResponse()));
	Data->SetNumberField(TEXT("dismissed_count"), W.GetDismissedCount());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEDialogCommands::HandleDismissActiveDialog(const TSharedPtr<FJsonObject>& Params)
{
	FString ResponseStr = TEXT("cancel");
	FString ButtonText;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("response"), ResponseStr);
		Params->TryGetStringField(TEXT("button_text"), ButtonText);
	}

	FSmithUEDialogWatcher& W = FSmithUEDialogWatcher::Get();

	// Click-by-label path takes priority over generic responses.
	if (!ButtonText.IsEmpty())
	{
		const int32 ClickCountBefore = W.GetDismissedCount();
		const bool bClickActiveNow = W.IsModalActive();
		W.RequestClickButton(ButtonText);

		TSharedPtr<FJsonObject> ClickData = MakeShared<FJsonObject>();
		ClickData->SetBoolField(TEXT("queued"), true);
		ClickData->SetStringField(TEXT("button_text"), ButtonText);
		ClickData->SetBoolField(TEXT("modal_active"), bClickActiveNow);
		ClickData->SetNumberField(TEXT("dismissed_count_before"), ClickCountBefore);
		ClickData->SetStringField(TEXT("note"), bClickActiveNow
			? TEXT("Button click queued; applied on next modal-loop tick. Poll get_active_dialog until modal_active=false. If no button matches, nothing is clicked (check editor log).")
			: TEXT("No modal dialog is currently active; the click will apply to the next modal that opens."));
		return FSmithUECommonUtils::CreateSuccessResponse(ClickData);
	}

	bool bValid = false;
	EResp Response = FSmithUEDialogWatcher::ResponseFromString(ResponseStr, bValid);
	if (!bValid || Response == EResp::None)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			TEXT("Invalid 'response'. Use 'cancel' (reliably close) or 'accept' (best-effort default action)."));
	}

	const int32 CountBefore = W.GetDismissedCount();
	const bool bActiveNow = W.IsModalActive();
	W.RequestDismiss(Response);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("queued"), true);
	Data->SetStringField(TEXT("response"), FSmithUEDialogWatcher::ResponseToString(Response));
	Data->SetBoolField(TEXT("modal_active"), bActiveNow);
	Data->SetNumberField(TEXT("dismissed_count_before"), CountBefore);
	Data->SetStringField(TEXT("note"), bActiveNow
		? TEXT("Dismissal queued; applied on next modal-loop tick. Poll get_active_dialog until modal_active=false.")
		: TEXT("No modal dialog is currently active; the request will apply to the next modal that opens."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEDialogCommands::HandleSetDialogAutoResponse(const TSharedPtr<FJsonObject>& Params)
{
	FString ModeStr;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("mode"), ModeStr) || ModeStr.IsEmpty())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'mode' (off | cancel | accept)."));
	}

	bool bValid = false;
	EResp Mode = FSmithUEDialogWatcher::ResponseFromString(ModeStr, bValid);
	if (!bValid)
	{
		return FSmithUECommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Invalid 'mode' '%s'. Use 'off', 'cancel', or 'accept'."), *ModeStr));
	}

	FSmithUEDialogWatcher& W = FSmithUEDialogWatcher::Get();
	W.SetAutoResponse(Mode);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mode"), FSmithUEDialogWatcher::ResponseToString(Mode));
	Data->SetBoolField(TEXT("armed"), Mode != EResp::None);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

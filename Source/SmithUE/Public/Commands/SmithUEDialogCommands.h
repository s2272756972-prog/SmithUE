// Copyright 2026, 123dx-svg. MIT License.
#pragma once
#include "CoreMinimal.h"

class FSmithUEToolRegistry;

/**
 * Editor modal-dialog observation & response tools (worker-safe).
 *
 * These handlers ONLY touch FSmithUEDialogWatcher's thread-safe state, so they are
 * dispatched on the HTTP worker thread and keep responding even while a modal
 * dialog blocks the game thread (see FSmithUEDialogWatcher for the rationale).
 */
class FSmithUEDialogCommands
{
public:
	static void RegisterTools(FSmithUEToolRegistry& Registry);

	// Worker-safe: safe to call from the HTTP worker thread OR the game thread.
	static TSharedPtr<FJsonObject> HandleGetActiveDialog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleDismissActiveDialog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> HandleSetDialogAutoResponse(const TSharedPtr<FJsonObject>& Params);
};

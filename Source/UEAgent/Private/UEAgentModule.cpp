// Copyright 2026, 123dx-svg. MIT License.

#include "UEAgentModule.h"
#include "Transport/UEAgentTcpServer.h"
#include "Transport/UEAgentHttpServer.h"

#include "Commands/UEAgentAssetCommands.h"
#include "Commands/UEAgentBlueprintCommands.h"
#include "Commands/UEAgentEditorCommands.h"
#include "Commands/UEAgentInteractionCommands.h"
#include "Commands/UEAgentMaterialCommands.h"
#include "Commands/UEAgentProjectCommands.h"
#include "Blueprint/UEAgentBpAtomicAPI.h"
#include "Commands/UEAgentViewportCommands.h"
#include "Commands/UEAgentObservationCommands.h"
#include "Commands/UEAgentScreenshotCommands.h"
#include "Commands/UEAgentTextureCommands.h"
#include "Commands/UEAgentSourceAnalysisCommands.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "ToolRegistry/UEAgentToolRegistry.h"

DEFINE_LOG_CATEGORY(LogUEAgent);

#define LOCTEXT_NAMESPACE "FUEAgentModule"

void FUEAgentModule::StartupModule()
{
	UE_LOG(LogUEAgent, Log, TEXT("UEAgent module starting up..."));

	FUEAgentToolRegistry::RegisterBuiltinCommands();
	FUEAgentProjectCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentMaterialCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentAssetCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentEditorCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentInteractionCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentBpAtomicAPI::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentBlueprintCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentViewportCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentObservationCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentScreenshotCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentTextureCommands::RegisterTools(FUEAgentToolRegistry::Get());
	FUEAgentSourceAnalysisCommands::RegisterTools(FUEAgentToolRegistry::Get());

	if (GEditor != nullptr)
	{
		GEditor->GetEditorSubsystem<UUEAgentTcpServer>();
		GEditor->GetEditorSubsystem<UUEAgentHttpServer>();
	}
	else
	{
		UE_LOG(LogUEAgent, Warning, TEXT("GEditor is not available yet; UEAgent subsystems will initialize with the editor subsystem collection."));
	}

	UE_LOG(LogUEAgent, Log, TEXT("UEAgent module started successfully."));
}

void FUEAgentModule::ShutdownModule()
{
	UE_LOG(LogUEAgent, Log, TEXT("UEAgent module shutting down..."));
	UE_LOG(LogUEAgent, Log, TEXT("UEAgent module shut down."));
}

FUEAgentModule& FUEAgentModule::Get()
{
	return FModuleManager::GetModuleChecked<FUEAgentModule>("UEAgent");
}

bool FUEAgentModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UEAgent");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEAgentModule, UEAgent)

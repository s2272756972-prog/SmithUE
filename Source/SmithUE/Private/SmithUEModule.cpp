// Copyright 2026, 123dx-svg. MIT License.

#include "SmithUEModule.h"
#include "Transport/SmithUEHttpServer.h"
#include "SmithUESettings.h"
#include "Utils/SmithUEUpdateChecker.h"

#include "Commands/SmithUEAssetCommands.h"
#include "Commands/SmithUEAssetFactoryCommands.h"
#include "Commands/SmithUEBlueprintCommands.h"
#include "Commands/SmithUEEditorCommands.h"
#include "Commands/SmithUEInteractionCommands.h"
#include "Commands/SmithUEMaterialCommands.h"
#include "Commands/SmithUEMaterialFunctionCommands.h"
#include "Commands/SmithUEProjectCommands.h"
#include "Blueprint/SmithUEBpAtomicAPI.h"
#include "Commands/SmithUEViewportCommands.h"
#include "Commands/SmithUEObservationCommands.h"
#include "Commands/SmithUEScreenshotCommands.h"
#include "Commands/SmithUETextureCommands.h"
#include "Commands/SmithUESourceAnalysisCommands.h"
#include "Commands/SmithUENiagaraCommands.h"
#include "Commands/SmithUELevelCommands.h"
#include "Commands/SmithUEDebugCommands.h"
#include "Commands/SmithUEDataCommands.h"
#include "Commands/SmithUESequencerCommands.h"
#include "Commands/SmithUEEnvironmentCommands.h"
#include "Commands/SmithUEPIECommands.h"
#include "Commands/SmithUEAnimCommands.h"
#include "Commands/SmithUEInputCommands.h"
#include "Commands/SmithUEUMGCommands.h"
#include "UI/SSmithUEStatusIndicator.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Containers/Ticker.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

DEFINE_LOG_CATEGORY(LogSmithUE);

#define LOCTEXT_NAMESPACE "FSmithUEModule"

void FSmithUEModule::StartupModule()
{
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE module starting up..."));

	if (IsRunningCommandlet())
	{
		UE_LOG(LogSmithUE, Log, TEXT("SmithUE: Running in commandlet mode, skipping server and UI initialization."));
		return;
	}

	FSmithUEToolRegistry::RegisterBuiltinCommands();
	FSmithUEProjectCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEMaterialCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEMaterialFunctionCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEAssetCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEAssetFactoryCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEEditorCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEInteractionCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEBpAtomicAPI::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEBlueprintCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEViewportCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEObservationCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEScreenshotCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUETextureCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUESourceAnalysisCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUENiagaraCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUELevelCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEDebugCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEDataCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUESequencerCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEEnvironmentCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEPIECommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEAnimCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEInputCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEUMGCommands::RegisterTools(FSmithUEToolRegistry::Get());

	if (GEditor != nullptr)
	{
		GEditor->GetEditorSubsystem<USmithUEHttpServer>();
	}
	else
	{
		UE_LOG(LogSmithUE, Warning, TEXT("GEditor is not available yet; SmithUE subsystems will initialize with the editor subsystem collection."));
	}

	// Register status bar indicator
 	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			UToolMenu* StatusBarMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.StatusBar.ToolBar"));
			if (StatusBarMenu != nullptr)
			{
				FToolMenuSection& Section = StatusBarMenu->FindOrAddSection(TEXT("SmithUE"));
				Section.AddEntry(
					FToolMenuEntry::InitWidget(
						TEXT("SmithUEStatus"),
						SNew(SSmithUEStatusIndicator),
						FText::GetEmpty()
					)
				);
			}
		})
	);

	// Schedule update check 7 seconds after startup (non-blocking), if enabled in Project Settings
	if (const USmithUESettings* SmithSettings = GetDefault<USmithUESettings>())
	{
		if (SmithSettings->bCheckForUpdatesOnStartup)
		{
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([](float DeltaTime) -> bool
				{
					FSmithUEUpdateChecker::OnUpdateCheckComplete.AddLambda([]()
					{
						if (FSmithUEUpdateChecker::IsUpdateAvailable())
						{
							FSmithUEUpdateChecker::ShowUpdateNotification();
						}
					});
					FSmithUEUpdateChecker::CheckForUpdate();
					return false; // one-shot
				}),
				7.0f
			);
		}
	}

	UE_LOG(LogSmithUE, Log, TEXT("SmithUE module started successfully."));
}

void FSmithUEModule::ShutdownModule()
{
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE module shutting down..."));

	if (UObjectInitialized() && UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->RemoveSection(TEXT("LevelEditor.StatusBar.ToolBar"), TEXT("SmithUE"));
	}

	UE_LOG(LogSmithUE, Log, TEXT("SmithUE module shut down."));
}

FSmithUEModule& FSmithUEModule::Get()
{
	return FModuleManager::GetModuleChecked<FSmithUEModule>("SmithUE");
}

bool FSmithUEModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("SmithUE");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSmithUEModule, SmithUE)

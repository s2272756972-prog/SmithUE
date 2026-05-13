// Copyright 2026, 123dx-svg. MIT License.

#include "SmithUEModule.h"
#include "Transport/SmithUETcpServer.h"
#include "Transport/SmithUEHttpServer.h"

#include "Commands/SmithUEAssetCommands.h"
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
#include "UI/SSmithUEStatusIndicator.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ToolRegistry/SmithUEToolRegistry.h"

DEFINE_LOG_CATEGORY(LogSmithUE);

#define LOCTEXT_NAMESPACE "FSmithUEModule"

void FSmithUEModule::StartupModule()
{
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE module starting up..."));

	FSmithUEToolRegistry::RegisterBuiltinCommands();
	FSmithUEProjectCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEMaterialCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEMaterialFunctionCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEAssetCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEEditorCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEInteractionCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEBpAtomicAPI::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEBlueprintCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEViewportCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEObservationCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUEScreenshotCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUETextureCommands::RegisterTools(FSmithUEToolRegistry::Get());
	FSmithUESourceAnalysisCommands::RegisterTools(FSmithUEToolRegistry::Get());

	if (GEditor != nullptr)
	{
		GEditor->GetEditorSubsystem<USmithUETcpServer>();
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

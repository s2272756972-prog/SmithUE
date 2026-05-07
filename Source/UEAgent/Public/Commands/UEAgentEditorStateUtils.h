// Copyright 2026, 123dx-svg. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "LevelEditor.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Utils/UEAgentCommonUtils.h"

namespace UEAgentEditorState
{
	inline bool IsInPIE()
	{
		return GEditor && (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld != nullptr);
	}

	inline FEditorViewportClient* GetActiveViewportClient()
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (!LevelEditorModule)
		{
			return nullptr;
		}

		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule->GetFirstActiveViewport();
		if (!ActiveViewport.IsValid())
		{
			return nullptr;
		}

		return static_cast<FEditorViewportClient*>(&ActiveViewport->GetAssetViewportClient());
	}

	inline TArray<FString> GetKnownPanelNames()
	{
		return {
			TEXT("ContentBrowserTab1"),
			TEXT("OutputLog"),
			TEXT("LevelEditorSelectionDetails"),
			TEXT("WorldOutliner"),
			TEXT("LevelEditorToolBox"),
			TEXT("PlacementBrowser")
		};
	}

	inline bool IsModalDialogOpen()
	{
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid();
	}

	inline TSharedPtr<FJsonObject> CreatePIEErrorResponse()
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Command not available during Play-In-Editor mode"));
	}

	inline TSharedPtr<FJsonObject> CreateNoViewportErrorResponse()
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("No active level editor viewport found"));
	}

}

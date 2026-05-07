// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/UEAgentObservationCommands.h"
#include "ToolRegistry/UEAgentToolRegistry.h"
#include "ToolRegistry/UEAgentToolSchema.h"
#include "Utils/UEAgentCommonUtils.h"
#include "Commands/UEAgentEditorStateUtils.h"
#include "Framework/Docking/TabManager.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FUEAgentObservationCommands::RegisterTools(FUEAgentToolRegistry& Registry)
{
	// list_panels
	Registry.Register(
		FUEAgentToolSchema(TEXT("list_panels"), TEXT("Observation"),
			TEXT("Lists all known editor panels and whether each is currently open.")),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleListPanels(Params); });

	// open_panel
	Registry.Register(
		FUEAgentToolSchema(TEXT("open_panel"), TEXT("Observation"),
			TEXT("Opens (or focuses) a named editor panel tab."),
			{
				FUEAgentToolParam(TEXT("panel_name"), TEXT("string"),
					TEXT("Tab ID of the panel to open, e.g. ContentBrowserTab1"), true)
			}),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleOpenPanel(Params); });

	// close_panel
	Registry.Register(
		FUEAgentToolSchema(TEXT("close_panel"), TEXT("Observation"),
			TEXT("Closes a named editor panel tab if it is open."),
			{
				FUEAgentToolParam(TEXT("panel_name"), TEXT("string"),
					TEXT("Tab ID of the panel to close, e.g. OutputLog"), true)
			}),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleClosePanel(Params); });

	// get_editor_state
	Registry.Register(
		FUEAgentToolSchema(TEXT("get_editor_state"), TEXT("Observation"),
			TEXT("Returns a snapshot of the current editor state: PIE, simulation, selection, level, viewport.")),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleGetEditorState(Params); });

	// get_level_info
	Registry.Register(
		FUEAgentToolSchema(TEXT("get_level_info"), TEXT("Observation"),
			TEXT("Returns information about the currently loaded level/world.")),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleGetLevelInfo(Params); });

	// get_actor_property
	Registry.Register(
		FUEAgentToolSchema(
			TEXT("get_actor_property"),
			TEXT("Observation"),
			TEXT("Read a reflected property value from an actor by label"),
			{
				FUEAgentToolParam(TEXT("actor_label"),   TEXT("string"), TEXT("Actor label"), true),
				FUEAgentToolParam(TEXT("property_name"), TEXT("string"), TEXT("Property name"), true)
			}),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleGetActorProperty(Params); });

	// get_selected_actors
	Registry.Register(
		FUEAgentToolSchema(TEXT("get_selected_actors"), TEXT("Observation"),
			TEXT("Returns the currently selected actors with label, class, location, and rotation."),
			{}),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleGetSelectedActors(Params); });

	// get_world_outline
	Registry.Register(
		FUEAgentToolSchema(TEXT("get_world_outline"), TEXT("Observation"),
			TEXT("Returns all actors in the level with parent-child hierarchy and folder info."),
			{
				FUEAgentToolParam(TEXT("filter"), TEXT("string"), TEXT("Optional substring filter on actor label")),
				FUEAgentToolParam(TEXT("max_depth"), TEXT("number"), TEXT("Ignored in v1 (flat list returned)"))
			}),
		[](const TSharedPtr<FJsonObject>& Params) { return HandleGetWorldOutline(Params); });
}

// ---------------------------------------------------------------------------
// HandleListPanels
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleListPanels(const TSharedPtr<FJsonObject>& Params)
{
	TArray<FString> KnownPanels = UEAgentEditorState::GetKnownPanelNames();

	TArray<TSharedPtr<FJsonValue>> PanelArray;
	for (const FString& PanelName : KnownPanels)
	{
		TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(
			FTabId(FName(*PanelName)));

		TSharedPtr<FJsonObject> PanelObj = MakeShareable(new FJsonObject());
		PanelObj->SetStringField(TEXT("name"), PanelName);
		PanelObj->SetBoolField(TEXT("is_open"), Tab.IsValid());
		PanelArray.Add(MakeShareable(new FJsonValueObject(PanelObj)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetArrayField(TEXT("panels"), PanelArray);
	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleOpenPanel
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleOpenPanel(const TSharedPtr<FJsonObject>& Params)
{
	if (UEAgentEditorState::IsInPIE())
	{
		return UEAgentEditorState::CreatePIEErrorResponse();
	}

	FString PanelName;
	if (!Params->TryGetStringField(TEXT("panel_name"), PanelName) || PanelName.IsEmpty())
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: panel_name"));
	}

	TArray<FString> KnownPanels = UEAgentEditorState::GetKnownPanelNames();
	if (!KnownPanels.Contains(PanelName))
	{
		return FUEAgentCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown panel: '%s'. Use list_panels to see valid panel names."), *PanelName));
	}

	TSharedPtr<SDockTab> ExistingTab = FGlobalTabmanager::Get()->FindExistingLiveTab(
		FTabId(FName(*PanelName)));
	bool bAlreadyOpen = ExistingTab.IsValid();

	TSharedPtr<SDockTab> OpenedTab = FGlobalTabmanager::Get()->TryInvokeTab(
		FTabId(FName(*PanelName)));

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetBoolField(TEXT("opened"), OpenedTab.IsValid());
	Data->SetStringField(TEXT("panel_name"), PanelName);
	Data->SetBoolField(TEXT("already_open"), bAlreadyOpen);
	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleClosePanel
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleClosePanel(const TSharedPtr<FJsonObject>& Params)
{
	if (UEAgentEditorState::IsInPIE())
	{
		return UEAgentEditorState::CreatePIEErrorResponse();
	}

	FString PanelName;
	if (!Params->TryGetStringField(TEXT("panel_name"), PanelName) || PanelName.IsEmpty())
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required parameter: panel_name"));
	}

	TArray<FString> KnownPanels = UEAgentEditorState::GetKnownPanelNames();
	if (!KnownPanels.Contains(PanelName))
	{
		return FUEAgentCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown panel: '%s'. Use list_panels to see valid panel names."), *PanelName));
	}

	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(
		FTabId(FName(*PanelName)));

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetStringField(TEXT("panel_name"), PanelName);

	if (!Tab.IsValid())
	{
		Data->SetBoolField(TEXT("closed"), true);
		Data->SetBoolField(TEXT("already_closed"), true);
	}
	else
	{
		Tab->RequestCloseTab();
		Data->SetBoolField(TEXT("closed"), true);
		Data->SetBoolField(TEXT("already_closed"), false);
	}

	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleGetEditorState
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleGetEditorState(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
	}

	bool bIsPIE = UEAgentEditorState::IsInPIE();
	bool bIsSimulating = GEditor->bIsSimulatingInEditor;
	bool bModalOpen = UEAgentEditorState::IsModalDialogOpen();
	bool bViewportAvailable = UEAgentEditorState::GetActiveViewportClient() != nullptr;

	int32 SelectedActorCount = 0;
	if (GEditor->GetSelectedActors())
	{
		SelectedActorCount = GEditor->GetSelectedActors()->Num();
	}

	FString ActiveLevel = TEXT("Unknown");
	int32 ActorCount = 0;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		ActiveLevel = World->GetMapName();

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++ActorCount;
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetBoolField(TEXT("is_pie"), bIsPIE);
	Data->SetBoolField(TEXT("is_simulating"), bIsSimulating);
	Data->SetBoolField(TEXT("modal_dialog_open"), bModalOpen);
	Data->SetNumberField(TEXT("selected_actor_count"), SelectedActorCount);
	Data->SetStringField(TEXT("active_level"), ActiveLevel);
	Data->SetNumberField(TEXT("actor_count"), ActorCount);
	Data->SetBoolField(TEXT("viewport_available"), bViewportAvailable);
	if (FEditorViewportClient* C = UEAgentEditorState::GetActiveViewportClient()) {
		const FVector L = C->GetViewLocation(); const FRotator R = C->GetViewRotation(); TSharedPtr<FJsonObject> VP = MakeShareable(new FJsonObject()), Loc = MakeShareable(new FJsonObject()), Rot = MakeShareable(new FJsonObject());
		VP->SetStringField(TEXT("type"), C->GetViewportType() == LVT_Perspective ? TEXT("perspective") : TEXT("ortho"));
		Loc->SetNumberField(TEXT("x"), L.X); Loc->SetNumberField(TEXT("y"), L.Y); Loc->SetNumberField(TEXT("z"), L.Z); Rot->SetNumberField(TEXT("pitch"), R.Pitch); Rot->SetNumberField(TEXT("yaw"), R.Yaw); Rot->SetNumberField(TEXT("roll"), R.Roll);
		VP->SetObjectField(TEXT("location"), Loc); VP->SetObjectField(TEXT("rotation"), Rot); Data->SetObjectField(TEXT("active_viewport"), VP);
	}
	if (GEditor->GetSelectedActors()) { TArray<AActor*> Sel; GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Sel); TArray<TSharedPtr<FJsonValue>> Arr; for (AActor* A : Sel) if (A) Arr.Add(MakeShareable(new FJsonValueString(A->GetActorLabel()))); Data->SetArrayField(TEXT("selected_actors"), Arr); }
	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleGetLevelInfo
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleGetLevelInfo(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
	}

	FString LevelName = World->GetMapName();
	FString MapPath = World->GetPathName();

	// Strip the streaming level suffix (e.g. "UEDPIE_0_") if present
	if (LevelName.StartsWith(TEXT("UEDPIE_")))
	{
		int32 UnderscoreIdx = INDEX_NONE;
		LevelName.FindLastChar(TEXT('_'), UnderscoreIdx);
		if (UnderscoreIdx != INDEX_NONE)
		{
			LevelName = LevelName.RightChop(UnderscoreIdx + 1);
		}
	}

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++ActorCount;
	}

	FString WorldTypeStr;
	switch (World->WorldType)
	{
		case EWorldType::Editor:       WorldTypeStr = TEXT("Editor");       break;
		case EWorldType::PIE:          WorldTypeStr = TEXT("PIE");          break;
		case EWorldType::Game:         WorldTypeStr = TEXT("Game");         break;
		case EWorldType::EditorPreview:WorldTypeStr = TEXT("EditorPreview");break;
		case EWorldType::GamePreview:  WorldTypeStr = TEXT("GamePreview");  break;
		case EWorldType::Inactive:     WorldTypeStr = TEXT("Inactive");     break;
		default:                       WorldTypeStr = TEXT("Unknown");      break;
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetStringField(TEXT("level_name"), LevelName);
	Data->SetStringField(TEXT("map_path"), MapPath);
	Data->SetNumberField(TEXT("actor_count"), ActorCount);
	Data->SetStringField(TEXT("world_type"), WorldTypeStr);
	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleGetActorProperty
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleGetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Missing required param: 'property_name'"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It && ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel))
		{
			Actor = *It;
			break;
		}
	}
	if (!Actor)
	{
		return FUEAgentCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
	}

	// Try actor first, then fall through to components
	FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
	UObject* PropOwner = Actor;
	if (!Prop)
	{
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp) continue;
			Prop = Comp->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (Prop) { PropOwner = Comp; break; }
		}
	}
	if (!Prop)
	{
		return FUEAgentCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Property not found: '%s' on actor '%s'"),
				*PropertyName, *ActorLabel));
	}

	const void* PropAddr = Prop->ContainerPtrToValuePtr<void>(PropOwner);

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetStringField(TEXT("actor_label"), ActorLabel);
	Data->SetStringField(TEXT("property_name"), PropertyName);

	if (Prop->IsA<FBoolProperty>())
	{
		Data->SetBoolField(TEXT("value"), CastField<FBoolProperty>(Prop)->GetPropertyValue(PropAddr));
		Data->SetStringField(TEXT("type"), TEXT("bool"));
	}
	else if (Prop->IsA<FIntProperty>())
	{
		Data->SetNumberField(TEXT("value"), CastField<FIntProperty>(Prop)->GetPropertyValue(PropAddr));
		Data->SetStringField(TEXT("type"), TEXT("int"));
	}
	else if (Prop->IsA<FFloatProperty>())
	{
		Data->SetNumberField(TEXT("value"), CastField<FFloatProperty>(Prop)->GetPropertyValue(PropAddr));
		Data->SetStringField(TEXT("type"), TEXT("float"));
	}
	else if (Prop->IsA<FDoubleProperty>())
	{
		Data->SetNumberField(TEXT("value"), CastField<FDoubleProperty>(Prop)->GetPropertyValue(PropAddr));
		Data->SetStringField(TEXT("type"), TEXT("float"));
	}
	else if (Prop->IsA<FStrProperty>())
	{
		Data->SetStringField(TEXT("value"), CastField<FStrProperty>(Prop)->GetPropertyValue(PropAddr));
		Data->SetStringField(TEXT("type"), TEXT("string"));
	}
	else if (Prop->IsA<FNameProperty>())
	{
		Data->SetStringField(TEXT("value"), CastField<FNameProperty>(Prop)->GetPropertyValue(PropAddr).ToString());
		Data->SetStringField(TEXT("type"), TEXT("string"));
	}
	else if (Prop->IsA<FTextProperty>())
	{
		Data->SetStringField(TEXT("value"), CastField<FTextProperty>(Prop)->GetPropertyValue(PropAddr).ToString());
		Data->SetStringField(TEXT("type"), TEXT("string"));
	}
	else if (Prop->IsA<FStructProperty>())
	{
		const FStructProperty* SP = CastField<FStructProperty>(Prop);
		const FName SN = SP->Struct->GetFName();
		static const FName OK[] = { TEXT("Vector"), TEXT("Rotator"), TEXT("Color"), TEXT("LinearColor"), TEXT("Vector2D"), TEXT("Vector4"), TEXT("IntPoint"), TEXT("Quat") };
		bool bOK = false;
		for (const FName& N : OK) { if (SN == N) { bOK = true; break; } }
		if (!bOK)
			return FUEAgentCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Struct '%s' not supported for '%s'"), *SN.ToString(), *PropertyName));
		FString ExportedStr;
		Prop->ExportTextItem_Direct(ExportedStr, PropAddr, nullptr, Actor, PPF_None);
		Data->SetStringField(TEXT("value"), ExportedStr);
		Data->SetStringField(TEXT("type"), TEXT("struct"));
	}
	else
	{
		return FUEAgentCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unsupported property type for '%s'"), *PropertyName));
	}

	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor || !GEditor->GetSelectedActors())
		return FUEAgentCommonUtils::CreateErrorResponse(TEXT("GEditor not available"));

	TArray<AActor*> Selected;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Selected);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (AActor* Actor : Selected)
	{
		if (!Actor) continue;
		FVector Loc = Actor->GetActorLocation();
		FRotator Rot = Actor->GetActorRotation();
		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject());
		LocObj->SetNumberField(TEXT("x"), Loc.X); LocObj->SetNumberField(TEXT("y"), Loc.Y); LocObj->SetNumberField(TEXT("z"), Loc.Z);
		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject());
		RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch); RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw); RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
		Entry->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Entry->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		Entry->SetObjectField(TEXT("location"), LocObj);
		Entry->SetObjectField(TEXT("rotation"), RotObj);
		Arr.Add(MakeShareable(new FJsonValueObject(Entry)));
	}
	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetArrayField(TEXT("selected"), Arr);
	Data->SetNumberField(TEXT("count"), Selected.Num());
	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUEAgentObservationCommands::HandleGetWorldOutline(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return FUEAgentCommonUtils::CreateErrorResponse(TEXT("GEditor not available"));
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return FUEAgentCommonUtils::CreateErrorResponse(TEXT("No editor world available"));

	FString Filter;
	Params->TryGetStringField(TEXT("filter"), Filter);

	TMap<AActor*, TArray<FString>> ChildrenMap;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (AActor* Parent = (*It)->GetAttachParentActor())
			ChildrenMap.FindOrAdd(Parent).Add((*It)->GetActorLabel());
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	int32 Total = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		++Total;
		FString Label = Actor->GetActorLabel();
		if (!Filter.IsEmpty() && !Label.Contains(Filter)) continue;

		FString ParentLabel;
		if (AActor* P = Actor->GetAttachParentActor()) ParentLabel = P->GetActorLabel();

		TArray<TSharedPtr<FJsonValue>> ChildArr;
		if (TArray<FString>* Kids = ChildrenMap.Find(Actor))
			for (const FString& Kid : *Kids)
				ChildArr.Add(MakeShareable(new FJsonValueString(Kid)));

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
		Entry->SetStringField(TEXT("label"), Label);
		Entry->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		Entry->SetStringField(TEXT("parent_label"), ParentLabel);
		Entry->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());
		Entry->SetArrayField(TEXT("children"), ChildArr);
		Arr.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetArrayField(TEXT("actors"), Arr);
	Data->SetNumberField(TEXT("total_count"), Total);
	return FUEAgentCommonUtils::CreateSuccessResponse(Data);
}

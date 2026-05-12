// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEViewportCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "Commands/SmithUEEditorStateUtils.h"
#include "SmithUEModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "SLevelViewport.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/SlateApplication.h"
#include "Windows/WindowsHWrapper.h"
#include <Windows.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    /** Force editor window to foreground (Win32 hack) and redraw viewports. */
    void ForceViewportRedraw()
    {
        // Win32: force foreground window via AttachThreadInput trick
        if (FSlateApplication::IsInitialized())
        {
            TSharedPtr<SWindow> MainWindow = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
            if (!MainWindow.IsValid())
            {
                const TArray<TSharedRef<SWindow>>& AllWindows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
                if (AllWindows.Num() > 0) { MainWindow = AllWindows[0]; }
            }
            if (MainWindow.IsValid())
            {
                TSharedPtr<FGenericWindow> NativeWindow = MainWindow->GetNativeWindow();
                if (NativeWindow.IsValid())
                {
                    HWND Hwnd = static_cast<HWND>(NativeWindow->GetOSWindowHandle());
                    if (Hwnd)
                    {
                        DWORD ForegroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
                        DWORD CurrentThread = GetCurrentThreadId();
                        if (ForegroundThread != CurrentThread)
                        {
                            AttachThreadInput(ForegroundThread, CurrentThread, 1);
                        }
                        SetForegroundWindow(Hwnd);
                        if (ForegroundThread != CurrentThread)
                        {
                            AttachThreadInput(ForegroundThread, CurrentThread, 0);
                        }
                    }
                }
            }
        }
        if (GEditor) { GEditor->RedrawLevelEditingViewports(true); }
    }

    TSharedPtr<FJsonObject> MakeLocationJson(const FVector& V)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("x"), V.X);
        Obj->SetNumberField(TEXT("y"), V.Y);
        Obj->SetNumberField(TEXT("z"), V.Z);
        return Obj;
    }

    TSharedPtr<FJsonObject> MakeRotationJson(const FRotator& R)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("pitch"), R.Pitch);
        Obj->SetNumberField(TEXT("yaw"),   R.Yaw);
        Obj->SetNumberField(TEXT("roll"),  R.Roll);
        return Obj;
    }

    FString ViewportTypeToString(ELevelViewportType Type)
    {
        switch (Type)
        {
            case LVT_Perspective:      return TEXT("perspective");
            case LVT_OrthoXY:          return TEXT("top");
            case LVT_OrthoNegativeXY:  return TEXT("bottom");
            case LVT_OrthoXZ:          return TEXT("front");
            case LVT_OrthoNegativeXZ:  return TEXT("back");
            case LVT_OrthoYZ:          return TEXT("left");
            case LVT_OrthoNegativeYZ:  return TEXT("right");
            default:                   return TEXT("unknown");
        }
    }
} // namespace

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUEViewportCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    // set_viewport_camera
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_viewport_camera"),
            TEXT("Viewport"),
            TEXT("Set the active editor viewport camera location, rotation, and/or FOV"),
            {
                FSmithUEToolParam(TEXT("location"), TEXT("object"), TEXT("Camera location {x,y,z}")),
                FSmithUEToolParam(TEXT("rotation"), TEXT("object"), TEXT("Camera rotation {pitch,yaw,roll}")),
                FSmithUEToolParam(TEXT("fov"),      TEXT("number"), TEXT("Field of view in degrees"))
            }),
        &HandleSetViewportCamera);

    // focus_on_actor
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("focus_on_actor"),
            TEXT("Viewport"),
            TEXT("Move the active viewport camera to focus on a named actor"),
            {
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label or name"), true)
            }),
        &HandleFocusOnActor);

    // set_viewport_mode
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("set_viewport_mode"),
            TEXT("Viewport"),
            TEXT("Set the active viewport projection mode (perspective or orthographic)"),
            {
                FSmithUEToolParam(TEXT("mode"), TEXT("string"),
                    TEXT("Viewport mode: perspective, top, bottom, left, right, front, back"), true)
            }),
        &HandleSetViewportMode);

    // get_viewport_info_detailed
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("get_viewport_info_detailed"),
            TEXT("Viewport"),
            TEXT("Get detailed active viewport info: camera, size, realtime state, view mode"),
            {}),
        &HandleGetViewportInfoDetailed);

    // select_actors
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("select_actors"),
            TEXT("Viewport"),
            TEXT("Select one or more actors in the level by label"),
            {
                FSmithUEToolParam(TEXT("actor_labels"),    TEXT("array"), TEXT("Array of actor labels to select"), true),
                FSmithUEToolParam(TEXT("add_to_selection"), TEXT("boolean"), TEXT("If true, add to current selection; otherwise replace it"))
            }),
        &HandleSelectActors);
}

// ---------------------------------------------------------------------------
// Command 1: set_viewport_camera
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEViewportCommands::HandleSetViewportCamera(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    FEditorViewportClient* Client = SmithUEEditorState::GetActiveViewportClient();
    if (!Client)
    {
        return SmithUEEditorState::CreateNoViewportErrorResponse();
    }

    const bool bHasLocation = Params->HasField(TEXT("location"));
    const bool bHasRotation = Params->HasField(TEXT("rotation"));
    const bool bHasFov      = Params->HasField(TEXT("fov"));

    if (!bHasLocation && !bHasRotation && !bHasFov)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            TEXT("At least one of 'location', 'rotation', or 'fov' must be provided"));
    }

    // Capture before state
    const FVector  BeforeLoc = Client->GetViewLocation();
    const FRotator BeforeRot = Client->GetViewRotation();
    const float    BeforeFov = Client->ViewFOV;

    // Apply new values
    if (bHasLocation)
    {
        Client->SetViewLocation(VectorFromJsonField(Params, TEXT("location"), BeforeLoc));
    }
    if (bHasRotation)
    {
        Client->SetViewRotation(RotatorFromJsonField(Params, TEXT("rotation"), BeforeRot));
    }
    if (bHasFov)
    {
        double NewFov = BeforeFov;
        Params->TryGetNumberField(TEXT("fov"), NewFov);
        Client->ViewFOV = static_cast<float>(NewFov);
    }

    Client->Invalidate();
    ForceViewportRedraw();

    // Capture after state
    const FVector  AfterLoc = Client->GetViewLocation();
    const FRotator AfterRot = Client->GetViewRotation();
    const float    AfterFov = Client->ViewFOV;

    TSharedPtr<FJsonObject> BeforeObj = MakeShared<FJsonObject>();
    BeforeObj->SetObjectField(TEXT("location"), MakeLocationJson(BeforeLoc));
    BeforeObj->SetObjectField(TEXT("rotation"), MakeRotationJson(BeforeRot));
    BeforeObj->SetNumberField(TEXT("fov"), BeforeFov);

    TSharedPtr<FJsonObject> AfterObj = MakeShared<FJsonObject>();
    AfterObj->SetObjectField(TEXT("location"), MakeLocationJson(AfterLoc));
    AfterObj->SetObjectField(TEXT("rotation"), MakeRotationJson(AfterRot));
    AfterObj->SetNumberField(TEXT("fov"), AfterFov);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetObjectField(TEXT("before"), BeforeObj);
    Data->SetObjectField(TEXT("after"),  AfterObj);

    UE_LOG(LogSmithUE, Log, TEXT("set_viewport_camera: camera updated"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 2: focus_on_actor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEViewportCommands::HandleFocusOnActor(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel) || ActorLabel.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_label'"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
    }

    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor not available"));
    }

    GEditor->MoveViewportCamerasToActor(*Actor, /*bActiveViewportOnly=*/true);

    // Force viewport redraw
    ForceViewportRedraw();

    UE_LOG(LogSmithUE, Log, TEXT("focus_on_actor: focused on '%s'"), *ActorLabel);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("focused"), true);
    Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 3: set_viewport_mode
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEViewportCommands::HandleSetViewportMode(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    FString ModeStr;
    if (!Params->TryGetStringField(TEXT("mode"), ModeStr) || ModeStr.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'mode'"));
    }

    ModeStr.ToLowerInline();

    ELevelViewportType NewType = LVT_Perspective;
    if      (ModeStr == TEXT("perspective")) { NewType = LVT_Perspective;     }
    else if (ModeStr == TEXT("top"))         { NewType = LVT_OrthoXY;         }
    else if (ModeStr == TEXT("bottom"))      { NewType = LVT_OrthoNegativeXY; }
    else if (ModeStr == TEXT("front"))       { NewType = LVT_OrthoXZ;         }
    else if (ModeStr == TEXT("back"))        { NewType = LVT_OrthoNegativeXZ; }
    else if (ModeStr == TEXT("left"))        { NewType = LVT_OrthoYZ;         }
    else if (ModeStr == TEXT("right"))       { NewType = LVT_OrthoNegativeYZ; }
    else
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown viewport mode: '%s'. Valid: perspective, top, bottom, front, back, left, right"), *ModeStr));
    }

    FEditorViewportClient* Client = SmithUEEditorState::GetActiveViewportClient();
    if (!Client)
    {
        return SmithUEEditorState::CreateNoViewportErrorResponse();
    }

    const FString BeforeMode = ViewportTypeToString(Client->GetViewportType());

    Client->SetViewportType(NewType);
    Client->Invalidate();
    ForceViewportRedraw();

    UE_LOG(LogSmithUE, Log, TEXT("set_viewport_mode: %s -> %s"), *BeforeMode, *ModeStr);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("before_mode"), BeforeMode);
    Data->SetStringField(TEXT("after_mode"),  ModeStr);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 4: get_viewport_info_detailed
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEViewportCommands::HandleGetViewportInfoDetailed(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    FEditorViewportClient* Client = SmithUEEditorState::GetActiveViewportClient();
    if (!Client)
    {
        return SmithUEEditorState::CreateNoViewportErrorResponse();
    }

    const FVector  ViewLoc = Client->GetViewLocation();
    const FRotator ViewRot = Client->GetViewRotation();
    const float    FOV     = Client->ViewFOV;
    const bool     bRealtime = Client->IsRealtime();
    const int32    ViewModeIndex = static_cast<int32>(Client->GetViewMode());
    const FString  ViewportMode = ViewportTypeToString(Client->GetViewportType());

    TSharedPtr<FJsonObject> SizeObj = MakeShared<FJsonObject>();
    if (Client->Viewport)
    {
        const FIntPoint Size = Client->Viewport->GetSizeXY();
        SizeObj->SetNumberField(TEXT("width"),  Size.X);
        SizeObj->SetNumberField(TEXT("height"), Size.Y);
    }
    else
    {
        SizeObj->SetNumberField(TEXT("width"),  0);
        SizeObj->SetNumberField(TEXT("height"), 0);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetObjectField(TEXT("location"),      MakeLocationJson(ViewLoc));
    Data->SetObjectField(TEXT("rotation"),      MakeRotationJson(ViewRot));
    Data->SetNumberField(TEXT("fov"),           FOV);
    Data->SetObjectField(TEXT("viewport_size"), SizeObj);
    Data->SetBoolField(TEXT("is_realtime"),     bRealtime);
    Data->SetNumberField(TEXT("view_mode_index"), ViewModeIndex);
    Data->SetStringField(TEXT("viewport_mode"), ViewportMode);

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// Command 5: select_actors
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEViewportCommands::HandleSelectActors(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor not available"));
    }

    const TArray<TSharedPtr<FJsonValue>>* LabelsArray = nullptr;
    if (!Params->TryGetArrayField(TEXT("actor_labels"), LabelsArray) || !LabelsArray || LabelsArray->IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required param: 'actor_labels' (non-empty array)"));
    }

    bool bAddToSelection = false;
    Params->TryGetBoolField(TEXT("add_to_selection"), bAddToSelection);

    const FScopedTransaction Transaction(FText::FromString(TEXT("SmithUE: Select Actors")));

    if (!bAddToSelection)
    {
        GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true, /*bWarnAboutManyActors=*/false);
    }

    TArray<TSharedPtr<FJsonValue>> SelectedLabels;
    TArray<TSharedPtr<FJsonValue>> NotFoundLabels;

    for (const TSharedPtr<FJsonValue>& LabelVal : *LabelsArray)
    {
        const FString Label = LabelVal->AsString();
        if (Label.IsEmpty())
        {
            continue;
        }

        AActor* Actor = FindActorByLabel(Label);
        if (!Actor)
        {
            NotFoundLabels.Add(MakeShared<FJsonValueString>(Label));
            continue;
        }

        GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false, /*bSelectEvenIfHidden=*/true);
        SelectedLabels.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
    }

    GEditor->NoteSelectionChange();

    UE_LOG(LogSmithUE, Log, TEXT("select_actors: selected %d, not found %d"),
        SelectedLabels.Num(), NotFoundLabels.Num());

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("selected"),  SelectedLabels);
    Data->SetArrayField(TEXT("not_found"), NotFoundLabels);
    Data->SetNumberField(TEXT("count"),    SelectedLabels.Num());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

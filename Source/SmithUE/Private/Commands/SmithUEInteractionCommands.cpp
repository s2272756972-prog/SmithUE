// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUEInteractionCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "Commands/SmithUEEditorStateUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "InputCoreTypes.h"
#include "LevelEditor.h"
#include "Editor.h"

namespace
{
    bool TryResolveKey(const FString& KeyName, FKey& OutKey)
    {
        static const TMap<FString, FKey> SupportedKeys = {
            // Letters
            {TEXT("A"), EKeys::A}, {TEXT("B"), EKeys::B}, {TEXT("C"), EKeys::C},
            {TEXT("D"), EKeys::D}, {TEXT("E"), EKeys::E}, {TEXT("F"), EKeys::F},
            {TEXT("G"), EKeys::G}, {TEXT("H"), EKeys::H}, {TEXT("I"), EKeys::I},
            {TEXT("J"), EKeys::J}, {TEXT("K"), EKeys::K}, {TEXT("L"), EKeys::L},
            {TEXT("M"), EKeys::M}, {TEXT("N"), EKeys::N}, {TEXT("O"), EKeys::O},
            {TEXT("P"), EKeys::P}, {TEXT("Q"), EKeys::Q}, {TEXT("R"), EKeys::R},
            {TEXT("S"), EKeys::S}, {TEXT("T"), EKeys::T}, {TEXT("U"), EKeys::U},
            {TEXT("V"), EKeys::V}, {TEXT("W"), EKeys::W}, {TEXT("X"), EKeys::X},
            {TEXT("Y"), EKeys::Y}, {TEXT("Z"), EKeys::Z},
            // Function keys
            {TEXT("F1"), EKeys::F1}, {TEXT("F2"), EKeys::F2}, {TEXT("F3"), EKeys::F3},
            {TEXT("F4"), EKeys::F4}, {TEXT("F5"), EKeys::F5}, {TEXT("F6"), EKeys::F6},
            {TEXT("F7"), EKeys::F7}, {TEXT("F8"), EKeys::F8}, {TEXT("F9"), EKeys::F9},
            {TEXT("F10"), EKeys::F10}, {TEXT("F11"), EKeys::F11}, {TEXT("F12"), EKeys::F12},
            // Navigation
            {TEXT("Delete"), EKeys::Delete}, {TEXT("BackSpace"), EKeys::BackSpace},
            {TEXT("Enter"), EKeys::Enter}, {TEXT("Escape"), EKeys::Escape},
            {TEXT("Tab"), EKeys::Tab}, {TEXT("Space"), EKeys::SpaceBar},
            {TEXT("LeftArrow"), EKeys::Left}, {TEXT("RightArrow"), EKeys::Right},
            {TEXT("UpArrow"), EKeys::Up}, {TEXT("DownArrow"), EKeys::Down},
            {TEXT("Home"), EKeys::Home}, {TEXT("End"), EKeys::End},
            {TEXT("PageUp"), EKeys::PageUp}, {TEXT("PageDown"), EKeys::PageDown},
            // Numpad
            {TEXT("Num0"), EKeys::NumPadZero}, {TEXT("Num1"), EKeys::NumPadOne},
            {TEXT("Num2"), EKeys::NumPadTwo}, {TEXT("Num3"), EKeys::NumPadThree},
            {TEXT("Num4"), EKeys::NumPadFour}, {TEXT("Num5"), EKeys::NumPadFive},
            {TEXT("Num6"), EKeys::NumPadSix}, {TEXT("Num7"), EKeys::NumPadSeven},
            {TEXT("Num8"), EKeys::NumPadEight}, {TEXT("Num9"), EKeys::NumPadNine},
        };

        if (const FKey* FoundKey = SupportedKeys.Find(KeyName))
        {
            OutKey = *FoundKey;
            return true;
        }
        return false;
    }
}

void FSmithUEInteractionCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("execute_editor_command"),
            TEXT("Interaction"),
            TEXT("Execute a named Unreal Editor command by name via GEditor->Exec"),
            {
                FSmithUEToolParam(TEXT("command_name"), TEXT("string"), TEXT("Editor command name to execute (e.g. ACTOR SELECT ALL)"), true)
            }),
        &HandleExecuteEditorCommand);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("execute_console_command"),
            TEXT("Interaction"),
            TEXT("Execute a UE console command in the current editor world"),
            {
                FSmithUEToolParam(TEXT("command"), TEXT("string"), TEXT("Console command string to execute (e.g. r.ScreenPercentage 100)"), true)
            }),
        &HandleExecuteConsoleCommand);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("list_editor_commands"),
            TEXT("Interaction"),
            TEXT("List all registered FUICommandInfo entries across all input binding contexts"),
            {
                FSmithUEToolParam(TEXT("filter"), TEXT("string"), TEXT("Optional case-insensitive substring filter applied to command name or label"))
            }),
        &HandleListEditorCommands);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("undo"),
            TEXT("Interaction"),
            TEXT("Undo the last editor transaction"),
            {}),
        &HandleUndo);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("redo"),
            TEXT("Interaction"),
            TEXT("Redo the last undone editor transaction"),
            {}),
        &HandleRedo);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("simulate_key"),
            TEXT("Interaction"),
            TEXT("Simulate a key press (command-lookup first, Slate fallback)"),
            {
                FSmithUEToolParam(TEXT("key"), TEXT("string"), TEXT("Key name: A-Z, F1-F12, Delete, Enter, Escape, Tab, Space, LeftArrow, RightArrow, UpArrow, DownArrow, Home, End, PageUp, PageDown, Num0-Num9"), true),
                FSmithUEToolParam(TEXT("modifiers"), TEXT("object"), TEXT("Optional modifiers: {ctrl,shift,alt,cmd} booleans"))
            }),
        &HandleSimulateKey);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("list_key_bindings"),
            TEXT("Interaction"),
            TEXT("List all registered key bindings (commands with active key chords)"),
            {
                FSmithUEToolParam(TEXT("filter"), TEXT("string"), TEXT("Optional substring filter on command name or label")),
                FSmithUEToolParam(TEXT("context"), TEXT("string"), TEXT("Optional binding context filter (e.g. LevelEditor)"))
            }),
        &HandleListKeyBindings);

}

// execute_editor_command
TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleExecuteEditorCommand(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    FString CommandName;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("command_name"), CommandName) || CommandName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'command_name'"));
    }

    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    const bool bExecResult = GEditor->Exec(World, *CommandName);

    if (!bExecResult)
    {
        return FSmithUECommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("GEditor->Exec did not handle command: '%s'"), *CommandName));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("executed"), true);
    Data->SetStringField(TEXT("command_name"), CommandName);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// execute_console_command
TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleExecuteConsoleCommand(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    FString Command;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'command'"));
    }

    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    GEditor->Exec(World, *Command);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("executed"), true);
    Data->SetStringField(TEXT("command"), Command);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// list_editor_commands
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleListEditorCommands(const TSharedPtr<FJsonObject>& Params)
{
    // list_editor_commands is safe in PIE — no PIE guard needed

    FString Filter;
    const bool bHasFilter = Params.IsValid() && Params->TryGetStringField(TEXT("filter"), Filter) && !Filter.IsEmpty();
    const FString FilterLower = bHasFilter ? Filter.ToLower() : FString();

    TArray<TSharedPtr<FJsonValue>> CommandsArray;

    TArray<TSharedPtr<FBindingContext>> Contexts;
    FInputBindingManager::Get().GetKnownInputContexts(Contexts);

    for (const TSharedPtr<FBindingContext>& Ctx : Contexts)
    {
        if (!Ctx.IsValid())
        {
            continue;
        }

        const FName ContextName = Ctx->GetContextName();

        TArray<TSharedPtr<FUICommandInfo>> CmdInfos;
        FInputBindingManager::Get().GetCommandInfosFromContext(ContextName, CmdInfos);

        for (const TSharedPtr<FUICommandInfo>& Cmd : CmdInfos)
        {
            if (!Cmd.IsValid())
            {
                continue;
            }

            const FString CmdName    = Cmd->GetCommandName().ToString();
            const FString CmdLabel   = Cmd->GetLabel().ToString();
            const FString CmdDesc    = Cmd->GetDescription().ToString();
            const FString CtxStr     = ContextName.ToString();

            if (bHasFilter)
            {
                const bool bNameMatch  = CmdName.ToLower().Contains(FilterLower);
                const bool bLabelMatch = CmdLabel.ToLower().Contains(FilterLower);
                if (!bNameMatch && !bLabelMatch)
                {
                    continue;
                }
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"),        CmdName);
            Entry->SetStringField(TEXT("label"),       CmdLabel);
            Entry->SetStringField(TEXT("description"), CmdDesc);
            Entry->SetStringField(TEXT("context"),     CtxStr);
            const auto& Ch = Cmd->GetActiveChord(EMultipleKeyBindingIndex::Primary); Entry->SetStringField(TEXT("input_chord"), Ch->IsValidChord() ? FString::Printf(TEXT("%s%s%s%s"), Ch->NeedsControl() ? TEXT("Ctrl+") : TEXT(""), Ch->NeedsShift() ? TEXT("Shift+") : TEXT(""), Ch->NeedsAlt() ? TEXT("Alt+") : TEXT(""), *Ch->Key.GetFName().ToString()) : FString());
            CommandsArray.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("commands"), CommandsArray);
    Data->SetNumberField(TEXT("count"), static_cast<double>(CommandsArray.Num()));
    if (bHasFilter)
    {
        Data->SetStringField(TEXT("filter"), Filter);
    }
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleUndo(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    GEditor->UndoTransaction();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("undone"), true);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// redo
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleRedo(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE())
    {
        return SmithUEEditorState::CreatePIEErrorResponse();
    }

    if (!GEditor)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("GEditor is not available"));
    }

    GEditor->RedoTransaction();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("redone"), true);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// simulate_key
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleSimulateKey(const TSharedPtr<FJsonObject>& Params)
{
    if (SmithUEEditorState::IsInPIE()) return SmithUEEditorState::CreatePIEErrorResponse();
    FString KeyName;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("key"), KeyName) || KeyName.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing required parameter: 'key'"));
    }

    FKey Key;
    if (!TryResolveKey(KeyName, Key))
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported key: %s"), *KeyName));
    }

    // Parse modifiers
    bool bCtrl = false, bShift = false, bAlt = false, bCmd = false;
    const TSharedPtr<FJsonObject>* ModObj = nullptr;
    if (Params->TryGetObjectField(TEXT("modifiers"), ModObj) && ModObj)
    {
        (*ModObj)->TryGetBoolField(TEXT("ctrl"),  bCtrl);
        (*ModObj)->TryGetBoolField(TEXT("shift"), bShift);
        (*ModObj)->TryGetBoolField(TEXT("alt"),   bAlt);
        (*ModObj)->TryGetBoolField(TEXT("cmd"),   bCmd);
    }

    // 1. Try command-lookup via FInputBindingManager
    FInputChord Chord(Key, bShift, bCtrl, bAlt, bCmd);
    TArray<TSharedPtr<FBindingContext>> Contexts;
    FInputBindingManager::Get().GetKnownInputContexts(Contexts);

    for (const TSharedPtr<FBindingContext>& Ctx : Contexts)
    {
        if (!Ctx.IsValid()) { continue; }
        TArray<TSharedPtr<FUICommandInfo>> CmdInfos;
        FInputBindingManager::Get().GetCommandInfosFromContext(Ctx->GetContextName(), CmdInfos);

        for (const TSharedPtr<FUICommandInfo>& Cmd : CmdInfos)
        {
            if (!Cmd.IsValid()) { continue; }
            const TSharedRef<const FInputChord> ActiveGesture = Cmd->GetActiveChord(EMultipleKeyBindingIndex::Primary);
            if (!ActiveGesture->IsValidChord()) { continue; }
            if (ActiveGesture->Key != Key) { continue; }
            if (ActiveGesture->NeedsShift() != bShift) { continue; }
            if (ActiveGesture->NeedsControl() != bCtrl) { continue; }
            if (ActiveGesture->NeedsAlt() != bAlt) { continue; }

            // Found a matching command — try to execute via LevelEditor global actions
            FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
            if (LevelEditorModule)
            {
                TSharedPtr<FUICommandList> CommandList = LevelEditorModule->GetGlobalLevelEditorActions();
                if (CommandList.IsValid() && CommandList->ExecuteAction(Cmd.ToSharedRef()))
                {
                    const FString ResolvedCmd = Cmd->GetCommandName().ToString();
                    UE_LOG(LogTemp, Log, TEXT("simulate_key: key=%s method=command_execution resolved=%s"), *KeyName, *ResolvedCmd);
                    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                    Data->SetBoolField(TEXT("simulated"), true);
                    Data->SetStringField(TEXT("key"), KeyName);
                    Data->SetStringField(TEXT("method"), TEXT("command_execution"));
                    Data->SetStringField(TEXT("resolved_command"), ResolvedCmd);
                    return FSmithUECommonUtils::CreateSuccessResponse(Data);
                }
            }
        }
    }

    // 2. Fallback: Slate key event
    if (!FSlateApplication::IsInitialized())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("FSlateApplication is not initialized"));
    }

    const FModifierKeysState ModState(bShift, bShift, bCtrl, bCtrl, bAlt, bAlt, false, false, bCmd);
    const FKeyEvent KeyEvent(Key, ModState, 0, false, 0, 0);
    FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);
    FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);

    UE_LOG(LogTemp, Log, TEXT("simulate_key: key=%s method=slate_fallback"), *KeyName);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("simulated"), true);
    Data->SetStringField(TEXT("key"), KeyName);
    Data->SetStringField(TEXT("method"), TEXT("slate_fallback"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// list_key_bindings
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUEInteractionCommands::HandleListKeyBindings(const TSharedPtr<FJsonObject>& Params)
{
    FString Filter, ContextFilter;
    const bool bHasFilter  = Params.IsValid() && Params->TryGetStringField(TEXT("filter"),  Filter)  && !Filter.IsEmpty();
    const bool bHasContext = Params.IsValid() && Params->TryGetStringField(TEXT("context"), ContextFilter) && !ContextFilter.IsEmpty();
    const FString FilterLower  = bHasFilter  ? Filter.ToLower()        : FString();
    const FString ContextLower = bHasContext ? ContextFilter.ToLower() : FString();

    TArray<TSharedPtr<FJsonValue>> BindingsArray;

    TArray<TSharedPtr<FBindingContext>> Contexts;
    FInputBindingManager::Get().GetKnownInputContexts(Contexts);

    for (const TSharedPtr<FBindingContext>& Ctx : Contexts)
    {
        if (!Ctx.IsValid()) { continue; }
        const FString CtxStr = Ctx->GetContextName().ToString();
        if (bHasContext && !CtxStr.ToLower().Contains(ContextLower)) { continue; }

        TArray<TSharedPtr<FUICommandInfo>> CmdInfos;
        FInputBindingManager::Get().GetCommandInfosFromContext(Ctx->GetContextName(), CmdInfos);

        for (const TSharedPtr<FUICommandInfo>& Cmd : CmdInfos)
        {
            if (!Cmd.IsValid()) { continue; }
            const TSharedRef<const FInputChord> Gesture = Cmd->GetActiveChord(EMultipleKeyBindingIndex::Primary);
            if (!Gesture->IsValidChord()) { continue; } // skip unbound

            const FString CmdName  = Cmd->GetCommandName().ToString();
            const FString CmdLabel = Cmd->GetLabel().ToString();
            if (bHasFilter)
            {
                if (!CmdName.ToLower().Contains(FilterLower) && !CmdLabel.ToLower().Contains(FilterLower))
                {
                    continue;
                }
            }

            TSharedPtr<FJsonObject> ModsObj = MakeShared<FJsonObject>();
            ModsObj->SetBoolField(TEXT("ctrl"),  Gesture->NeedsControl());
            ModsObj->SetBoolField(TEXT("shift"), Gesture->NeedsShift());
            ModsObj->SetBoolField(TEXT("alt"),   Gesture->NeedsAlt());

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("command_name"), CmdName);
            Entry->SetStringField(TEXT("label"),        CmdLabel);
            Entry->SetStringField(TEXT("key"),          Gesture->Key.GetFName().ToString());
            Entry->SetObjectField(TEXT("modifiers"),    ModsObj);
            Entry->SetStringField(TEXT("context"),      CtxStr);
            BindingsArray.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("bindings"), BindingsArray);
    Data->SetNumberField(TEXT("count"), static_cast<double>(BindingsArray.Num()));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

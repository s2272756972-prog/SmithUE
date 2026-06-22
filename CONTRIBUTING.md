# Contributing to SmithUE

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh.md)

SmithUE grows through community contributions, and you don't need to write code to help.
Whether you're an Artist, Technical Artist, Designer, or C++ developer, there's a way for you to make SmithUE better.

## Ways to Contribute

| | Type | Who | What |
|---|---|---|---|
| 🐛 | Report a Bug | Anyone | A command returned an error? Something behaves unexpectedly? |
| 💡 | Request a Feature | Anyone | Want AI to automate an editor task that SmithUE can't do yet? |
| 🎨 | Share a Workflow | Artists / TAs | Found a useful sequence of commands for your pipeline? |
| 📖 | Improve Docs | Anyone | Spotted unclear instructions or a missing example? |
| 🔧 | Add a Command | C++ Developers | Implement a new editor command in the plugin |

## For Non-Programmers (Artists & Technical Artists)

You don't need to understand C++ to contribute to SmithUE. The most valuable contributions often come from people who use the editor every day.

### How to Report a Bug

If a command doesn't work as expected, open an [Issue on GitHub](https://github.com/123dx-svg/SmithUE/issues/new).

A good bug report includes:
- **What you tried to do**, for example, "I asked the AI to create a material instance"
- **What command was used**, for example, `create_material_instance`
- **What happened**, for example, "Got error: Missing parameter 'parent'"
- **What you expected**, for example, "A new material instance created in /Game/Materials"
- **Your UE version**, for example, UE 5.2

You don't need to provide code or a reproduction project. A clear description is enough.

### How to Request a Feature

Think about something you do manually in the Unreal Editor that takes time:
- Adjusting the same material parameter across 20 material instances?
- Renaming actors in bulk by a pattern?
- Setting up lighting presets?

Open an [Issue](https://github.com/123dx-svg/SmithUE/issues/new) and describe:
- **What editor task you want to automate**
- **How often you do it**
- **What the ideal result looks like**

That's it. You're telling us what to build next.

### Share a Workflow

If you've discovered a useful sequence of SmithUE commands for your art pipeline (e.g., a standard character material setup, a batch LOD configuration flow), share it in [GitHub Discussions](https://github.com/123dx-svg/SmithUE/discussions). It may inspire a new command or a doc example.

### Issue Labels

When you open an issue, you can add labels to help us triage:
- `bug`: something isn't working
- `feature-request`: new capability you'd like to see
- `workflow`: a pipeline or usage pattern to discuss
- `docs`: documentation is unclear or incomplete

## For Developers

Adding a new command takes about 15 minutes. It lets AI agents interact with Unreal Engine in new ways.

### AI-Assisted Development

AI coding assistants (OpenCode, Claude Code, Cline, etc.) should read these files in the repo root for full context:

- **[AGENTS.md](AGENTS.md)** — repo boundaries, build commands, test systems, high-frequency pitfalls
- **[docs/spec/TOOL_SPEC.md](docs/spec/TOOL_SPEC.md)** — tool authoring conventions, schema, thread safety, 6-step checklist
- **[docs/spec/NAMING.md](docs/spec/NAMING.md)** — tool/param naming rules, canonical dictionary, description-for-routing
- **[docs/spec/PITFALLS.md](docs/spec/PITFALLS.md)** — 13 documented pitfalls

For runtime operation of the editor (not development), the `smithue-control` skill ships with [smithue-cli](https://www.npmjs.com/package/smithue-cli) (`smithue-cli skill --install`).

### Prerequisites

To develop and compile the SmithUE plugin, you need:

- Unreal Engine 5.2 installed
- Visual Studio 2022 with the "Game development with C++" workload
- Node.js is only required if you are developing the separate `smithue-cli` repo; it is not needed for this plugin repo
- Git for cloning the repository

### Getting Started

```bash
git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
cd SmithUE
```

### Repository Structure

The project is divided into the C++ plugin (this repo) and the separate `smithue-cli` client repo:

```text
SmithUE/
├── Source/SmithUE/
│   ├── Private/Commands/    # Domain command implementations
│   ├── Private/Transport/   # HTTP JSON server, dynamic port, /ready
│   ├── Private/UI/          # Editor status indicator
│   └── Public/ToolRegistry/ # Schema and Registry core
├── docs/
│   └── spec/                # TOOL_SPEC, NAMING, PITFALLS
├── CONTRIBUTING.md
└── SmithUE.uplugin
```

Note: Adding new UE commands does not require changes to `smithue-cli`. The CLI auto-discovers commands from the plugin at runtime via `/api/v1/tools`.

### How to Add a New Command

Follow these steps to implement a new command in the SmithUE plugin.

#### Step 1: Choose the Right Domain File

Commands are grouped by domain in `Source/SmithUE/Private/Commands/`. Locate the file that best fits your command. Use `SmithUEObservationCommands.cpp` for inspection tools, for instance.

#### Step 2: Add Header Declaration

Declare your handler function in the corresponding `.h` file in `Source/SmithUE/Public/Commands/`.

```cpp
// SmithUEObservationCommands.h
private:
    static TSharedPtr<FJsonObject> HandleMyNewCommand(const TSharedPtr<FJsonObject>& Params);
```

#### Step 3: Define the Tool Schema

In the `RegisterTools` function of the `.cpp` file, register your command by defining a `FSmithUEToolSchema`. This schema tells the system and the AI what your tool does and what parameters it accepts.

```cpp
Registry.Register(
    FSmithUEToolSchema(
        TEXT("my_new_command"), 
        TEXT("Observation"), // Category
        TEXT("Description of what the command does"),
        {
            FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter description"), true)
        }
    ),
    [](const TSharedPtr<FJsonObject>& Params) { return HandleMyNewCommand(Params); }
);
```

#### Step 4: Implement the Handler

Implement the handler function. It must accept a `TSharedPtr<FJsonObject>` for parameters and return a `TSharedPtr<FJsonObject>` containing the response.

```cpp
TSharedPtr<FJsonObject> FSmithUEObservationCommands::HandleMyNewCommand(const TSharedPtr<FJsonObject>& Params)
{
    FString MyParam;
    if (!Params->TryGetStringField(TEXT("param_name"), MyParam))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing parameter: param_name"));
    }

    // Your logic here...

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("result"), TEXT("Success!"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
```

#### Step 5: Compile

Use the Unreal Build Tool to compile the plugin. Run the following command:

```powershell
dotnet "{EngineRoot}/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" {ProjectName}Editor Win64 Development "-Project={ProjectRoot}/{ProjectName}.uproject" -WaitMutex
```

Adjust `{EngineRoot}`, `{ProjectRoot}`, and `{ProjectName}` to match your local environment.

#### Step 6: Test Your Command

You can test the command directly through the CLI or by curling the plugin's dynamic port:

```bash
npx smithue-cli exec my_new_command '{"params":{"param_name":"test"}}'

# Or read %LOCALAPPDATA%\.smithue\<pid>.port and curl that port directly
curl -X POST http://localhost:<dynamic-port>/api/v1/execute -H "Content-Type: application/json" -d '{"command":"my_new_command","params":{"param_name":"test"}}'
```

`13721` is only an example in older docs; the real port is dynamic and comes from the port file.

### Code Conventions

- **Response Format**: All handlers must return a JSON object via `FSmithUECommonUtils::CreateSuccessResponse(Data)` or `CreateErrorResponse(Message)`. The final envelope will have `status: "success"|"error"` and `data: {...}`.
- **Logging**: Use the `SMITHUE_LOG` macro for consistent logging within the plugin.
- **Validation**: Always validate parameters at the start of your handler. Return clear error messages for invalid or missing inputs.
- **Categories**: Commands are organized into 23 domains (Blueprint, Material, Niagara, Asset, Analysis, Level, Environment, PIE, Editor, Data, Observation, Interaction, Animation, Viewport, Sequencer, Input, System, Project, Curve, UMG, Debug, RenderTarget, Physics). New domains may be added when 3 or more related commands form a distinct group.
- **Full Reference**: See [TOOLS.md](TOOLS.md) for the complete tool reference with parameter schemas.
- **Compatibility**: Target Unreal Engine 5.2 only. Avoid using APIs introduced in 5.3 or later.

### Example: Adding "list_actors"

Here is a simplified example of adding a command to list actors in the current level.

**Registration:**
```cpp
Registry.Register(
    FSmithUEToolSchema(TEXT("list_actors"), TEXT("Observation"), TEXT("Lists all actors in the level")),
    [](const TSharedPtr<FJsonObject>& Params) { return HandleListActors(Params); }
);
```

**Implementation:**
```cpp
TSharedPtr<FJsonObject> FSmithUEObservationCommands::HandleListActors(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> ActorList;
    for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
    {
        TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
        ActorObj->SetStringField(TEXT("label"), It->GetActorLabel());
        ActorList.Add(MakeShared<FJsonValueObject>(ActorObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("actors"), ActorList);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
```

### Pull Request Checklist

- [ ] Command compiles without errors or warnings.
- [ ] Handler returns the correct JSON envelope format.
- [ ] Parameters are validated and errors are handled gracefully.
- [ ] Command is registered in the appropriate domain-specific file.
- [ ] Functionality has been verified via `smithue-cli exec` or by curling the dynamic port from `%LOCALAPPDATA%\.smithue\<pid>.port`.

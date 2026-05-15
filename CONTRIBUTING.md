# Contributing to SmithUE

SmithUE grows through community contributions. Adding a new command takes ~15 minutes and enables AI agents to interact with Unreal Engine in new ways.

## AI 辅助开发 / AI-Assisted Development

如果你使用 AI 编码工具（OpenCode、Claude Code、Cline 等）参与 SmithUE 的迭代开发，强烈建议安装项目自带的开发技能，它会教你的 AI 助手理解 SmithUE 的架构、编码规范和工作流程：

If you use an AI coding tool (OpenCode, Claude Code, Cline, etc.) to contribute to SmithUE, install the bundled development skill so your AI assistant understands the architecture, conventions, and workflow:

```
# Skill 文件位置 / Skill file location:
Docs/smithue-dev/SKILL.md
```

各工具的安装方式 / Installation per tool:

- **OpenCode**: 将 `Docs/smithue-dev/` 复制到 `~/.agents/skills/smithue-dev/`
- **Claude Code**: 运行 `claude skill add ./Docs/smithue-dev/SKILL.md`
- **其他工具**: 将 `Docs/smithue-dev/SKILL.md` 的内容添加到你的 AI 工具的 system prompt 或 skill 配置中

## Prerequisites

To develop and compile the SmithUE plugin, you need:

*   Unreal Engine 5.2 installed
*   Visual Studio 2022 with the "Game development with C++" workload
*   Node.js 18+ (required for the MCP Server)
*   Git (for cloning the repository)

## Getting Started

```bash
git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
cd SmithUE
```

## Repository Structure

The project is divided into the C++ plugin and the TypeScript MCP server:

```text
SmithUE/
├── Source/SmithUE/
│   ├── Private/Commands/    # 命令实现 / Domain command implementations
│   ├── Private/Transport/   # HTTP 服务 & 连接管理 / HTTP server & connection manager
│   ├── Private/UI/          # 编辑器状态指示器 / Editor status indicator
│   └── Public/ToolRegistry/ # Schema/Registry 核心 / Schema and Registry core
├── Scripts/
│   └── SmithUE-MCP/         # TypeScript MCP 服务 / TypeScript MCP Server
├── Docs/
│   └── smithue-dev/         # AI 开发技能 / AI development skill
│       └── SKILL.md
├── CONTRIBUTING.md
└── SmithUE.uplugin
```

> **Note**: Adding new UE commands does NOT require changes to `Scripts/SmithUE-MCP/` — the MCP Server auto-discovers commands from the plugin.
> **注意**：添加新 UE 命令不需要修改 `Scripts/SmithUE-MCP/` — MCP 服务会自动发现插件中的命令。

## How to Add a New Command

Follow these steps to implement a new command in the SmithUE plugin.

### Step 1: Choose the Right Domain File

Commands are grouped by domain in `Source/SmithUE/Private/Commands/`. Locate the file that best fits your command (e.g., `SmithUEObservationCommands.cpp` for inspection tools).

### Step 2: Add Header Declaration

Declare your handler function in the corresponding `.h` file in `Source/SmithUE/Public/Commands/`.

```cpp
// SmithUEObservationCommands.h
private:
    static TSharedPtr<FJsonObject> HandleMyNewCommand(const TSharedPtr<FJsonObject>& Params);
```

### Step 3: Define the Tool Schema

In the `RegisterTools` function of the `.cpp` file, register your command by defining a `FSmithUEToolSchema`. This schema tells the system (and the AI) what your tool does and what parameters it accepts.

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

### Step 4: Implement the Handler

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

### Step 5: Compile

Use the Unreal Build Tool to compile the plugin. Run the following command (adjust paths if necessary):

```powershell
dotnet "{EngineRoot}/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" {ProjectName}Editor Win64 Development "-Project={ProjectRoot}/{ProjectName}.uproject" -WaitMutex
```

> Adjust `{EngineRoot}`, `{ProjectRoot}`, and `{ProjectName}` to match your local environment.
> 请根据你的本地环境调整 `{EngineRoot}`、`{ProjectRoot}` 和 `{ProjectName}`。

### Step 6: Test Your Command

You can test the command directly via HTTP before trying it through the MCP server:

```bash
curl -X POST http://localhost:13721/api/v1/execute -H "Content-Type: application/json" -d '{"command":"my_new_command","params":{"param_name":"test"}}'
```

The MCP Server auto-discovers new commands from the plugin. No TypeScript changes are needed!

## Code Conventions

*   **Response Format**: All handlers must return a JSON object via `FSmithUECommonUtils::CreateSuccessResponse(Data)` or `CreateErrorResponse(Message)`. The final envelope will have `status: "success"|"error"` and `data: {...}`.
*   **Logging**: Use the `SMITHUE_LOG` macro for consistent logging within the plugin.
*   **Validation**: Always validate parameters at the start of your handler. Return clear error messages for invalid or missing inputs.
*   **Categories**: Commands are organized into 18 domains (System, Project, Material, Asset, Editor, Interaction, Blueprint, Viewport, Observation, Analysis, Niagara, Level, Data, Sequencer, Environment, PIE, Animation, Input). New domains may be added when 3+ related commands form a distinct group — see `Docs/smithue-dev/SKILL.md` for guidelines.
*   **Full Reference**: See [TOOLS.md](TOOLS.md) for the complete 172-tool reference with parameter schemas.
*   **Compatibility**: Target Unreal Engine 5.2 only. Avoid using APIs introduced in 5.3 or later.

## Example: Adding "list_actors"

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

## Pull Request Checklist

* [ ] Command compiles without errors or warnings.
* [ ] Handler returns the correct JSON envelope format.
* [ ] Parameters are validated and errors are handled gracefully.
* [ ] Command is registered in the appropriate domain-specific file.
* [ ] Functionality has been verified via `curl` or the MCP server.

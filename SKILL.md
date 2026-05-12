---
name: smithue-dev
description: SmithUE/UEAgent UE5 plugin development skill. Standardizes AI-assisted contribution: adding commands, fixing bugs, compiling, testing. Use this skill whenever the user mentions SmithUE, UEAgent, ToolRegistry, FSmithUEToolSchema, or wants to add/modify editor automation commands in the SmithUE project.
---

# SmithUE Development Skill

You are a SmithUE contributor assistant. SmithUE is a UE5.2 editor automation plugin that exposes 65+ editor commands via HTTP (port 13721) and bridges to AI tools via MCP protocol.

## Architecture Overview

SmithUE follows a clear layered architecture that allows Unreal Engine editor functionality to be safely exposed to AI agents:

*   **UE5 C++ Plugin**: The core engine logic, hosting an HTTP server on port 13721.
*   **ToolRegistry Pattern**: Commands are defined using `FSmithUEToolSchema` and registered in `RegisterTools()`.
*   **8 Domains**: Commands are grouped into System, Asset, Material, Editor, Blueprint, Viewport, Observation, and Analysis.
*   **TypeScript MCP Server**: A bridge that auto-discovers commands from the UE plugin. No TypeScript changes are needed when adding new UE commands.
*   **Response Envelope**: Every command returns a JSON object with `{ "status": "success"|"error", "data": { ... } }`.

## Adding a New Command

Follow these steps to implement a new automation command:

1.  **Choose Domain**: Identify the relevant domain file in `Source/SmithUE/Private/Commands/SmithUE{Domain}Commands.cpp`.
2.  **Declare Header**: Add the static handler declaration in the corresponding `.h` file in `Source/SmithUE/Public/Commands/`.
    ```cpp
    static TSharedPtr<FJsonObject> HandleMyCommand(const TSharedPtr<FJsonObject>& Params);
    ```
3.  **Define Schema**: Create a `FSmithUEToolSchema` with name, category, description, and parameters.
4.  **Implement Handler**: Write the logic returning a `TSharedPtr<FJsonObject>` using the status/data envelope.
5.  **Register**: Call `Registry.Register(schema, handler)` inside the `RegisterTools` function.
6.  **Compile**: Use the UnrealBuildTool command:
    ```bash
    dotnet "E:\Program Files\Epic Games\UE_5.2\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" SmithUE Win64 Development "-Project=F:\DXProject\AIScript\AIScript.uproject" -WaitMutex
    ```
7.  **Test**: Verify with a curl request:
    ```bash
    curl -X POST http://localhost:13721 -d '{"command":"your_command"}'
    ```

## Code Conventions

*   **Result Format**: Use `FSmithUECommonUtils::CreateSuccessResponse(Data)` or `CreateErrorResponse(Message)`.
*   **Logging**: Use the `SMITHUE_LOG` macro for all internal logging.
*   **Validation**: Validate parameters at the start of every handler. Return clear errors for missing or malformed inputs.
*   **Version Lock**: Strictly use UE 5.2 APIs. Do not use 5.3+ features.
*   **Dependencies**: No third-party C++ libraries are allowed.

## File Organization

```text
Source/SmithUE/
├── Private/Commands/SmithUE{Domain}Commands.cpp  # Command implementations
├── Public/Commands/SmithUE{Domain}Commands.h     # Declarations
├── Private/Blueprint/                             # Low-level Blueprint APIs
└── Public/ToolRegistry/                           # Schema/Registry core logic
```

The MCP Server (`Scripts/SmithUE-MCP/`) lives inside the plugin:
```text
Scripts/SmithUE-MCP/src/
├── client.ts    # SmithUE HTTP Client
├── tools.ts     # MCP meta-tool definitions
└── index.ts     # Server entry point
```

## Constraints

*   **Environment**: UE 5.2 only.
*   **Network**: Default HTTP port is 13721 (can be overridden via `-ueagenthttpport=`).
*   **MCP Protocol**: Uses a meta-tool approach (3 fixed tools); do not attempt to register tools individually in TypeScript.
*   **Scope**: Supports `smithue serve`. Command-line execution (`smithue exec`) is not supported in v1.

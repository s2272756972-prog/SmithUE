# SmithUE — AI-Native UE5 Editor Automation

Control Unreal Engine 5 from any AI tool via MCP protocol.

[![npm version](https://img.shields.io/npm/v/smithue.svg)](https://www.npmjs.com/package/smithue)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## What is SmithUE

SmithUE is a high-performance automation bridge designed to empower AI tools with direct, programmatic control over the Unreal Engine 5 editor. It consists of a C++ plugin that exposes the engine's internal Reflection API through a high-speed HTTP interface, allowing for seamless integration with modern LLM-driven development workflows.

The project implements the Model Context Protocol (MCP) via a dedicated TypeScript server. Instead of overwhelming the AI's context with hundreds of individual tool registrations, SmithUE uses a meta-tool architecture to provide access to over 65 editor commands across 8 domains, including System, Asset, Material, Editor, Blueprint, Viewport, Observation, and Analysis.

## Architecture

SmithUE operates as a multi-tier bridge between your AI assistant and the UE5 editor environment.

```
AI Tool (OpenCode/Claude/Cline/Copilot)
     ↕ MCP stdio
SmithUE MCP Server (npx smithue serve)
     ↕ HTTP :13721
SmithUE UE5 Plugin
     ↕ UE Reflection API
Unreal Engine 5.2 Editor
```

## Context Impact Analysis

Traditional MCP implementations that register every available command as a separate tool quickly consume the LLM's limited context window. SmithUE solves this by using three fixed meta-tools that dynamically query and execute commands, maintaining a constant context footprint regardless of the underlying command count.

| Approach | 65 tools | 200 tools | 1000 tools |
|---|---|---|---|
| Full registration | ~6.5K tokens | ~20K tokens | ~100K tokens |
| Meta-tool (SmithUE) | ~300 tokens | ~300 tokens | ~300 tokens |

The workflow follows a discovery-first pattern:
1. `smithue_list_domain`: Discover categories and specific command schemas.
2. `smithue_search`: Locate commands by keywords or functionality.
3. `smithue_execute`: Run the identified command with validated parameters.

## Quick Start

### Prerequisites
- Unreal Engine 5.2 (or compatible versions)
- Node.js 18 or higher

### Step 1: Install the Plugin
Clone the repository into your Unreal Engine project's `Plugins/` folder:
```bash
cd {YourProject}/Plugins
git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
```
Ensure the plugin is enabled in your `.uproject` file and compile the project.

### Step 2: Build & Start the MCP Server
Install dependencies and build the MCP Server:
```bash
cd Plugins/SmithUE/mcp-server
npm install
npm run build
```

Start the server (requires UE Editor running with SmithUE plugin):
```bash
node Plugins/SmithUE/mcp-server/dist/index.js serve
```

### AI Configuration

#### OpenCode
Add the server to your `mcp.json` or `.opencode/mcp.json`:
```json
{
  "mcpServers": {
    "smithue": {
      "command": "node",
      "args": ["{YourProject}/Plugins/SmithUE/mcp-server/dist/index.js", "serve"]
    }
  }
}
```

#### Claude Code
Run the following command in your terminal:
```bash
claude mcp add smithue -- node {YourProject}/Plugins/SmithUE/mcp-server/dist/index.js serve
```

#### GitHub Copilot
Create or update `.github/copilot-mcp.json`:
```json
{
  "mcpServers": {
    "smithue": {
      "command": "node",
      "args": ["{YourProject}/Plugins/SmithUE/mcp-server/dist/index.js", "serve"]
    }
  }
}
```

#### Cline
Add the configuration to your VSCode settings under `cline.mcpServers`:
```json
{
  "smithue": {
    "command": "node",
    "args": ["{YourProject}/Plugins/SmithUE/mcp-server/dist/index.js", "serve"]
  }
}
```

## Deployment Guide

### UE Plugin
Compile the plugin using the Unreal Build Tool. For a standard Win64 development build:
```bash
dotnet "E:\Program Files\Epic Games\UE_5.2\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" SmithUE Win64 Development "-Project={YourProjectPath}.uproject" -WaitMutex
```

### MCP Server
The server defaults to port 13721 on localhost. You can override these settings using environment variables:
```bash
SMITHUE_PORT=13721 SMITHUE_HOST=localhost node Plugins/SmithUE/mcp-server/dist/index.js serve
```

### Verification
Ensure the UE editor is running with the plugin enabled, then verify the connection:
```bash
curl -X POST http://localhost:13721 -d '{"command":"ping"}'
```

## One-Click Setup Prompt

Copy and paste the following prompt into your AI assistant to automatically configure SmithUE for your environment.

---

**Setup SmithUE MCP Server**

I want to set up SmithUE for editor automation. Please perform the following steps:

1. Check if Node.js 18 or higher is installed on this system.
2. Find the SmithUE plugin directory (look for `Plugins/SmithUE/mcp-server/` in the UE project).
3. Run `cd {path-to-SmithUE}/mcp-server && npm install && npm run build` to build the MCP Server.
4. Detect which AI tool is currently being used and write the appropriate MCP configuration:
   - If OpenCode: Write to `mcp.json` or `.opencode/mcp.json` with `{"mcpServers":{"smithue":{"command":"node","args":["{path}/mcp-server/dist/index.js","serve"]}}}`.
   - If Claude Code: Execute `claude mcp add smithue -- node {path}/mcp-server/dist/index.js serve`.
   - If Cline: Update the VSCode settings under `cline.mcpServers`.
   - If GitHub Copilot: Create or update `.github/copilot-mcp.json`.
5. Verify the connection by running `curl -X POST http://localhost:13721 -d '{"command":"ping"}'` and report the result.

**一键配置 SmithUE**

我想配置 SmithUE 以实现编辑器自动化，请执行以下步骤：

1. 检查系统中是否安装了 Node.js 18 或更高版本。
2. 找到 SmithUE 插件目录（在 UE 项目中查找 `Plugins/SmithUE/mcp-server/`）。
3. 运行 `cd {SmithUE路径}/mcp-server && npm install && npm run build` 构建 MCP 服务。
4. 识别当前使用的 AI 工具并写入正确的 MCP 配置：
   - 如果是 OpenCode：写入 `mcp.json` 或 `.opencode/mcp.json`，配置 `{"mcpServers":{"smithue":{"command":"node","args":["{路径}/mcp-server/dist/index.js","serve"]}}}`。
   - 如果是 Claude Code：执行 `claude mcp add smithue -- node {路径}/mcp-server/dist/index.js serve`。
   - 如果是 Cline：更新 VSCode 设置中的 `cline.mcpServers`。
   - 如果是 GitHub Copilot：创建或更新 `.github/copilot-mcp.json`。
5. 运行 `curl -X POST http://localhost:13721 -d '{"command":"ping"}'` 验证连接并报告结果。

---

## Current Commands

SmithUE provides a comprehensive suite of editor commands organized into 8 functional domains:
- **System**: Plugin status and core engine operations.
- **Asset**: Management, import, and manipulation of Unreal assets.
- **Material**: Programmatic creation and editing of Materials and Material Instances.
- **Editor**: General editor state and workflow automation.
- **Blueprint**: Node manipulation and logic generation.
- **Viewport**: Camera control and viewport visualization.
- **Observation**: Scene analysis and actor property inspection.
- **Analysis**: Performance metrics and structural verification.

For a full reference of available commands and their parameters, please refer to the [Plugin Command Reference](../README.md).

## Contributing

Contributions are welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for step-by-step instructions on adding new commands.

## License

This project is licensed under the [MIT License](LICENSE).

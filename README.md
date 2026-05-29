# SmithUE

<p align="center">
  <img src="https://raw.githubusercontent.com/123dx-svg/SmithUE/main/Resources/Icon128.png" width="128" />
</p>

<p align="center">
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.zh.md">简体中文</a>
</p>

A high-performance Unreal Engine editor plugin that exposes the editor's full capabilities through a command protocol and MCP integration, turning repetitive manual operations into scriptable, AI-driven workflows.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Architecture](#architecture)
3. [MCP Server](#mcp-server)
4. [Command Reference](#command-reference)
5. [Connection Status Indicator](#connection-status-indicator)
6. [AI Texture Generation](#ai-texture-generation)
7. [Changelog](#changelog)
8. [Roadmap](#roadmap)
9. [Known Limitations](#known-limitations)
10. [Contributing](#contributing)
11. [License](#license)

---

## Quick Start

1. Clone into `{YourProject}/Plugins/` on branch `UE5.2`:
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
   ```

2. Build your project with Unreal Engine 5.2.

3. Launch the editor. SmithUE automatically starts the HTTP server on port 13721.

4. Verify the connection:
   ```bash
   curl -X POST http://localhost:13721/api/v1/execute -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
   ```

**Note**: The MCP Server is pre-built at `Scripts/SmithUE-MCP/dist/bundle.js`. Only Node.js 18+ is required.

---

## Architecture

```
AI Tool (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ MCP stdio
SmithUE MCP Server (Scripts/SmithUE-MCP/)
     ↕ HTTP :13721
SmithUE UE5 Plugin
     ↕ UE Reflection API
Unreal Engine 5.2 Editor
```

---

## MCP Server

SmithUE includes a TypeScript MCP (Model Context Protocol) Server that bridges AI tools to the UE5 plugin. It uses a meta-tool architecture, using 3 fixed tools instead of registering every command individually. This keeps AI context usage constant at approximately 300 tokens regardless of command count.

### Context Impact

| Approach | N tools | 200 tools | 1000 tools |
|---|---|---|---|
| Full registration | ~6.5K tokens | ~20K tokens | ~100K tokens |
| Meta-tool (SmithUE) | ~300 tokens | ~300 tokens | ~300 tokens |

### 3 Meta-Tools

| Tool | Description |
|---|---|
| `smithue_list_domain` | List domains or get full command schemas for a domain |
| `smithue_search` | Search commands by keyword |
| `smithue_execute` | Execute any command with parameters |

### Install & Run

The MCP Server is pre-built and ready to use. Start it using the following command (requires UE Editor running with SmithUE plugin):

```bash
node Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js serve
```

Environment variables:
- `SMITHUE_PORT`: HTTP port (default: `13721`)
- `SMITHUE_HOST`: Host address (default: `localhost`)
- `SMITHUE_CLIENT_NAME`: Client display name for connection indicator (default: `OpenCode`)

### AI Tool Configuration

#### OpenCode

Add to `opencode.json` in your project root:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "smithue": {
      "type": "local",
      "command": ["node", "{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
    }
  }
}
```

#### Claude Code

```bash
claude mcp add smithue -- node {YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js serve
```

#### GitHub Copilot

Create or update `.github/copilot-mcp.json`:

```json
{
  "mcpServers": {
    "smithue": {
      "command": "node",
      "args": ["{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
    }
  }
}
```

#### Cline

Add to VSCode settings under `cline.mcpServers`:

```json
{
  "smithue": {
    "command": "node",
    "args": ["{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
  }
}
```

### Workflow Example

1. `smithue_list_domain()`: See all 19 domains.
2. `smithue_list_domain("Material")`: Get Material command schemas.
3. `smithue_search("blueprint")`: Find blueprint-related commands.
4. `smithue_execute("create_material", {"name": "M_Test", "path": "/Game/Materials"})`: Execute command.

---

## Command Reference

SmithUE provides **178 tools** organized across **19 functional domains**. The command set is continuously growing. Use `smithue_list_domain()` to see the latest available commands, or refer to [TOOLS.md](TOOLS.md) for the full reference.

### Domain Overview

| Domain | Tools | Description |
|---|---|---|
| System | 5 | Server connectivity, session management |
| Project | 4 | Project info, plugins, folders, source files |
| Material | 20 | Materials, material instances, MPC, material functions |
| Asset | 12 | Asset CRUD, browser operations, AI texture generation |
| Editor | 8 | Actor spawning, properties, post-process, project settings |
| Interaction | 7 | Console/editor commands, undo/redo, key simulation |
| Blueprint | 17 | BP creation, nodes, functions, variables, components, DSL compiler |
| Viewport | 6 | Camera control, screenshots, actor selection |
| Observation | 7 | Panels, editor state, actor properties, world outline |
| Analysis | 13 | Source analysis, dependency graphs, BP diagnostics, asset validation |
| Niagara | 17 | Particle system creation, emitters, modules, renderers |
| Level | 11 | Level management, landscape, foliage |
| Data | 6 | DataTables, UserDefinedStructs, UserDefinedEnums |
| Sequencer | 6 | LevelSequence creation, bindings, tracks, keyframes |
| Environment | 11 | Post-process, fog, sky, lights, physics, splines |
| PIE | 11 | Play-In-Editor: start/stop, actors, properties, console |
| Animation | 7 | AnimMontage, AnimBlueprint, sections, notifies |
| Input | 6 | Enhanced Input: InputAction, InputMappingContext |
| UMG | 4 | Widget Blueprint creation, widget tree, properties |

---

## Connection Status Indicator

SmithUE displays a status indicator in the editor status bar:

| Color | State |
|---|---|
| 🟢 Green | Connected, MCP client session active |
| 🔴 Red | Disconnected, no active sessions |
| 🟡 Red/Yellow blink | State change, session connecting or disconnecting |

---

## AI Texture Generation

The `generate_texture` command bridges the editor with modern generative AI. It automatically detects the target API format based on the provided endpoint URL, supporting DALL-E, Imagen, and OpenAI Chat-based generation.

**Example Usage:**
```bash
curl -X POST http://localhost:13721/api/v1/execute \
  -H "Content-Type: application/json" \
  -d '{"command":"generate_texture","params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## Changelog

### v0.6.0, N-id Sessions, Metrics, Blueprint Preview & Editor Guards

**New Features:**
- N-id session system: short-ID (N0, N1, ...) to GUID mapping per blueprint graph, with stale detection.
- Command metrics: call count, request/response bytes, execution timing, retry detection, per-command stats.
- `system_get_metrics` / `system_reset_metrics`: query and reset session metrics.
- `take_blueprint_preview_screenshot`: capture SCS (Components) viewport of any Blueprint as PNG.
- Editor state guard: non-readonly commands rejected while PIE is running.
- Blueprint atomic API expansion: approximately 1600 lines of new BP manipulation primitives.
- MCP Server: enhanced tool dispatching logic.

**Fixes:**
- `FTSTicker::FDelegateHandle` type correction for UE 5.2+ compatibility.
- Added `RHI` module dependency to Build.cs.

### v1.3.0, Blueprint Component System Enhancement

**New Features:**
- `bp_add_component` supports `parent` param for hierarchical component attachment.
- `bp_remove_component` command to remove components from SCS. Children are reparented automatically.
- `bp_get_summary` returns component hierarchy with `parent` and `children` fields.
- `bp_create` supports three parent class formats: C++ class name, BP class name with `_C` suffix, or BP asset path.
- `bp_create_node` supports `Class::Function` shorthand to auto-create CallFunction nodes.
- `bp_create_node` supports `K2Node_EnhancedInputAction` via `input_action` extra param.
- `bp_create_node` supports `K2Node_DynamicCast` via `target_class` extra param.
- Added `InputBlueprintNodes` module dependency for Enhanced Input Blueprint nodes.

**Bug Fixes:**
- Fixed Blueprint component recompilation crash by using `SkipGarbageCollection`.
- Fixed pure virtual crash when adding BP-derived components to SCS.

---

## Roadmap

- **v0.7**, Blueprint Depth
  - Complete DSL compiler for EventGraph and FunctionGraph generation.
  - Blueprint debugging tools including breakpoints, stepping, and variable watching via commands.
  - Full K2Node coverage for all common node types.

- **v0.8**, Domain Expansion
  - AI, Navigation, and BehaviorTree commands.
  - World Partition and Level Streaming commands.
  - PCG (Procedural Content Generation) commands.
  - Gameplay Ability System commands.
  - Physics and Collision configuration commands.

- **v0.9**, Performance & Stability
  - Async command execution pipeline for non-blocking operations.
  - Batch operation support to execute multiple commands in one request.
  - Transaction system for atomic multi-command rollback.
  - Enhanced error recovery and diagnostics.

- **v1.0**, Multi-Client & SDK
  - Multi-client collaboration mode for concurrent sessions.
  - Python SDK accessible via `pip install smithue`.
  - REST API documentation with OpenAPI specification.
  - WebSocket streaming for real-time editor events.

---

## Known Limitations

- **Concurrency**: Single-client connection only.
- **Input Focus**: `simulate_key` requires viewport focus.
- **Property Types**: Advanced types like `TMap` or delegates are not supported by `set_actor_property`. Nested struct properties require dedicated commands.
- **SceneTexture**: `SceneTextureId` is set via FProperty reflection to avoid link errors with unexported engine symbols.
- **OS**: Windows Win64 only.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for step-by-step instructions on adding new commands.

### AI-Assisted Development Skill

SmithUE provides an AI development skill file at `Docs/smithue-dev/SKILL.md`. This file teaches AI coding assistants how to contribute to the project.

**Install:**

Copy the skill directory to your AI tool's skill location:

```bash
# OpenCode (project-level)
cp -r Plugins/SmithUE/Docs/smithue-dev {YourProject}/.agents/skills/

# OpenCode (user-level)
cp -r Plugins/SmithUE/Docs/smithue-dev ~/.agents/skills/

# Claude Code
cp -r Plugins/SmithUE/Docs/smithue-dev ~/.claude/skills/
```

---

## License

Copyright 2026, 123dx-svg. MIT License.

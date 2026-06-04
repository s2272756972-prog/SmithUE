# SmithUE

<p align="center">
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

<p align="center">
  <a href="README.md">简体中文</a> | <a href="README.en.md">English</a>
</p>

A high-performance Unreal Engine editor plugin that exposes the editor's full capabilities through a command protocol, turning repetitive manual operations into scriptable, AI-driven workflows.

---

## For AI Agents

### Quick Deployment (Windows)
```bash
# Verify editor is running and SmithUE plugin is enabled
npx smithue-cli status
npx smithue-cli exec ping '{}'
npx smithue-cli list
```

### Troubleshooting Tree
1. Editor not running → Start Unreal Engine Editor and wait for full load
2. Multiple instances → Use `npx smithue-cli status --pid <pid>` to specify instance
3. Stale portfiles → Run `npx smithue-cli prune` to clean up

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Architecture](#architecture)
3. [Command Reference](#command-reference)
4. [Connection Status Indicator](#connection-status-indicator)
5. [AI Texture Generation](#ai-texture-generation)
6. [Changelog](#changelog)
7. [Roadmap](#roadmap)
8. [Known Limitations](#known-limitations)
9. [Contributing](#contributing)
10. [License](#license)

---

## Quick Start

1. Clone into `{YourProject}/Plugins/` on branch `UE5.2`:
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
   ```

2. Build your project with Unreal Engine 5.2.

3. Launch the editor. SmithUE automatically starts the HTTP server and assigns a dynamic port.

4. Verify the connection:
   ```bash
   npx smithue-cli status
   ```

**Note**: SmithUE now uses `smithue-cli` for interaction. Details at: https://github.com/123dx-svg/smithue-cli

---

## Architecture

```
AI Tool (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ smithue-cli (npx smithue-cli exec/list/search/status)
SmithUE UE5 Plugin (HTTP :dynamic port)
     ↕ UE Reflection API
Unreal Engine 5.2 Editor
```

---

## Command Reference

SmithUE provides **178 tools** organized across **19 functional domains**. The command set is continuously growing. Use `npx smithue-cli list` to see the latest available commands, or refer to [TOOLS.md](TOOLS.md) for the full reference.

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

SmithUE displays a circular status indicator in the editor status bar showing real-time CLI connection state.

### Indicator Colors

| Color | State |
|---|---|
| 🟢 Green | Ready — HTTP server started and port assigned, reachable via CLI |
| 🔴 Gray | Not Ready — Server not started or initializing |

### Interactive Features

- **Tooltip Details**:
  - **SmithUE version** (read from `.uplugin`)
  - **Port** — Current dynamically assigned HTTP port
  - **PID** — Current editor process ID
  - **Status** — Ready state confirmation
- **One-Click Copy**: Click the indicator to copy the base `smithue-cli` command for the current instance.

---

## AI Texture Generation

The `generate_texture` command bridges the editor with modern generative AI. It automatically detects the target API format based on the provided endpoint URL, supporting DALL-E, Imagen, and OpenAI Chat-based generation.

**Example Usage:**
```bash
npx smithue-cli exec generate_texture '{"params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## Changelog

### v0.8.0, CLI Migration
- Removed TCP Server / ConnectionManager / SessionManager
- HTTP Server changed to dynamic port + portfile discovery
- `/ready` endpoint + 503 guard during startup
- StatusIndicator rewritten as CLI-aware dot + copy-CLI-command button
- New smithue-cli npm package replaces MCP: `npm install -g smithue-cli`
- See: https://github.com/123dx-svg/smithue-cli

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

## Historical: MCP Server Deprecated
The MCP Server was removed in v0.8.0 and replaced by `smithue-cli`. See [smithue-cli MIGRATION.md](https://github.com/123dx-svg/smithue-cli/blob/main/MIGRATION.md) for the migration guide.

---

## License

Copyright 2026, 123dx-svg. MIT License.

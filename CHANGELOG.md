# SmithUE Changelog

## v1.8.0 — Blueprint Breakpoint & Navigation

### Added
- `bp_set_breakpoint` — Set/enable a breakpoint on a Blueprint node by NodeGuid. Params: `bp_path` (required), `graph_name` (EventGraph or function name), `node_id` (NodeGuid), `focus` (boolean, default true). When `focus=true` (default) also opens the Blueprint editor and jumps to the node. Set `focus=false` for headless/batch use. Returns `{bp_path, graph, node_id, node_title, enabled}`.
- `bp_clear_breakpoint` — Remove a breakpoint from a node. Same params as `bp_set_breakpoint` (including `focus`). Returns `{..., was_present}`.
- `bp_list_breakpoints` — List all breakpoints in a Blueprint. Param: `bp_path` (required). Returns `{bp_path, breakpoint_count, breakpoints:[{graph, node_id, node_title, enabled}]}`.
- `bp_focus_node` — Open a Blueprint editor and focus a target. Params: `bp_path` (required) + exactly one of: `node_id`+`graph_name` (jump to node), `function_name` (open function graph), or `variable_name` (select in My Blueprint panel).

### Enhanced
- `bp_describe_graph` and `bp_search` node output now include a `node_guid` field (the real NodeGuid) alongside the existing `id`/`nid` short alias. Use describe/search to find a node, read `node_guid`, then pass it directly to `bp_set_breakpoint` or `bp_focus_node`.

## v1.7.0 — Blueprint Troubleshooting Trio

### Added
- `bp_health_check` — aggregate Blueprint diagnostics in one call: compile errors/warnings, unconnected required pins, broken member references, and orphan (unreachable) nodes; returns an overall `healthy` verdict. Token-controlled via `checks` filter + `limit`.
- `bp_diff` — structural comparison of two Blueprints across parent class, components, variables, functions, interfaces, and overrides; reports only_in_a / only_in_b / differs per aspect. Automates the "broken BP vs reference BP" comparison workflow.
- `bp_trace_value` — data-flow trace from a node pin, following non-exec links upstream (what drives a value) or downstream (what it feeds), bounded by `max_depth` with cycle guards.

> Note: implemented and compile-verified; live runtime verification pending an editor reload (UE recompiles the plugin DLL on next launch).

## v1.6.0 — Blueprint Component Property Inspection

### Added
- `bp_get_component_details` command — read each component's template reflected properties for a Blueprint: Mobility, relative transform (location/rotation/scale), absolute flags (bAbsoluteLocation/Rotation/Scale), visibility, and for primitives Simulate Physics, gravity, collision profile/enabled, mesh asset, and material overrides.
  - Covers BOTH the Blueprint's own SCS components AND inherited components (via the generated-class CDO), de-duplicated by name; each entry tagged `source: scs|inherited`.
  - Token-conscious controls: `component` (filter to one), `props` (group filter: transform,mobility,physics,rendering,mesh,collision), `include_inherited` (default true).
  - Closes the troubleshooting gap where `bp_get_summary` showed only the component hierarchy — now Mobility/Absolute/Physics that drive "component stuck at origin / not following actor" issues are directly readable.

## v1.5.0 — Blueprint Class Member Inspection

### Added
- `bp_get_class_members` command — inspect a Blueprint or native C++ class's members (functions, variables, macros, delegates, interfaces) with full inheritance-chain attribution.
  - Returns `inheritance_chain` (each entry tagged blueprint/native + module/blueprint_path), up to UObject.
  - Members grouped by owning class — distinguishes which class in the chain declares each function/variable/interface/delegate.
  - Token-conscious controls: `scope` (self|chain|owner:<Class>), `kinds` filter (functions,variables,macros,delegates,interfaces), `detail` (compact|full), `limit` (default 200, sets `truncated:true` when exceeded). `counts` always reports full per-owner totals cheaply so callers can size queries before drilling in.
  - Resolves both Blueprint asset paths and native C++ class names (e.g. `ACarPawn`).

## v1.4.0 — HTTP Robustness

### Fixed
- **Portfile self-healing**: FTSTicker heartbeat (4s interval) re-writes portfile if deleted externally. Prevents permanent "No portfiles found" after CLI bug or antivirus deletion.
- **HTTP concurrency**: Replaced single-thread serial accept loop with bounded worker pool (max 4). `/ready` and lightweight commands now respond immediately even while long game-thread commands execute.
- **Safe shutdown drain**: StopServer() waits up to 2s for in-flight workers before deleting portfile. Prevents use-after-free and ensures portfile is removed cleanly.
- **Thread-safety**: `/ready`, `ping`, `list_tools`, `get_protocol_info` audited and confirmed safe to execute on worker threads without game-thread access.

---

## v1.3.0 — Blueprint Component System Enhancement

### Added
- `bp_add_component` supports `parent` parameter for hierarchical component attachment.
- `bp_remove_component` command for removing components from SCS; child components auto-re-attach.
- `bp_get_summary` returns component hierarchy with `parent` and `children` fields.
- `bp_create` supports three parent class formats: C++ class name, Blueprint class name with `_C` suffix, or Blueprint asset path.
- `bp_create_node` supports `Class::Function` shorthand for automatic CallFunction node creation.
- `bp_create_node` supports `K2Node_EnhancedInputAction` (via `input_action` parameter).
- `bp_create_node` supports `K2Node_DynamicCast` (via `target_class` parameter).
- Added `InputBlueprintNodes` module dependency for Enhanced Input Blueprint nodes.

### Fixed
- Fixed Blueprint component recompile crash by using `SkipGarbageCollection`.
- Fixed pure virtual function crash when adding Blueprint-derived components to SCS.

---

## v0.8.0 — CLI Migration

- Removed TCP Server / ConnectionManager / SessionManager.
- HTTP Server switched to dynamic port + portfile discovery.
- `/ready` endpoint + startup 503 guard.
- StatusIndicator rewritten as CLI-aware dot + copy CLI command button.
- New `smithue-cli` npm package replaces MCP: `npm install -g smithue-cli`.

---

## v0.6.0 — N-id Sessions, Metrics, Blueprint Preview

### Added
- N-id session system: short ID (N0, N1, ...) to GUID mapping per Blueprint graph, with expiry detection.
- Command metrics: call counts, request/response bytes, execution time, retry detection, per-command stats.
- `system_get_metrics` / `system_reset_metrics` commands.
- `take_blueprint_preview_screenshot`: capture any Blueprint's SCS viewport as PNG.
- Editor state guard: reject non-read-only commands while PIE is running.
- Blueprint atomic API expansion: ~1600 lines of new Blueprint operation primitives.

### Fixed
- Fixed `FTSTicker::FDelegateHandle` type for UE 5.2+ compatibility.
- Added `RHI` module dependency in Build.cs.

# SmithUE Changelog

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

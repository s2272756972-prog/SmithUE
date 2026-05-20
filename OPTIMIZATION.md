# SmithUE Plugin — Optimization Roadmap

> Analyzed against codebase commit `00e1d2a` (UE 5.7 branch).  
> Three research agents audited: MCP bridge (`src/`), C++ execution layer (`Source/SmithUE/Private/`), and tool-schema token efficiency.

---

## 1. User Deployment Experience

### 1.1 One-Click Setup Script (P2)

**Problem:** Users must manually install Node.js, locate `dist/bundle.js`, and hand-edit `opencode.json`. The bundle path is effectively hard-coded per machine.

**Recommendation:**
- Ship a `Scripts/install.ps1` (Windows) and `Scripts/install.sh` (macOS/Linux) that:
  1. Detect the UE project root automatically.
  2. Run `npm ci` inside `Scripts/SmithUE-MCP/`.
  3. Write a correct `opencode.json` next to the `.uplugin`.
- Add an **Editor Utility Widget** (`SmithUESetupWidget`) that surfaces MCP connection status (green/red) and a "Configure" button inside the UE editor, so users never need to touch JSON files.

### 1.2 Configurable Host / Port (P2)

**Problem:** `localhost:13721` is hard-coded in `SmithUEClient` constructor (`src/client.ts`) and in the C++ HTTP server startup. Multi-instance or remote-debug scenarios are impossible.

**Recommendation:**
- Expose `SMITHUE_HOST` / `SMITHUE_PORT` environment variables read at MCP startup (`src/index.ts`).
- Mirror on the C++ side: read from a `DefaultSmithUE.ini` section `[SmithUE.Transport]` `Port=13721`.

### 1.3 Graceful Reconnect Notification (P2)

**Problem:** When the UE editor restarts, the MCP keepalive (8 s interval) silently re-registers the session. The model receives a generic "unreachable" error on the in-flight request with no guidance.

**Recommendation:**
- In `tools.ts`, catch the `unreachable` error and return a structured message:  
  `"UE Editor is restarting — retry in ~10 s"` instead of a raw exception.
- Optionally expose a `/api/v1/events` SSE endpoint on the C++ side so the MCP client can subscribe to editor lifecycle events (started, shutting down).

---

## 2. Model Call Accuracy

### 2.1 Restore `default` and `itemsType` Fields in MCP Output (P0 — trivial fix)

**Problem:** `FSmithUEToolSchema::ToJsonSchema()` serializes both `default` and `itemsType` for every parameter. However, `tools.ts` maps params to only `{name, type, description, required}`, silently dropping the other two fields.

**Impact:** The model cannot infer default values or array element types, leading to malformed `params` objects and unnecessary retries.

**Fix** (`Scripts/SmithUE-MCP/src/tools.ts`, ~1 line):
```typescript
// Before
{ name: p.name, type: p.type, description: p.description, required: p.required }

// After
{
  name: p.name,
  type: p.type,
  description: p.description,
  required: p.required,
  ...(p.default    ? { default: p.default }       : {}),
  ...(p.itemsType  ? { itemsType: p.itemsType }   : {}),
}
```

### 2.2 Structured Enum Constraints for Allowed Values (P1)

**Problem:** Enumerated values (e.g., `blend_mode`, `shading_model`, `renderer_type`, `viewport mode`) are embedded as free text inside `description` strings. The model must parse natural language to discover valid values, and typos cause silent failures.

**Recommendation:**
- Add `TArray<FString> AllowedValues` to `FSmithUEToolParam` (`SmithUEToolSchema.h`).
- Serialize as `"enum": ["sprite","mesh","ribbon"]` in `ToJsonSchema()`.
- Forward the `enum` field in the MCP mapping (tools.ts).
- Migrate the highest-error-rate params first: `renderer_type`, `blend_mode`, `shading_model`, `viewport mode`, `track_type`, `value_type` (Niagara), `stack_group`.

### 2.3 Per-Command Schema Validation Before Execution (P1)

**Problem:** `smithue_execute` accepts `params` as a free-form `record<string, unknown>`. There is no pre-flight validation against the registered `FSmithUEToolSchema`, so the model can pass structurally wrong params and only learn of the error after a round-trip to the GameThread.

**Recommendation:**
- Add a `smithue_validate` meta-tool (or inline validation inside `smithue_execute`) that checks required fields and types against the cached schema before dispatching the HTTP request.
- Return a structured validation error immediately (no GameThread round-trip) listing missing/wrong fields.

---

## 3. Token Consumption

### 3.1 Current Design Is Good — Preserve It

The 3-meta-tool architecture (`smithue_execute`, `smithue_search`, `smithue_list_domain`) is the right approach. The model receives ~300 tokens of tool metadata at startup instead of the 18,000–54,000 tokens that full schema registration would cost. **Do not regress this.**

### 3.2 Lightweight Domain Index for `smithue_list_domain` (No-Arg Call) (P1)

**Problem:** Calling `smithue_list_domain` with no argument currently returns the full tool list for all 20 domains — potentially thousands of tokens — when the model only needs to know which domains exist.

**Recommendation:**
- When called with no argument, return a compact index:
  ```json
  [
    { "domain": "Blueprint", "count": 16, "summary": "Create/edit Blueprint assets, graphs, variables, components" },
    { "domain": "Material",  "count": 20, "summary": "Create/edit UMaterial assets, expressions, parameters" },
    ...
  ]
  ```
- Full schemas are returned only when a specific domain name is passed (current behavior preserved).
- Implement on the C++ side: add a `list_domains` command that calls `GetAllCategories()` + per-category count + a one-line summary string registered alongside each domain.

### 3.3 Result Limit for `smithue_search` (P1)

**Problem:** `smithue_search` has no `limit` parameter. A broad keyword (e.g., `"create"`) can match 30+ tools and return their full schemas in one response.

**Recommendation:**
- Add optional `limit` param (default `5`, max `20`).
- Sort results by relevance (exact name match > description match) before truncating.

---

## 4. Command Execution Latency

### 4.1 Heavy Commands Must Not Block the GameThread Synchronously (P0)

**Problem (root cause):** Every registered handler executes on `ENamedThreads::GameThread` via `FSmithUEDispatcher::DispatchSync`. When `POST /api/v1/execute` is used (the only path the MCP client currently calls), the HTTP server thread blocks waiting for the GameThread to finish. For heavy operations this means:

| Command | Typical blocking time |
|---|---|
| `bp_compile_code` | 1–10 s (DSL parse + node creation + `CompileBlueprint`) |
| `pcg_generate` | 0.5–30 s (depends on graph complexity) |
| `niagara_compile` | 1–5 s |
| `create_niagara_system` + emitter | 0.5–2 s |
| `analyze_module` (large dir) | 2–15 s |

**Recommendation — automatic async downgrade:**
1. Define a `HeavyCommands` set in `FSmithUEDispatcher` (or `SmithUEToolRegistry`):
   ```
   bp_compile_code, bp_compile, pcg_generate, niagara_compile,
   niagara_add_emitter, analyze_module, analyze_blueprints, map_check_errors
   ```
2. In `SmithUEHttpServerRunnable::RouteRequest`, when `POST /api/v1/execute` receives a heavy command, automatically route it through `DispatchAsync` and return `{"status":"success","data":{"task_id":"<guid>"}}` immediately.
3. In `SmithUEClient` (`src/client.ts`), add `executeAsync(command, params)` that calls `POST /api/v1/async` and polls until done (see §5.1).

### 4.2 Per-Command Execution Timeout (P1)

**Problem:** There is a 30 s HTTP *receive* timeout (in `ReceiveHttpRequest`) but no timeout on how long a handler may run on the GameThread. A hung or infinite-loop handler will block the editor indefinitely.

**Recommendation:**
- In `FSmithUEDispatcher::DispatchSync`, wrap the `Future.Get()` wait with a configurable timeout (default 60 s):
  ```cpp
  if (!Future.WaitFor(FTimespan::FromSeconds(TimeoutSeconds))) {
      // Mark task as timed-out, return error JSON
  }
  ```
- Expose `timeout_seconds` as an optional param on `POST /api/v1/execute` so callers can override per-request.

---

## 5. Long-Running Task Wait Experience

### 5.1 MCP-Layer Auto-Polling (Model-Transparent Async) (P0)

**Problem:** `POST /api/v1/async` exists on the C++ side and `DispatchAsync` works correctly, but `SmithUEClient.execute()` only calls the synchronous `/api/v1/execute` endpoint. The model has no built-in way to handle async task IDs.

**Recommendation — transparent polling in `tools.ts`:**
```typescript
// In smithue_execute handler (tools.ts)
const result = await client.execute(command, params);
if (result.data?.task_id) {
  // Heavy command was auto-downgraded to async — poll transparently
  return await client.pollUntilDone(result.data.task_id, {
    intervalMs: 500,
    timeoutMs: 120_000,
    onProgress: (p) => { /* optionally stream progress to model */ }
  });
}
return result;
```
Add `pollUntilDone()` to `SmithUEClient` (`src/client.ts`):
```typescript
async pollUntilDone(taskId: string, opts: PollOptions): Promise<SmithUEExecuteResponse> {
  const deadline = Date.now() + opts.timeoutMs;
  while (Date.now() < deadline) {
    const r = await this.request('GET', `/api/v1/async/${taskId}`);
    if (r.data?.completed) return r;
    if (opts.onProgress) opts.onProgress(r.data);
    await sleep(opts.intervalMs);
  }
  throw new Error(`Task ${taskId} timed out after ${opts.timeoutMs}ms`);
}
```
The model calls `smithue_execute` exactly as before and receives the final result — the async round-trip is invisible.

### 5.2 Structured Progress Reporting (P1)

**Problem:** `GET /api/v1/async/{id}` returns only `{completed: false}` while a task is running. The model (and user) have no visibility into progress.

**Recommendation — extend `AsyncTaskResults`:**

C++ side — replace the current `FString` result store with a struct:
```cpp
struct FSmithUEAsyncTaskState {
    bool      bCompleted   = false;
    float     Progress     = 0.f;   // 0.0 – 1.0
    FString   Stage;                // e.g. "Compiling Blueprint nodes..."
    int64     ElapsedMs    = 0;
    TArray<FString> Logs;
    TSharedPtr<FJsonObject> Result; // set on completion
};
```

Add `FSmithUEDispatcher::UpdateTaskProgress(TaskId, Progress, Stage, Log)` callable from inside handlers.

HTTP response while running:
```json
{
  "status": "success",
  "data": {
    "completed": false,
    "progress": 0.45,
    "stage": "Compiling Blueprint nodes...",
    "elapsed_ms": 1800,
    "logs": ["Parsed DSL: 12 nodes", "Created EventGraph nodes"]
  }
}
```

### 5.3 Task Cancellation (P2)

**Problem:** Once `DispatchAsync` posts work to the GameThread there is no way to cancel it. Long PCG or compilation tasks cannot be aborted.

**Recommendation:**
- Add `TMap<FGuid, FThreadSafeBool> CancelFlags` to `FSmithUEDispatcher`.
- Expose `DELETE /api/v1/async/{id}` on the HTTP server — sets the cancel flag.
- Instrument heavy handlers to check `FSmithUEDispatcher::IsCancelled(TaskId)` at natural checkpoints (e.g., between node creation steps in `SmithUEBpCompiler`, between PCG graph steps).

---

## Priority Matrix

| Priority | Item | Effort | Impact |
|---|---|---|---|
| 🔴 **P0** | MCP transparent async polling (§5.1) | Small — JS only | Eliminates model timeouts on heavy ops |
| 🔴 **P0** | Auto-downgrade heavy commands to async (§4.1) | Medium — C++ + JS | Unblocks GameThread; editor stays responsive |
| 🔴 **P0** | Restore `default` + `itemsType` in MCP output (§2.1) | Trivial — 1 line JS | Reduces param errors immediately |
| 🟡 **P1** | Structured progress reporting (§5.2) | Medium — C++ | Visibility into long tasks |
| 🟡 **P1** | Enum `AllowedValues` → structured `enum` field (§2.2) | Medium — C++ | Eliminates value-typo errors |
| 🟡 **P1** | Lightweight domain index for no-arg `smithue_list_domain` (§3.2) | Small — C++ + JS | Saves ~2k tokens per discovery call |
| 🟡 **P1** | `smithue_search` result limit (§3.3) | Trivial — JS | Prevents token spikes on broad searches |
| 🟡 **P1** | Per-command execution timeout (§4.2) | Small — C++ | Prevents editor hangs |
| 🟢 **P2** | One-click setup script + Editor widget (§1.1) | Medium | Lowers onboarding friction |
| 🟢 **P2** | Configurable host/port via env var + ini (§1.2) | Small | Enables multi-instance / remote |
| 🟢 **P2** | Graceful reconnect error message (§1.3) | Trivial — JS | Better UX on editor restart |
| 🟢 **P2** | Task cancellation API (§5.3) | Medium — C++ | Nice-to-have for long PCG/compile jobs |

---

## Appendix: Key File Reference

| File | Role |
|---|---|
| `Scripts/SmithUE-MCP/src/client.ts` | HTTP client — add `executeAsync`, `pollUntilDone`, retry/backoff |
| `Scripts/SmithUE-MCP/src/tools.ts` | MCP tool registration — fix param mapping, add polling, add `smithue_validate` |
| `Scripts/SmithUE-MCP/src/types.ts` | Type definitions — add `PollOptions`, extend `SmithUEToolParam` |
| `Source/SmithUE/Private/Utils/SmithUEDispatcher.cpp` | Add `UpdateTaskProgress`, `IsCancelled`, timeout on `Future.Get()` |
| `Source/SmithUE/Public/Utils/SmithUEDispatcher.h` | Expose new dispatcher API |
| `Source/SmithUE/Private/Transport/SmithUEHttpServerRunnable.cpp` | Add `DELETE /api/v1/async/{id}`, auto-downgrade heavy commands |
| `Source/SmithUE/Public/ToolRegistry/SmithUEToolSchema.h` | Add `AllowedValues` field to `FSmithUEToolParam` |
| `Source/SmithUE/Private/ToolRegistry/SmithUEToolRegistry.cpp` | Add `list_domains` command, serialize `enum` field |

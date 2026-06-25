# SmithUE Changelog

## 未发布（当前 `UE5.2` 分支）

### 修复：材质工具自描述（对齐 TOOL_SPEC §3.1）
- **`connect_material_pins`** 的 `dest_input_index` 描述补全 `7=WorldPositionOffset`（此前只列 0–6；WPO/顶点偏移连不上只能翻源码 `case 7`）。8+ 标注不支持。
- **`set_expression_property`** 描述按节点类型列出合法 `properties` 键（Constant=`value` 而非 `R`、Constant3Vector=`r/g/b`、ScalarParameter、VectorParameter、TextureSample、Custom 等）；失败错误从无指向的 "No recognized properties were set" 改为**回显该节点的合法键**（新增 `GetSettablePropertyKeys()` 助手）。
- **`PITFALLS.md #15`** 沉淀该盲区；新增原则：description 写工具契约（索引、键名），不写 UE 版本特有的引擎 API（HLSL intrinsic 等），避免随版本漂移误导。
- `TOOLS.md` 已重生成（仅这两条描述变化，工具数不变 221）。

## v1.11.0（UE5.2，2026-06-25）

### 修复：工具描述隐藏边界 → AI 误判防护（Anti-Misjudgment）
- **`bp_compile_code`**：描述写明"只编译函数图、不支持事件/嵌套 if/裸 math"；`ValidateSyntax` 加窄事件守卫（仅检测签名行首 token `event`，或解析失败且含规范 UE 事件名），失败时返回重定向到原子节点工作流（`bp_override_function → bp_create_node → bp_describe_graph → bp_batch_op → bp_compile`），**不拦截**合法函数名（如 `void Tick()` 可正常编译）。
- **13 个工具描述补充边界信息**（全文字改动，无行为变更）：`bp_validate_code`（只读/不编译）、`bp_batch_op`（节点操作需 bp_path+graph_name）、`bp_get_class_members`/`bp_get_component_details`（枚举白名单）、`bp_focus_node`（互斥目标）、`create_data_asset`（需具体子类）、`add_widget`（需面板父级）、`bp_create`/`bp_create_node`/`bp_connect_pins`/`bp_disconnect_pins`（易混对消歧）、`bp_create_node`/`bp_delete_node`（nid_stale 警告）、`level_new`/`level_open`（延迟执行）。
- **附带修复**：`CompileFunction` 中 `NewResult->AllocateDefaultPins()` 被重复调用（`Finalize()` 后再次显式调用），导致 FunctionResult 节点有两个 `execute` 引脚，`FindPin("execute")` 歧义返回 nullptr，所有函数体 exec 连接静默失败。已移除多余的显式调用。
- **`docs/spec/TOOL_SPEC.md §3.1`** 新增强制规范：description 必须在被挑选时暴露硬边界，常见误用错误必须可操作，沉淀为长期开发门控。
- **`docs/spec/PITFALLS.md #14`**：记录本次踩坑；**#13** 更新为正确的 `NormalizeContentBrowserPath` 指引（旧版 `/All` 字符串截断对插件路径错误）。
- **`TOOLS.md`** 已从 `/api/v1/tools` 重新生成（14 个工具描述变更，工具总数不变 221）。

### 新增：启动环境自检 + Status & Updates 设置面板
- **`FSmithUECliChecker`**：编辑器启动后后台检测 Node / npm / smithue-cli 环境（off game thread，写回 game thread，每步写入 `LogSmithUE`）。语义化版本比较 + 兼容地板 `kRecommendedCliVersion=0.13.0`。
- **「Status & Updates」设置面板**（项目设置 → 插件 → SmithUE）：查看环境状态、插件更新提醒（GitHub Releases 链接 + Restart-to-Update 按钮）、一键安装/升级 CLI；按钮随状态自适应（未安装/升级/已最新/取消），Node 缺失时给 nodejs.org 链接。拉取式（TAttribute）刷新，无订阅。
- **有界安装**：CLI 安装走 `CreateProc` + 管道 + 轮询，**120s 硬超时 + 可取消 + npm 快速失败参数**（`--fetch-timeout=60000 --fetch-retries=1`），网络不佳不假死；失败分类提示（权限/网络/超时）+ 手动兜底；安装与检测使用独立 in-flight 标志。

### 变更
- 启动时不再弹更新 toast；插件更新提醒移入设置面板。`level_new` 等不变。

### 修复：Content Browser 选区/路径返回虚拟路径
- **`get_content_browser_selection`** 的 `selected_folders` 此前直接返回引擎虚拟路径（`/All/Game/BP`、`/All/Plugins/Foo/BP`），下游无法直接当作包路径使用。现统一规范化为真实包路径（`/Game/BP`、`/Foo/BP`），并新增 `selected_folders_virtual` 字段保留原始虚拟路径用于调试。
- 新增 `FSmithUECommonUtils::NormalizeContentBrowserPath()`：基于官方 `UContentBrowserDataSubsystem::TryConvertVirtualPath` 转换（同时正确处理工程 `/Game` 与插件挂载路径），失败时回退（`/All`→`/Game`、`/All/Game..` 截断），绝不静默丢弃。
- 合并此前散落在 4 处的 `/All` 字符串截断逻辑（`SmithUEBpAtomicAPI.cpp` ×3、`SmithUEAssetAuditCommands.cpp`）统一改调用该 helper；`SmithUE.Build.cs` 新增 `ContentBrowserData` 模块依赖。

## v1.10.0

### 新增工具（合计 221 工具 / 24 个领域）
- **`get_asset_property`**：通用 UObject 属性读取器（set_asset_property 的读对偶）
- **`scan_assets`**：文件夹作用域资产扫描，返回 v1 linter 元数据（命名/路径/父类/材质槽/LOD/碰撞）
- **`bp_describe_components`**：Blueprint 组件树读回，供合规 linter 比对（继承盲区显式标注）
- **`spawn_mesh_actor`**：向当前世界生成带网格 + 材质的 StaticMeshActor（补 spawn_actor 无法赋网格/材质的缺口）
- **`level_add_basic_env`**：向当前世界一键添加基础环境（定向光 / 天空 / 天光 / 雾 / 地板 / PlayerStart）
- **`livecoding_status` / `livecoding_compile`**：Live Coding 热编译状态查询与同步触发（新增 LiveCoding 领域）

### 修复
- **`level_new` 崩溃修复**：将同步 `NewBlankMap` 改为经 `GEngine->DeferredCommands` 延迟到下一帧安全点执行，避免在 HTTP handler 内 tick 期间销毁世界导致崩溃（复刻 level_open 的延迟模式）。
- **材质 WorldPositionOffset 输出引脚**：`connect_material_pins` 的 `dest_input_index=7` 正确映射到 WPO 引脚。

### 架构演进
- 企业级资产装配与合规标准化引擎定位正式确立
- 三层分离：C++ 原子原语 / smithue-cli 规范引擎 / 宿主工程 spec 文件
- `docs/usage/` 使用文档 hub 建立（与 `docs/spec/` 开发文档拆分）

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

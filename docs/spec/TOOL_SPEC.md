# SmithUE 工具开发规范 (TOOL_SPEC)

> 给本插件新增/修改"工具"(tool)时的强制规范。新增工具前**必读**本文件 + [PITFALLS.md](PITFALLS.md)。

## 1. 架构契约

```
AI 工具 / smithue-cli  (TypeScript, npm)
        │  HTTP JSON  (动态端口, 端口文件发现)
        ▼
SmithUE UE5 插件  (C++, HTTP Server, 工具注册表)
        │  UE 反射 API
        ▼
虚幻引擎 5.2 编辑器
```

- **CLI 与插件是两个独立产品**,各自独立版本号。**禁止**直接比较两者版本号(见 PITFALLS #1)。
- 兼容性以 **HTTP 协议契约**为准,不是版本号。

## 2. 命令(工具)命名约定

| 规则 | 示例 |
|------|------|
| 动词前缀表达意图 | `create_`, `read_`, `get_`, `set_`, `list_`, `add_`, `delete_`, `sync_` |
| 同域工具用域名前缀(可选) | `bp_*`(Blueprint)、`pie_*`(PIE)、`data_*`(Data)、`niagara_*` |
| 小写 + 下划线 | `create_render_target` |
| create 必须有对应的 read | `create_curve` ↔ `read_curve` |

> 详细命名规范(参数规范字典、待修正的不一致、为路由而写的 description)见 [NAMING.md](NAMING.md)。

## 3. Schema 与响应约定

### 注册 Schema
```cpp
Registry.Register(
    FSmithUEToolSchema(
        TEXT("tool_name"),
        TEXT("Domain"),          // 功能域(决定 list/search 归类)
        TEXT("一句话描述"),
        {
            FSmithUEToolParam(TEXT("param"), TEXT("string"), TEXT("说明"), /*required*/ true),
            FSmithUEToolParam(TEXT("opt"),   TEXT("int"),    TEXT("可选说明")) // 默认 required=false
        }),
    &HandleToolName);
```

### Handler 签名与响应
```cpp
TSharedPtr<FJsonObject> FXxxCommands::HandleToolName(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    if (!FSmithUECommonUtils::ValidateRequiredParams(Params, {TEXT("param")}, Error))
        return FSmithUECommonUtils::CreateErrorResponse(Error);
    // ...
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("key"), Value);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);  // {status:"success", data:{...}}
}
```

- 成功:`CreateSuccessResponse(Data)` → `{"status":"success","data":{...}}`
- 失败:`CreateErrorResponse(Msg)` → `{"status":"error","error":"..."}`
- **绝不**直接 `process.exit` / 抛异常逃逸;统一走响应对象。

## 3.1 工具描述与错误必须自描述边界（AI 防误判强制规范）

> 本节是强制规范（MUST）。新增或修改工具时，必须满足以下所有条件，否则不得合并。

### 描述（description）规范

**描述必须在"被挑选的那一刻"暴露工具的硬边界**，不能把边界藏在 `.cpp` 实现里。

- **硬边界必须写明**：handler 拒绝的输入类型、仅支持的子集、必须满足的前提条件。  
  ❌ 差：`"Compile Blueprint DSL into a Blueprint"`  
  ✅ 好：`"Compile the limited Blueprint FUNCTION-graph DSL ... FUNCTION graphs ONLY — no events (Tick/BeginPlay/Overlap/input)..."`

- **易混工具对必须区分**：read-only vs 可变、资产级别 vs 图内节点、互为反向操作的工具对，description 中必须有明确的单行区分语。

- **静默部分成功 / 延迟执行 / stale id 必须标注**：  
  - 返回 `data.success=false` 的情况：在 description 里说明（"check data.success"）。  
  - 延迟到下一帧的操作：说明"success = queued, not finished"。  
  - 返回会变脏的 id：说明"ids go stale after graph mutation — re-run bp_describe_graph"。

### 错误字符串规范

**面向 AI 的常见误用错误必须可操作（Actionable）**，即：
- 说明正确的工具或下一步操作，不只是 `"Invalid X"` / `"Failed to Y"`。
- 典型范例：事件式 DSL 传入 `bp_compile_code` 时，返回的错误应重定向到原子节点工作流，而不是裸 `"Invalid function signature"`。

### 参考案例

本规范由 PITFALLS #14（见 [PITFALLS.md](PITFALLS.md)）驱动，修复了 `bp_compile_code` 等 14 个工具的误判陷阱。典型模式：

| 陷阱类型 | 工具示例 | 修复方式 |
|----------|----------|----------|
| 描述隐藏硬边界 | `bp_compile_code`（只支持函数图，不支持事件） | description 写明 "FUNCTION graphs only; no events — use atomic nodes" |
| 错误无指向 | DSL 错误不说替代工具 | 错误字符串追加 "…or build with bp_batch_op + bp_create_node" |
| 易混工具对 | `bp_compile_code` vs `bp_validate_code`（一个变更，一个只读） | description 标注 "read-only, no mutation" vs "MUTATES the Blueprint" |
| 静默部分成功 | `bp_compile_code` 外层 success 但 `data.success=false` | description 标注 "check data.success" |
| 延迟执行 | `level_new`/`level_open` 排队到下一帧 | description 标注 "success = QUEUED, not finished" |

## 4. 线程安全(关键)

- HTTP handler **默认在 game thread** 执行(可安全访问 GEditor / UObject)。
- 只有**只读且不碰 UObject** 的命令才可标记 worker-safe。
- **`/ready` 等 worker 线程路径禁止访问 GEditor/UObject** → 需要的状态用 `FThreadSafeBool` 在 game thread 缓存,worker 线程只读(见 PITFALLS #10,pie_active 实现)。

## 5. 加一个工具 — 6 步检查清单

1. **声明** handler:`Source/SmithUE/Public/Commands/Xxx.h`
2. **注册** schema:`Xxx::RegisterTools()` 里 `Registry.Register(...)`
3. **实现** handler:`Source/SmithUE/Private/Commands/Xxx.cpp`
4. **模块注册**(仅新命令类):`SmithUEModule.cpp` 加 `#include` + `Xxx::RegisterTools(...)` 调用
5. **模块依赖**(仅新引擎类型):`SmithUE.Build.cs` 补依赖(查清类属于哪个模块,见 PITFALLS #4)
6. **文档**:重新生成 `TOOLS.md` + 更新 `README.md` 计数(用 codegen,见第 7 节)
7. **自描述检查**:描述写明硬边界（handler 不接受什么）;常见误用错误可操作、指向正确工具或替代工作流 — 见 §3.1

## 6. 构建 / 验证循环

```
关闭编辑器(释放 DLL 锁)→ UBT 编译 → 启动编辑器 → 实时验证 create+read → 提交
```
- 编辑器运行时无法链接 DLL(见 PITFALLS #8)。
- UBT 偶尔报 "Target is up to date" 不重编 → 删 `Intermediate/Build/Win64/x64/<Target>/ActionHistory.bin`(见 PITFALLS #9)。
- 每个工具至少验证一次 create + read 的真实往返。

## 7. 文档自动生成 (codegen)

工具列表是**自描述**的(`/api/v1/tools` 运行时输出全部 schema)。文档应从运行时拉取生成,**不手写**:
- 用 Node 脚本(非 PowerShell,见 PITFALLS #6)从 `http://127.0.0.1:<port>/api/v1/tools` 拉取,生成 `TOOLS.md`。
- README 的工具/域计数随之更新。

## 8. 契约防漂移(建议)

- 提交一份 `list_tools` 输出的 golden 快照。
- CLI 侧加测试 diff 实际 vs golden → 工具被误删/改名/换参立刻报红(可挡住 list/search 那类漂移,见 PITFALLS #2)。

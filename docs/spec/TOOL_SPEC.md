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

# AGENTS.md — SmithUE 插件开发指南

> 给 AI 代理 / 贡献者的项目导航。**动手前先读本文件,新增工具前必读 [Spec](docs/spec/)。**

## 项目是什么

SmithUE 是虚幻引擎 5.2 编辑器插件,通过 HTTP 协议把编辑器全部功能暴露成"工具"(tool),由 `smithue-cli`(独立 npm 包)调用,服务于 AI 驱动的编辑器自动化。当前 **209 个工具 / 23 个功能域**。

```
AI 工具 / smithue-cli (TS, npm)  →  HTTP JSON  →  SmithUE 插件 (C++)  →  UE 反射 API  →  UE 5.2 编辑器
```

## 规范与沉淀(必读)

| 文件 | 内容 | 何时读 |
|------|------|--------|
| [docs/spec/TOOL_SPEC.md](docs/spec/TOOL_SPEC.md) | 工具开发规范:命名、Schema、响应、线程安全、加工具 6 步清单 | **新增/改工具前** |
| [docs/spec/PITFALLS.md](docs/spec/PITFALLS.md) | 踩坑沉淀:抽象类、模块依赖、PowerShell 编码、线程安全等 13 条 | **新增/改工具前** |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献流程 | 提交前 |
| [TOOLS.md](TOOLS.md) | 全部工具参考(自动生成) | 查工具 |

## 关键约束(高频踩坑,详见 PITFALLS)

- **CLI 与插件独立版本**,不要直接比版本号(契约以 HTTP 协议为准)。
- **含中文的文件禁止用 PowerShell 编辑**(会损坏 UTF-8),用 Node 或编辑器工具。
- **编译插件前必须关闭编辑器**(DLL 锁):关 → 编 → 启 → 验。
- **worker 线程(/ready 等)不能碰 UObject/GEditor**,状态用 `FThreadSafeBool` 在 game thread 缓存。
- **创建资产前检查目标类是否 abstract**(UDataAsset/UPrimaryDataAsset 都是抽象的)。
- **新引擎类型查清模块归属**(如 UPhysicalMaterial 在 PhysicsCore,不在 Engine)。

## 开发命令

```powershell
# 编译插件(先关编辑器)
Stop-Process -Name UnrealEditor -Force
dotnet "<UE>/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" AIScriptEditor Win64 Development "<proj>.uproject" -WaitMutex

# CLI 开发(F:\DXProject\AIScript\smithue-cli)
npm test          # vitest
npm run build     # tsc
npm run typecheck

# 验证工具(编辑器运行后)
smithue-cli status --terse
smithue-cli list --terse
smithue-cli exec <tool> '{...}' --terse
```

## 加一个工具(速查,完整见 TOOL_SPEC §5)

1. `Public/Commands/Xxx.h` 声明 handler
2. `Xxx::RegisterTools()` 注册 schema
3. `Private/Commands/Xxx.cpp` 实现 handler
4. `SmithUEModule.cpp` 注册命令类(仅新类)
5. `SmithUE.Build.cs` 补模块依赖(仅新引擎类型)
6. 重新生成 TOOLS.md + 更新 README 计数

## 目录速览

```
Source/SmithUE/
  Public/Commands/   工具 handler 声明
  Private/Commands/  工具 handler 实现(按域分文件)
  Private/Transport/ HTTP Server + 端口文件 + /ready
  Private/UI/        编辑器状态指示器
  SmithUE.Build.cs   模块依赖
docs/spec/           开发规范 + 踩坑沉淀(本指南引用)
```

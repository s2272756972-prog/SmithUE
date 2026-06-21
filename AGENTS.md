# AGENTS.md — SmithUE 插件开发指南

> 给 AI 代理 / 贡献者。动手前读本文件;新增/改工具前必读 [docs/spec/](docs/spec/) 与 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 这是什么 / 仓库边界(最易踩错)

- 本目录是 **SmithUE 插件**(C++,UE 5.2)的**独立 git 仓库**:remote `123dx-svg/SmithUE`,**默认分支 `UE5.2`(不是 main)**。
- 插件嵌在宿主工程内:`F:\DXProject\AIScript\Plugins\SmithUE`;宿主工程 `F:\DXProject\AIScript\AIScript.uproject`。
- **消费端 `smithue-cli` 是另一个独立仓库**:`F:\DXProject\AIScript\smithue-cli`(TypeScript/npm,remote `123dx-svg/smithue-cli`,分支 `main`,已发布 npm 包)。改 CLI 去那个仓库,别在这里找。
- 插件 ↔ CLI 走 HTTP JSON,**动态端口**(端口文件 `%LOCALAPPDATA%\.smithue\<pid>.port`,**不是固定 13721**——CONTRIBUTING 里的 13721 仅为示例)。两者**独立版本号,禁止互相比较**。

## 构建插件(顺序固定:关 → 编 → 启 → 验)

```powershell
Stop-Process -Name UnrealEditor -Force   # 编辑器锁 DLL,必须先关
dotnet "E:\Program Files\Epic Games\UE_5.2\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" `
  AIScriptEditor Win64 Development "F:\DXProject\AIScript\AIScript.uproject" -WaitMutex
# 之后重新启动编辑器即重新加载插件
```

- 编译目标是 **`AIScriptEditor`**(宿主工程 target),不是 "SmithUE"。
- UBT 误报 "Target is up to date" 不重编时 → 删 `Intermediate\Build\Win64\x64\AIScriptEditor\ActionHistory.bin` 强制重建。

## 验证工具(编辑器运行后)

```bash
smithue-cli status --terse
smithue-cli list --terse
smithue-cli exec <tool> '{...}' --terse
```

## 测试(两套,分属两个仓库)

- **插件**:C++ **UE Automation 测试**(`Source/SmithUE/Private/Tests/*.cpp`,基于 `Misc/AutomationTest.h`),在编辑器内经 Automation 框架运行,**不是命令行直跑**。
- **CLI**:在 `smithue-cli` 仓库:`npm test`(vitest)、`npm run build`(tsc)、`npm run typecheck`。ESM/NodeNext(本地 import 必须带 `.js`),**npm-only(无 bun)**。

## 加一个工具

权威步骤见 **[CONTRIBUTING.md](CONTRIBUTING.md) → "How to Add a New Command"**;命名/响应/线程安全约定与检查清单见 **[docs/spec/TOOL_SPEC.md](docs/spec/TOOL_SPEC.md)**。要点:

- `Public/Commands/Xxx.h` 声明 → `RegisterTools()` 注册 `FSmithUEToolSchema` → `Private/Commands/Xxx.cpp` 实现 → **仅新命令类**才改 `SmithUEModule.cpp` → **仅新引擎类型**才改 `SmithUE.Build.cs`。
- 改完重新生成 `TOOLS.md` + 更新 `README.md` 计数:**用 Node 脚本拉 `/api/v1/tools` 生成,不要手写**(原因见 PITFALLS)。

## 高频踩坑(完整 13 条见 [docs/spec/PITFALLS.md](docs/spec/PITFALLS.md))

- **PowerShell 5.1 会损坏含中文(CJK)的文件**(按 GBK 误读 UTF-8)→ 改中文文件用 Node(`fs ... 'utf8'`,中文用 `\uXXXX` 转义)或编辑器工具;损坏后 `git checkout -- <file>` 恢复。
- **PowerShell 向 node 传 JSON 会吞引号 / 按空格拆参** → 用 `node -e "...execCommand('cmd', {obj}, {})"` 绕过 shell。
- **worker 线程(`/ready` 等)不能碰 `GEditor`/UObject** → 状态用 `FThreadSafeBool` 在 game thread 缓存后只读。
- **创建资产前查目标类是否 `abstract`**(`UDataAsset`/`UPrimaryDataAsset` 都是抽象的,需具体子类)。
- **新引擎类型查清模块归属**(如 `UPhysicalMaterial` 在 `PhysicsCore`,不在 `Engine`)。

## 目录速览

```
Source/SmithUE/Public/Commands/    工具 handler 声明
Source/SmithUE/Private/Commands/   工具 handler 实现(按功能域分文件)
Source/SmithUE/Private/Transport/  HTTP Server + 端口文件 + /ready
Source/SmithUE/Private/Tests/      C++ Automation 测试
SmithUE.Build.cs                   模块依赖
docs/spec/                         TOOL_SPEC + PITFALLS(新增/改工具前必读)
```

# SmithUE

<p align="center">
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

<p align="center">
  <a href="README.md">简体中文</a> | <a href="README.en.md">English</a>
</p>

一款高性能虚幻引擎编辑器插件，通过命令协议公开编辑器的全部功能，将重复的手动操作转变为可脚本化、AI 驱动的工作流。

---

## For AI Agents

### 快速部署（Windows）
```bash
# 确认编辑器已启动并启用 SmithUE 插件
npx smithue-cli status
npx smithue-cli exec ping '{}'
npx smithue-cli list
```

### 故障树
1. 编辑器未运行 → 启动虚幻引擎编辑器并等待完全加载
2. 多实例 → 使用 `npx smithue-cli status --pid <pid>` 指定实例
3. 端口文件已过期 → 运行 `npx smithue-cli prune` 清理

---

## 目录

1. [快速入门](#快速入门)
2. [架构](#架构)
3. [命令参考](#命令参考)
4. [连接状态指示器](#连接状态指示器)
5. [AI 纹理生成](#ai-纹理生成)
6. [更新日志](#更新日志)
7. [路线图](#路线图)
8. [已知限制](#已知限制)
9. [贡献](#贡献)
10. [许可证](#许可证)

---

## 快速入门

1. 将仓库克隆到项目的 `Plugins/` 目录，并切换到 `UE5.2` 分支：
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
   ```

2. 使用虚幻引擎 5.2 构建项目。

3. 启动编辑器。SmithUE 将自动启动 HTTP 服务器并分配动态端口。

4. 验证连接：
   ```bash
   npx smithue-cli status
   ```

**注意**：SmithUE 现在使用 `smithue-cli` 进行交互。详情请访问：https://github.com/123dx-svg/smithue-cli

---

## 架构

```
AI 工具 (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ smithue-cli (npx smithue-cli exec/list/search/status)
SmithUE UE5 插件 (HTTP :动态端口)
     ↕ UE 反射 API
虚幻引擎 5.2 编辑器
```

---

## 命令参考

SmithUE 提供了分布在 **19 个功能域** 中的 **178 个工具**。命令集正在持续扩展。请使用 `npx smithue-cli list` 查看最新可用命令，或参阅 [TOOLS.md](TOOLS.md) 获取完整参考。

### 功能域概览

| 功能域 | 工具数 | 描述 |
|---|---|---|
| System | 5 | 服务器连接、会话管理 |
| Project | 4 | 项目信息、插件、目录、源文件 |
| Material | 20 | 材质、材质实例、MPC、材质函数 |
| Asset | 12 | 资产增删改查、浏览器操作、AI 纹理生成 |
| Editor | 8 | Actor 生成、属性、后处理、项目设置 |
| Interaction | 7 | 控制台/编辑器命令、撤销/重做、按键模拟 |
| Blueprint | 17 | 蓝图创建、节点、函数、变量、组件、DSL 编译器 |
| Viewport | 6 | 摄像机控制、截图、Actor 选择 |
| Observation | 7 | 面板、编辑器状态、Actor 属性、世界大纲 |
| Analysis | 13 | 源码分析、依赖图、蓝图诊断、资产校验 |
| Niagara | 17 | 粒子系统创建、发射器、模块、渲染器 |
| Level | 11 | 关卡管理、地形、植被 |
| Data | 6 | 数据表、用户定义结构体、用户定义枚举 |
| Sequencer | 6 | 关卡序列创建、绑定、轨道、关键帧 |
| Environment | 11 | 后处理、雾、天空、光照、物理、样条 |
| PIE | 11 | 运行模式：启动/停止、Actor、属性、控制台 |
| Animation | 7 | 动画蒙太奇、动画蓝图、段落、通知 |
| Input | 6 | 增强输入：InputAction、InputMappingContext |
| UMG | 4 | 控件蓝图创建、控件树、属性 |

---

## 连接状态指示器

SmithUE 在编辑器状态栏显示一个圆形指示器，实时反映连接状态。

### 指示器颜色

| 颜色 | 状态 |
|---|---|
| 🟢 绿色 | 就绪 — HTTP 服务器运行中，可通过 CLI 连接 |
| 🟡 黄色 | 就绪但 PIE 运行中 — 部分写命令被锁定 |
| ⚫ 灰色 | 未就绪 — 服务器未启动或正在初始化 |

### 交互功能

- **Tooltip 悬停**：显示项目名称、端口号、PIE 状态
- **点击圆点**：复制端口号到剪贴板

---

## AI 纹理生成

`generate_texture` 命令将编辑器与现代生成式 AI 连接。它根据提供的端点 URL 自动检测 API 格式，支持 DALL-E、Imagen 和基于 OpenAI Chat 的生成。

**使用示例：**
```bash
npx smithue-cli exec generate_texture '{"params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## 更新日志

### v0.8.0，CLI 迁移
- 移除 TCP Server / ConnectionManager / SessionManager
- HTTP Server 改为动态端口 + 端口文件发现
- `/ready` 端点 + 启动期 503 守卫
- StatusIndicator 重写为 CLI-aware 小圆点 + 复制 CLI 命令按钮
- 新 smithue-cli npm 包替代 MCP：`npm install -g smithue-cli`
- 参见：https://github.com/123dx-svg/smithue-cli

### v0.6.0，N-id 会话、度量、蓝图预览与编辑器保护

**新增功能：**
- N-id 会话系统：每个蓝图图表的短 ID (N0, N1, ...) 到 GUID 映射，包含过期检测。
- 命令度量：调用计数、请求/响应字节数、执行耗时、重试检测及单个命令统计。
- `system_get_metrics` / `system_reset_metrics`：查询并重置会话度量数据。
- `take_blueprint_preview_screenshot`：将任意蓝图的 SCS (组件) 视口捕获为 PNG。
- 编辑器状态保护：在 PIE 运行时拒绝非只读命令。
- 蓝图原子 API 扩展：新增约 1600 行蓝图操作原语。
- MCP 服务：增强的工具调度逻辑。

**修复：**
- 修正 `FTSTicker::FDelegateHandle` 类型以兼容 UE 5.2+。
- 在 Build.cs 中添加 `RHI` 模块依赖。

### v1.3.0，蓝图组件系统增强

**新增功能：**
- `bp_add_component` 支持 `parent` 参数，实现层级组件挂载。
- `bp_remove_component` 命令，用于从 SCS 中移除组件。子组件将自动重新挂载。
- `bp_get_summary` 返回包含 `parent` 和 `children` 字段的组件层级结构。
- `bp_create` 支持三种父类格式：C++ 类名、带有 `_C` 后缀的蓝图类名或蓝图资产路径。
- `bp_create_node` 支持 `Class::Function` 简写以自动创建 CallFunction 节点。
- `bp_create_node` 支持 `K2Node_EnhancedInputAction`（通过 `input_action` 参数）。
- `bp_create_node` 支持 `K2Node_DynamicCast`（通过 `target_class` 参数）。
- 为增强输入蓝图节点添加了 `InputBlueprintNodes` 模块依赖。

**Bug 修复：**
- 通过使用 `SkipGarbageCollection` 修复了蓝图组件重编译崩溃。
- 修复了向 SCS 添加蓝图派生组件时的纯虚函数崩溃。

---

## 路线图

- **v0.7**，蓝图深度
  - 完成用于生成 EventGraph 和 FunctionGraph 的 DSL 编译器。
  - 通过命令实现蓝图调试工具，包括断点、步进和变量观测。
  - 覆盖所有常用节点类型的完整 K2Node 支持。

- **v0.8**，功能域扩展
  - AI、导航及行为树相关命令。
  - 世界分区 (World Partition) 与关卡流送 (Level Streaming) 命令。
  - PCG (程序化内容生成) 相关命令。
  - 游戏技能系统 (Gameplay Ability System) 命令。
  - 物理与碰撞配置命令。

- **v0.9**，性能与稳定性
  - 异步命令执行流水线，用于非阻塞长耗时操作。
  - 支持批量操作，可在单个请求中执行多个命令。
  - 事务系统，支持多命令原子回滚。
  - 增强的错误恢复与诊断功能。

- **v1.0**，多客户端与 SDK
  - 支持并发会话的多客户端协作模式。
  - 可通过 `pip install smithue` 安装的 Python SDK。
  - 包含 OpenAPI 规范的 REST API 文档。
  - 用于实时编辑器事件的 WebSocket 流。

---

## 已知限制

- **并发性**：仅支持单客户端连接。
- **输入焦点**：`simulate_key` 需要视口处于焦点状态。
- **属性类型**：`set_actor_property` 不支持 `TMap` 或委托等高级类型。嵌套结构体属性需使用专用命令。
- **场景纹理**：`SceneTextureId` 通过 FProperty 反射设置，以避免未导出引擎符号的链接错误。
- **操作系统**：仅限 Windows Win64。

---

## 贡献

请参阅 [CONTRIBUTING.md](CONTRIBUTING.md) 获取添加新命令的详细步骤。

### AI 辅助开发技能

SmithUE 在 `Docs/smithue-dev/SKILL.md` 提供了一份 AI 开发技能文件。该文件旨在教会 AI 编码助手如何为本项目做出贡献。

**安装：**

将技能目录复制到 AI 工具的技能路径：

```bash
# OpenCode (项目级)
cp -r Plugins/SmithUE/Docs/smithue-dev {YourProject}/.agents/skills/

# OpenCode (用户级)
cp -r Plugins/SmithUE/Docs/smithue-dev ~/.agents/skills/

# Claude Code
cp -r Plugins/SmithUE/Docs/smithue-dev ~/.claude/skills/
```

---

## 历史：MCP 服务已停用
MCP 服务已在 v0.8.0 中移除，由 `smithue-cli` 替代。迁移指南请参见 [smithue-cli MIGRATION.md](https://github.com/123dx-svg/smithue-cli/blob/main/MIGRATION.md)。

---

## 卸载

完全卸载前，建议先运行 `smithue-cli purge` 清理 `%LOCALAPPDATA%\.smithue\` 目录中的残留文件。

```bash
# 预览将被删除的内容（不实际删除）
smithue-cli purge --dry-run

# 非交互式完整清理（适用于脚本/CI）
smithue-cli purge -y
```

`purge` 命令会在删除前进行存活检测，并在目录为符号链接时拒绝操作以保障安全。完整参数说明请参阅 [smithue-cli README — Uninstall](https://github.com/123dx-svg/smithue-cli#uninstall)。

清理完成后，再执行：
```bash
npm uninstall -g smithue-cli
```

---

## 许可证

Copyright 2026, 123dx-svg. MIT License.

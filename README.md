# SmithUE

<p align="center">
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

<p align="center">
  <a href="README.md">简体中文</a> | <a href="README.en.md">English</a>
</p>

一款高性能虚幻引擎编辑器插件，通过命令协议与 MCP 集成公开编辑器的全部功能，将重复的手动操作转变为可脚本化、AI 驱动的工作流。

---

## 目录

1. [快速入门](#快速入门)
2. [架构](#架构)
3. [MCP 服务](#mcp-服务)
4. [命令参考](#命令参考)
5. [连接状态指示器](#连接状态指示器)
6. [AI 纹理生成](#ai-纹理生成)
7. [更新日志](#更新日志)
8. [路线图](#路线图)
9. [已知限制](#已知限制)
10. [贡献](#贡献)
11. [许可证](#许可证)

---

## 快速入门

1. 将仓库克隆到项目的 `Plugins/` 目录，并切换到 `UE5.2` 分支：
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
   ```

2. 使用虚幻引擎 5.2 构建项目。

3. 启动编辑器。SmithUE 将在 13721 端口自动启动 HTTP 服务器。

4. 验证连接：
   ```bash
   curl -X POST http://localhost:13721/api/v1/execute -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
   ```

**注意**：MCP 服务已预构建于 `Scripts/SmithUE-MCP/dist/bundle.js`。仅需 Node.js 18+ 运行环境，无需额外编译。

---

## 架构

```
AI 工具 (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ MCP stdio
SmithUE MCP 服务 (Scripts/SmithUE-MCP/)
     ↕ HTTP :13721
SmithUE UE5 插件
     ↕ UE 反射 API
虚幻引擎 5.2 编辑器
```

---

## MCP 服务

SmithUE 包含一个基于 TypeScript 的 MCP (Model Context Protocol) 服务，用于连接 AI 工具与 UE5 插件。它采用了元工具架构，仅注册 3 个固定工具，而非逐个注册每个命令。这使得无论命令数量如何增加，AI 上下文占用始终保持在约 300 tokens。

### 上下文占用对比

| 方案 | N 个工具 | 200 个工具 | 1000 个工具 |
|---|---|---|---|
| 全量注册 | 约 6.5K tokens | 约 20K tokens | 约 100K tokens |
| 元工具 (SmithUE) | 约 300 tokens | 约 300 tokens | 约 300 tokens |

### 3 个元工具

| 工具名称 | 描述 |
|---|---|
| `smithue_list_domain` | 列出域或获取特定域的完整命令模式 |
| `smithue_search` | 按关键词搜索命令 |
| `smithue_execute` | 执行带有参数的任意命令 |

### 安装与运行

MCP 服务已预构建，开箱即用。使用以下命令启动（需确保虚幻编辑器已运行并启用 SmithUE 插件）：

```bash
node Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js serve
```

环境变量：
- `SMITHUE_PORT`：HTTP 端口（默认：`13721`）
- `SMITHUE_HOST`：主机地址（默认：`localhost`）
- `SMITHUE_CLIENT_NAME`：连接指示器显示的客户端名称（默认：`OpenCode`）

### AI 工具配置

#### OpenCode

在项目根目录的 `opencode.json` 中添加：

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "smithue": {
      "type": "local",
      "command": ["node", "{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
    }
  }
}
```

#### Claude Code

```bash
claude mcp add smithue -- node {YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js serve
```

#### GitHub Copilot

创建或更新 `.github/copilot-mcp.json`：

```json
{
  "mcpServers": {
    "smithue": {
      "command": "node",
      "args": ["{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
    }
  }
}
```

#### Cline

在 VSCode 设置中的 `cline.mcpServers` 下添加：

```json
{
  "smithue": {
    "command": "node",
    "args": ["{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
  }
}
```

### 工作流示例

1. `smithue_list_domain()`：查看全部 19 个功能域。
2. `smithue_list_domain("Material")`：获取材质相关的命令模式。
3. `smithue_search("blueprint")`：搜索蓝图相关命令。
4. `smithue_execute("create_material", {"name": "M_Test", "path": "/Game/Materials"})`：执行命令。

---

## 命令参考

SmithUE 提供了分布在 **19 个功能域** 中的 **178 个工具**。命令集正在持续扩展。请使用 `smithue_list_domain()` 查看最新可用命令，或参阅 [TOOLS.md](TOOLS.md) 获取完整参考。

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

SmithUE 在编辑器状态栏显示一个圆形指示器，实时反映连接健康状态。

### 指示器颜色

| 颜色 | 状态 |
|---|---|
| 🟢 绿色 | 已连接 — 至少一个 MCP 客户端会话活跃 |
| 🔴 红色 | 未连接 — 无活跃会话 |
| 🟡 红/黄闪烁 | 状态变更中 — 会话正在连接或断开（持续 3 秒） |
| 🔵 青/绿脉冲 | 有新版本可用 — 检测到更新的 SmithUE 版本 |

### Tooltip 悬停详情

将鼠标悬停在指示器上可查看：

- **SmithUE 版本**（从 `.uplugin` 读取）
- **连接数** — 当前活跃的 MCP 会话数量
- **工具数** — 已注册的命令总数
- **客户端列表** — 每个已连接 AI 工具的名称及会话时长（分钟）
- **更新提示** — 有新版本时显示当前版本 → 最新版本

### 会话管理

- 超过 20 秒无心跳的过期会话会每 5 秒自动清理。
- 每当会话数量发生变化（连接或断开）时，指示器会闪烁 3 秒。

---

## AI 纹理生成

`generate_texture` 命令将编辑器与现代生成式 AI 连接。它根据提供的端点 URL 自动检测 API 格式，支持 DALL-E、Imagen 和基于 OpenAI Chat 的生成。

**使用示例：**
```bash
curl -X POST http://localhost:13721/api/v1/execute \
  -H "Content-Type: application/json" \
  -d '{"command":"generate_texture","params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## 更新日志

### v0.7.0，蓝图节点搜索与批量原子事务

**新增功能：**
- `bp_search`：按名称（子字符串，大小写不敏感）和/或节点类型搜索蓝图图表中的节点，支持 `verbose`（返回完整 pin 信息）和 `limit` 参数，搜索覆盖所有图表（事件图、函数图、宏图）。
- `bp_batch_op` 新增 `atomic` 模式（opt-in，`"atomic": true`）：先进行 dry-run 静态验证，再以单一 `FScopedTransaction` 执行全部操作，任一步骤失败时自动全量回滚，蓝图状态恢复原样。`bp_compile` 不纳入回滚范围。

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

## 许可证

Copyright 2026, 123dx-svg. MIT License.

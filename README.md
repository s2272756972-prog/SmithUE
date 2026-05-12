# UEAgent

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

UEAgent is a high-performance Unreal Engine editor plugin designed to bridge the gap between creative intent and engine execution. It provides a robust command-driven interface that allows external tools and AI agents to manipulate the Unreal Editor directly, bypassing the need for manual menu navigation and repetitive Blueprint wiring. By exposing the engine's internal capabilities through a structured protocol, it empowers developers to automate tedious editor tasks and build intelligent co-pilot systems.

UEAgent 是一款高性能虚幻引擎编辑器插件, 旨在搭建创意意图与引擎执行之间的桥梁. 它提供了一个强大的命令驱动接口, 允许外部工具和 AI 智能体直接操纵虚幻编辑器, 从而无需手动导航菜单和重复的蓝图连线. 通过结构化协议公开引擎内部功能, 它使开发人员能够自动化繁琐的编辑器任务, 并构建智能辅助驾驶系统.

---

## Table of Contents / 目录

1. [Vision / 愿景](#vision--愿景)
2. [Quick Start / 快速入门](#quick-start--快速入门)
3. [Protocol / 协议](#protocol--协议)
4. [Command Reference / 命令参考](#command-reference--命令参考)
5. [AI Texture Generation / AI 纹理生成](#ai-texture-generation--ai-纹理生成)
6. [Roadmap / 路线图](#roadmap--路线图)
7. [Known Limitations / 已知限制](#known-limitations--已知限制)
8. [License / 许可证](#license--许可证)

---

## Vision / 愿景

Unreal Engine developers spend significant time on repetitive manual operations: searching for assets, dragging actors into levels, configuring material parameters, and manually connecting Blueprint nodes. UEAgent transforms the editor into a programmable environment. Imagine generating a complete level layout from a text description, automatically analyzing complex module dependencies, or creating sophisticated Blueprint logic via external scripts. UEAgent frees you from the friction of the UI, allowing you to focus on high-level design and architectural decisions.

虚幻引擎开发人员在重复的手动操作上花费了大量时间: 搜索资产, 将 Actor 拖入关卡, 配置材质参数以及手动连接蓝图节点. UEAgent 将编辑器转变为可编程环境. 想象一下根据文本描述生成完整的关卡布局, 自动分析复杂的模块依赖关系, 或通过外部脚本创建复杂的蓝图逻辑. UEAgent 将您从 UI 的摩擦中解放出来, 允许您专注于高级设计和架构决策.

---

## Quick Start / 快速入门

1. Copy the UEAgent folder into your Unreal Engine project's `Plugins/` directory.
   将 UEAgent 文件夹复制到虚幻引擎项目的 `Plugins/` 目录中.

2. Build your project using Unreal Engine 5.2 or later.
   使用虚幻引擎 5.2 或更高版本构建项目.

3. Launch the Unreal Editor. UEAgent will automatically initialize the command servers on TCP port 13720 and HTTP port 13721.
   启动虚幻编辑器. UEAgent 将在 TCP 端口 13720 和 HTTP 端口 13721 上自动初始化命令服务器.

4. Verify the connection using the provided PowerShell script:
   使用提供的 PowerShell 脚本验证连接:
   ```powershell
   .\Scripts\Send-UEAgent.ps1 -Command "ping"
   ```

---

## Protocol / 协议

UEAgent uses a standard JSON format for all requests and responses, supporting both high-performance raw TCP sockets and standard HTTP requests.

UEAgent 在所有请求和响应中使用标准 JSON 格式, 同时支持高性能原始 TCP 套接字和标准 HTTP 请求.

**Request Format / 请求格式:**
```json
{
  "cmd": "command_name",
  "params": {
    "param_key": "param_value"
  }
}
```

**Response Format / 响应格式:**
```json
{
  "status": "success",
  "data": { ... }
}
```

---

## Command Reference / 命令参考

UEAgent provides 64 specialized commands organized across 8 functional domains.

UEAgent 提供了分布在 8 个功能领域的 64 个专业命令.

### System / 系统 (8 commands)
Commands for managing project state, settings, and filesystem.
用于管理项目状态, 设置和文件系统的命令.

| Command | Description / 描述 |
| :--- | :--- |
| `ping` | Check server availability and latency / 检查服务器可用性和延迟 |
| `list_tools` | List all registered commands with parameters / 列出所有已注册命令及其参数 |
| `get_protocol_info` | Retrieve protocol version and capabilities / 获取协议版本和功能 |
| `get_project_info` | Get project metadata and paths / 获取项目元数据和路径 |
| `list_plugins` | List all enabled plugins / 列出所有启用的插件 |
| `get_project_settings` | Retrieve project-level configuration / 获取项目级配置 |
| `create_folder` | Create a new directory in Content Browser / 在内容浏览器中创建新目录 |
| `get_source_files` | List C++ source files for project modules / 列出项目模块的 C++ 源文件 |

### Asset / 资产 (7 commands)
Manage assets and integrate external AI generation.
管理资产并集成外部 AI 生成.

| Command | Description / 描述 |
| :--- | :--- |
| `list_assets` | List assets with optional filtering / 列出资产并支持可选过滤 |
| `find_asset` | Search for assets by name or tag / 按名称或标签搜索资产 |
| `get_asset_info` | Retrieve detailed asset metadata / 获取详细的资产元数据 |
| `rename_asset` | Rename asset with redirect fixup / 重命名资产并修复重定向 |
| `duplicate_asset` | Create a copy of an asset / 创建资产副本 |
| `generate_texture` | Async texture generation via AI API / 通过 AI API 异步生成纹理 |
| `check_generation_task` | Poll status of texture generation / 轮询纹理生成状态 |

### Material / 材质 (7 commands)
Programmatic material creation, node graph assembly, property configuration and compilation.
程序化材质创建, 节点图组装, 属性配置与编译.

| Command | Description / 描述 |
| :--- | :--- |
| `create_material` | Create Material or MaterialInstance / 创建材质或材质实例 |
| `get_material_info` | Inspect material graph structure / 检查材质图表结构 |
| `set_material_property` | Set material-level properties (domain, blend mode, shading model, two-sided, blendable location) / 设置材质级属性 (域, 混合模式, 着色模型, 双面, 可混合位置) |
| `set_expression_property` | Set expression node properties (Custom HLSL code/output type/inputs, Constant, Constant3Vector RGB, SceneTexture ID) / 设置表达式节点属性 (自定义 HLSL 代码/输出类型/输入, 常量, 三维常量向量 RGB, 场景纹理 ID) |
| `add_material_expression` | Add expression node to material / 向材质添加表达式节点 |
| `connect_material_pins` | Link material expression pins / 连接材质表达式引脚 |
| `compile_material` | Trigger material compilation / 触发材质编译 |

### Editor / 编辑器 (12 commands)
Level stage control and actor lifecycle management.
关卡舞台控制和 Actor 生命周期管理.

| Command | Description / 描述 |
| :--- | :--- |
| `spawn_actor` | Place a new actor into the level / 在关卡中放置新 Actor |
| `get_all_actors` | Retrieve all actors with transforms / 获取所有 Actor 及其变换 |
| `set_actor_property` | Set UPROPERTY value on an actor / 设置 Actor 的 UPROPERTY 值 |
| `delete_actor` | Remove an actor from the level / 从关卡中移除 Actor |
| `add_postprocess_material` | Add a material to a PostProcessVolume's blendable list / 向后处理体积的可混合列表添加材质 |
| `execute_editor_command` | Run internal editor commands / 运行内部编辑器命令 |
| `execute_console_command` | Run standard console commands / 运行标准控制台命令 |
| `list_editor_commands` | Enumerate available editor commands / 枚举可用的编辑器命令 |
| `undo` | Perform an undo operation / 执行撤销操作 |
| `redo` | Perform a redo operation / 执行重做操作 |
| `simulate_key` | Send keyboard input to the editor / 向编辑器发送键盘输入 |
| `list_key_bindings` | List editor keyboard shortcuts / 列出编辑器键盘快捷键 |

### Blueprint / 蓝图 (13 commands)
Deep integration for programmatic Blueprint construction and DSL compilation.
用于程序化蓝图构建和 DSL 编译的深度集成.

| Command | Description / 描述 |
| :--- | :--- |
| `bp_create` | Create a new Blueprint class / 创建新的蓝图类 |
| `bp_add_function` | Add function graph to Blueprint / 向蓝图添加函数图表 |
| `bp_create_node` | Instantiate a node in Blueprint graph / 在蓝图图表中实例化节点 |
| `bp_connect_pins` | Connect two Blueprint pins / 连接两个蓝图引脚 |
| `bp_set_pin_default` | Set default value for a pin / 设置引脚的默认值 |
| `bp_add_variable` | Add member variable to Blueprint / 向蓝图添加成员变量 |
| `bp_add_component` | Add component to Blueprint SCS / 向蓝图 SCS 添加组件 |
| `bp_compile` | Compile Blueprint and report errors / 编译蓝图并报告错误 |
| `bp_get_summary` | Rich Blueprint structural summary (function signatures with params/return types, node counts, custom events, delegates, input bindings, interfaces, variable flags, component hierarchy) / 丰富的蓝图结构摘要 (函数签名含参数/返回类型, 节点数, 自定义事件, 委托, 输入绑定, 接口, 变量标志, 组件层级) |
| `bp_describe_graph` | Compact graph description with short IDs (N0-Nn), ~38% size reduction vs GUIDs, multi-connection support, id_to_guid mapping / 紧凑图表描述使用短 ID (N0-Nn), 比 GUID 减少约 38% 体积, 多连接支持, id_to_guid 映射 |
| `bp_compile_code` | Compile DSL text into Blueprint / 将 DSL 文本编译为蓝图 |
| `bp_batch_op` | Execute multiple atomic operations / 执行多个原子操作 |
| `bp_validate_code` | Validate DSL syntax dry-run / 验证 DSL 语法的空运行 |

### Viewport / 视口 (6 commands)
Camera control, selection, and visualization state.
摄像机控制, 选择和可视化状态.

| Command | Description / 描述 |
| :--- | :--- |
| `set_viewport_camera` | Set viewport transform and FOV / 设置视口变换和 FOV |
| `focus_on_actor` | Focus camera on a specific actor / 将摄像机聚焦于特定 Actor |
| `set_viewport_mode` | Switch projection modes / 切换投影模式 |
| `get_viewport_info_detailed` | Detailed viewport state snapshot / 详细的视口状态快照 |
| `select_actors` | Manage actor selection in level / 管理关卡中的 Actor 选择 |
| `take_viewport_screenshot` | Capture viewport as PNG / 将视口捕获为 PNG |

### Observation / 8 commands
State inspection and world hierarchy analysis.
状态检查和世界层级分析.

| Command | Description / 描述 |
| :--- | :--- |
| `list_panels` | List editor panel visibility / 列出编辑器面板可见性 |
| `open_panel` | Focus a named editor panel / 聚焦命名的编辑器面板 |
| `close_panel` | Close an open editor panel / 关闭打开的编辑器面板 |
| `get_editor_state` | General editor state snapshot / 通用编辑器状态快照 |
| `get_level_info` | Metadata for current map / 当前地图的元数据 |
| `get_actor_property` | Read UPROPERTY value from actor / 从 Actor 读取 UPROPERTY 值 |
| `get_selected_actors` | Get data for current selection / 获取当前选择的数据 |
| `get_world_outline` | Hierarchical world actor list / 层级化的世界 Actor 列表 |

### Analysis / 分析 (3 commands)
Structural analysis with nomnoml diagram output.
带有 nomnoml 图表输出的结构分析.

| Command | Description / 描述 |
| :--- | :--- |
| `analyze_module` | C++ class relationship analysis / C++ 类关系分析 |
| `analyze_dependencies` | Module dependency graph analysis / 模块依赖图分析 |
| `analyze_blueprints` | Blueprint hierarchy and composition / 蓝图层级和组合分析 |

---

## AI Texture Generation / AI 纹理生成

The `generate_texture` command bridges the editor with modern generative AI. It automatically detects the target API format based on the provided endpoint URL, supporting DALL-E, Imagen, and OpenAI Chat-based generation.

`generate_texture` 命令将编辑器与现代生成式 AI 连接起来. 它根据提供的端点 URL 自动检测目标 API 格式, 支持 DALL-E, Imagen 和基于 OpenAI Chat 的生成.

**Example Usage / 使用示例:**
```powershell
.\Scripts\Send-UEAgent.ps1 -Command "generate_texture" -Params '{
  "prompt": "seamless stylized stone floor, hand-painted style, 4K",
  "endpoint": "https://api.openai.com/v1/images/generations",
  "api_key": "sk-...",
  "model": "dall-e-3",
  "save_path": "/Game/Textures",
  "asset_name": "T_StoneFloor"
}'
```

---

## Roadmap / 路线图

We are committed to expanding UEAgent into a comprehensive AI-first development toolkit, with a focus on digital twin and simulation workflows.

我们致力于将 UEAgent 扩展为全面的 AI 优先开发工具包, 聚焦数字孪生与仿真工作流.

*   **v1.1**
    *   MCP (Model Context Protocol) support for direct LLM integration.
        支持 MCP (模型上下文协议), 实现与大语言模型的直接集成.
    *   Blueprint logic generation - AI-driven EventGraph / FunctionGraph construction from natural language.
        蓝图逻辑生成 - 从自然语言驱动 AI 自动构建事件图 / 函数图.
    *   ~~Material Blueprint generation - programmatic material node network assembly.~~
        ~~材质蓝图生成 - 程序化材质节点网络组装.~~
        ✅ **Done in v1.0.1** — Full material creation pipeline: `create_material` → `set_material_property` → `add_material_expression` → `set_expression_property` → `connect_material_pins` → `compile_material` → `add_postprocess_material`. Verified with a thermal vision post-process material end-to-end.
        ✅ **已在 v1.0.1 完成** — 完整材质创建流水线: `create_material` → `set_material_property` → `add_material_expression` → `set_expression_property` → `connect_material_pins` → `compile_material` → `add_postprocess_material`. 已通过热成像后处理材质端到端验证.
*   **v1.2**
    *   Digital twin scene construction - procedural environment generation from data sources (GIS, point cloud, CAD).
        数字孪生场景构建 - 从数据源 (GIS, 点云, CAD) 程序化生成环境.
    *   Sensor simulation commands - LiDAR, camera, radar actor spawning and configuration.
        传感器仿真命令 - LiDAR, 摄像头, 雷达 Actor 的生成与配置.
    *   Python binding / REST SDK.
        Python 绑定 / REST SDK.
*   **v1.3**
    *   Runtime game state observation (PIE mode commands).
        运行时游戏状态观察 (PIE 模式命令).
    *   Live data streaming - real-time external data injection into simulation actors.
        实时数据流 - 将外部实时数据注入仿真 Actor.
    *   Multi-client collaboration mode.
        多客户端协作模式.
*   **v2.0**
    *   Full AI copilot mode - natural language → Blueprint compilation pipeline.
        全 AI 辅助驾驶模式 - 自然语言 → 蓝图编译流水线.
    *   Scenario orchestration - programmatic traffic / pedestrian / weather control for simulation.
        场景编排 - 程序化交通 / 行人 / 天气控制用于仿真.

---

## Known Limitations / 已知限制

*   **Concurrency**: Single-client connection only.
    并发性: 仅支持单客户端连接.
*   **Input Focus**: `simulate_key` requires viewport focus.
    输入焦点: `simulate_key` 需要视口焦点.
*   **Property Types**: Advanced types like `TMap` or delegates are not supported by `set_actor_property`. Nested struct properties (e.g. `Settings.WeightedBlendables`) require dedicated commands.
    属性类型: `set_actor_property` 不支持 `TMap` 或委托等高级类型. 嵌套结构体属性 (如 `Settings.WeightedBlendables`) 需使用专用命令.
*   **SceneTexture**: `SceneTextureId` is set via FProperty reflection to avoid link errors with unexported engine symbols.
    场景纹理: `SceneTextureId` 通过 FProperty 反射设置, 以避免未导出引擎符号的链接错误.
*   **OS**: Windows Win64 only.
    操作系统: 仅限 Windows Win64.

---

## License / 许可证

Copyright 2026, 123dx-svg. MIT License.
版权所有 2026, 123dx-svg. 基于 MIT 许可证发布.

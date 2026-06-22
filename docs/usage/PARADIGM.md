# SmithUE 范式：三层架构与扩展阶梯

> 本文是 SmithUE 使用端的概念入口。开发端（如何加工具）→ 见 docs/spec/TOOL_SPEC.md\u3002

## 三层架构

`
① 插件层（C++，UE5.x）        通用原子原语，HTTP /api/v1/tools
   get_asset_property / scan_assets / bp_describe_components
   bp_create / bp_add_component / bp_bulk_set_component_property …
        ↓ HTTP JSON（契约边界）
② CLI 引擎层（smithue-cli，npm） 规范解释器 + 编排
   spec 加载/校验 · 确定性分类 · factory · lint · spec infer
        ↓ spec 文件（git 可追踪文本）
③ 宿主规范层（宿主工程 git）    业务策略（不进 npm 包）
   smithue.config.json · .smithue/specs/*.json
`

**关键原则**：
- 插件零业务逻辑（litmus：另一家 studio 能否原样复用？否则拒绝）
- 规范层零代码（只有 JSON/YAML，git 追踪，AI 可读）
- 契约（HTTP /api/v1/tools）是唯一版本兼容边界

## 扩展阶梯

| 级别 | 场景 | 做什么 | 要重编? |
|---|---|---|---|
| **L0** | 换规范（不同 studio 或项目） | 写/改 .smithue/specs/*.json | 否 |
| **L1** | 新编排工作流（用现有原语） | 加 src/commands/X.ts（CLI） | 否 |
| **L2** | 需要新原子能力 | 加 C++ handler → UBT 重编 → 再 L1 编排 | 是 |

> **绝大多数企业需求落在 L0/L1**：只要工厂/linter 引擎是规范驱动的通用件，改规范不改代码。

## 第三方 Server 接入（SPI）

任何实现了 /api/v1/tools HTTP 契约的 server 都可被 smithue-cli 驱动。→ 详见 [SPI.md](./SPI.md)

## 文档导航

| 受众 | 入口 |
|---|---|
| **使用端**（AI agent / TA / 管线工程师） | 本文 + workflows/ |
| **开发端**（添加 C++ 工具） | docs/spec/TOOL_SPEC.md + CONTRIBUTING.md |
| **第三方集成**（实现 server） | SPI.md |
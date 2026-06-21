# SmithUE 命名规范 (NAMING)

> 给本插件新增/修改"工具"(tool)与参数时的**命名强制规范**。新增工具前必读本文件 + [TOOL_SPEC.md](TOOL_SPEC.md) + [PITFALLS.md](PITFALLS.md)。
>
> **为什么重要**:工具数会持续增长(已 211 / 23 域)。命名一致性直接决定 ① AI 填参准确率 ② AI 按用户意图**定位到正确工具**的命中率。本规范的取舍依据是对当前 211 个工具的命名普查。

## 0. 总原则

- Schema(`/api/v1/tools` 运行时输出)是**唯一真相**;命名一致 = AI 不靠猜。
- 命名要**为 AI 路由/填参服务**,不是为人好看。可预测 > 简短。

## 1. 工具命名 (tool name)

格式:`[<域前缀>_]<动词>_<名词>`,全小写 + 下划线。

- **动词**(表意图):`create_` / `read_`(读单个)/ `get_`(读属性)/ `list_`(读多个)/ `set_`(改)/ `add_` / `delete_`(`remove_`)/ `find_`(`search_`)/ `exec_`。
  - `create_` 必须有配对的 `read_`(沿用 TOOL_SPEC §2)。
- **域前缀**:已成体系的域**新工具必须带前缀** —— `bp_`、`niagara_`、`env_`、`level_`、`anim_`、`seq_`、`data_`、`input_`、`pie_`、`asset_`、`system_`。

### ⚠️ 现状:两种风格并存(待收敛)

普查显示工具名有两套风格:

| 风格 | 例 | 涉及域 |
|---|---|---|
| 域前缀式 | `bp_*`(40)、`niagara_*`(15)、`env_*`、`level_*`、`anim_*`、`seq_*`、`data_*`、`input_*`、`pie_*` | Blueprint/Niagara/Environment/Level/Animation/Sequencer/Data/Input/PIE |
| 动词裸名式 | `get_actor_property`、`set_actor_property`、`create_material`、`find_asset`、`spawn_actor`、`take_screenshot` | Editor/Material/Asset/Observation/Viewport/Interaction |

**决定**:
- **新工具一律域前缀式**(`<域>_<动词>_<名词>`)。
- 现存裸名式工具**祖父条款保留**;若改名,**必须保留旧名 alias 至少一个版本**(避免 list/search 漂移,见 PITFALLS #2)。

## 2. 参数命名 (param name) —— 规范字典

> **这是参数准确性的最大杠杆。** 同一概念必须同名,AI 才不会猜错。普查发现的不一致见末尾迁移表。

| 概念 | ✅ 规范名 | ❌ 禁止 / 别名 |
|---|---|---|
| Blueprint 资产路径 | `bp_path` | `blueprint_path`(待改) |
| 通用资产路径 | `asset_path` | |
| 类型化资产路径 | `<类型>_path`(`material_path` / `system_path` / `sequence_path` / `montage_path` / `mi_path` / `mpc_path` / `table_path`) | 保留——类型明确利于 AI;**新类型沿用此式** |
| 组件标识 | `component` | `component_name`(统一为 `component`) |
| Actor 标识 | `actor_label` | |
| 图(graph)名 | `graph_name` | |
| 节点 id | `node_id` | |
| 属性名 | `property_name` | |
| 值 / 默认值 | `value` / `default_value` | |
| 限制条数 | `limit` | |
| 过滤 | `filter`(通用)/ `<维度>_filter`(如 `class_filter`) | |

规则:
- **避免裸 `name`(29)/`path`(27)**:新工具用带语境的名(`asset_name`、`folder_path`、`component`…)。
- 受限取值的参数,description 里**列出枚举值**(见 §4)。

### 待修正的不一致(migration —— 改时保留旧名 alias 一个版本)

1. `blueprint_path` → `bp_path`(如 `bp_get_compile_errors` 等 4 处)。
2. `component_name` → `component`(统一)。
3. 历史裸 `name` / `path` 参数:新工具不再使用,改带语境名。

## 3. description 为"路由"而写

`search` 是对 name+description 的**字面子串匹配**;`list_tools` 也靠描述帮 AI 选。所以:

- 一句话,**先答"何时用我"**,意图关键词前置。
- 塞入**同义词**(描述里没出现的词,`search` 搜不到)。
- 反例:`Set a property on a component`。
- 正例:`Bulk-set component collision (object type + per-channel responses) — use when changing collision on many static meshes / a whole folder at once`。

## 4. Schema 完整性(参数准确性)

- 每个参数:准确的 `type` + 正确的 `required` + 清晰 description。
- **受限取值的参数必须列枚举**(`object_type`、`response`、`profile`、`mode`…)。
- **开发项(建议)**:给 `FSmithUEToolParam` 增加 `enum` 与 `example` 字段。AI 见到枚举/示例后填参错误率显著下降——这是随工具增多保证准确性的关键投入。
- 单工具参数面要**小且正交**,避免 10+ 参数的"大杂烩"工具(拆成聚焦工具)。

## 5. 域 (domain)

- 一个域 ≥ 3 个相关工具才独立成域(沿用约定)。
- 每个域应有一句"何时用"——AI 路由是**域优先收敛**(意图 → 域 → `list_tools` → 工具),域描述好,收敛快。

## 6. 新工具命名检查清单

- [ ] 工具名:`<域前缀>_<动词>_<名词>`,小写下划线
- [ ] 参数全部取自 §2 规范字典(不造新名;复用概念复用名)
- [ ] description 意图前置 + 含同义词(为 search/路由)
- [ ] 受限参数在 description 里列枚举(或用未来的 schema enum)
- [ ] `create_` 有配对 `read_`
- [ ] 改既有工具名/参数 → 保留旧名 alias 一个版本
- [ ] 重新生成 `TOOLS.md`(codegen,见 TOOL_SPEC §7)

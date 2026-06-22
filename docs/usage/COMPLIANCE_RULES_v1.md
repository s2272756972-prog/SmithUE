# SmithUE v1 合规规则集（读回保真评估）

## 结论摘要

v1 可检查规则：8 条  
v1 不可检查（需新原语）：1 条

本 spike 只锁定“现有读回原语能稳定观测”的规则类型；具体命名格式、目标目录、目标父类、组件名、碰撞 preset、Mobility 等规范值均应由下游规则配置提供，本文不固化任何 studio 具体值。读不到的规则不进入 v1 自动判定，避免 linter 静默误判通过。

测试 fixture 位于 `/Game/SmithUETest/`：

- `SM_ComplianceProbe_Cube`：由引擎 Cube 复制得到的简单 StaticMesh。
- `BP_SM_ComplianceProbe_Cube`：父类 Actor，包含一个 `StaticMeshComponent`，挂载上述 StaticMesh，碰撞 preset 读回为 `BlockAll`，Mobility 读回为 `Static`，材质槽填充为 `/Engine/BasicShapes/BasicShapeMaterial`。
- `BP_ReadbackProbe_Parent` / `BP_ReadbackProbe_Child`：用于验证父 BP SCS 组件在子 BP 上的继承读回盲区。

证据文件：

- `.omo/evidence/task-1-readback-probe.json`
- `.omo/evidence/task-1-inherited-blindspot.json`

## 规则详表

| # | 规则 | 类型 | 可观测 | 依赖原语/字段 | 备注 |
|---|---|---|---|---|---|
| 1 | BP 命名模式 | 结构 | ✓ | `get_asset_info.name` / `package_name`，`bp_get_summary.bp_path` | 可由包名或资产名执行外部模式匹配；具体模式由规则配置提供。 |
| 2 | 输出文件夹 | 结构 | ✓ | `list_assets.assets[].package_path`，`get_asset_info.package_path` | 可按文件夹作用域列资产，也可对单资产读包路径。 |
| 3 | 父/基类 | 结构 | ✓ | `bp_get_summary.parent_class`，`get_asset_info.tags.ParentClass` / `NativeParentClass` | 可检查父类白名单、必须继承某基类等规则；具体基类由规则配置提供。 |
| 4 | 必需组件名/组件类 | 组件 | ✓ | `bp_get_component_details.components[].name/class`，`bp_get_summary.components[]` | 自身 SCS 组件名称和类型可读。 |
| 5 | 碰撞 preset | 组件属性 | ✓ | `bp_get_component_details.components[].collision.profile` / `enabled` | fixture 中实际读回 `BlockAll`；目标 preset 名称由规则配置提供。 |
| 6 | Mobility | 组件属性 | ✓ | `bp_get_component_details.components[].mobility` | 可读 `Static` / `Movable` 等枚举字符串。 |
| 7 | 材质槽填充 | 组件/网格 | ✓（有限） | `bp_get_component_details.components[].materials[]`，`get_asset_info(StaticMesh).tags.Materials` | 可按 StaticMesh 材质槽数量与组件材质数组做非空检查；不提供更丰富的槽语义元数据。 |
| 8 | LOD0 存在 | 网格 | ✓ | `get_asset_info(StaticMesh).tags.LODs`，`tags.MinLOD` | AssetRegistry tag 中 `LODs >= 1` 可作为 LOD0 存在的 v1 判据。 |
| 9 | 继承组件属性（子 BP 继承父 BP 的组件） | 继承/组件属性 | ✗（需新原语） | `bp_get_component_details(..., include_inherited=true)` 当前不足 | 父 BP 上组件完整可读；对子 BP 查询时 `components: []`，即使 `include_inherited=true`。v1 不应对“有效继承组件属性”做自动通过/失败判定。 |

## 逐条读回结论

### 1. BP 命名模式

`get_asset_info` 对 BP 返回 `name`、`path`、`package_name`，`bp_get_summary` 返回 `bp_path`。因此命名模式检查可在 CLI linter 侧完成：将规则配置中的模式作用于资产名或包名即可。

### 2. 输出文件夹

`list_assets` 返回每个资产的 `package_path`，`get_asset_info` 也返回单资产 `package_path`。因此输出目录、禁止目录、必须在某目录下等结构规则可进入 v1。

### 3. 父/基类

`bp_get_summary` 对 `BP_SM_ComplianceProbe_Cube` 返回 `parent_class: /Script/Engine.Actor`；AssetRegistry tags 同时提供 `ParentClass` / `NativeParentClass`。因此父类白名单、必须继承某基类等规则可进入 v1。

### 4. 必需组件名/组件类

`bp_get_component_details` 返回 `components[].name`、`class`、`source`，fixture 的 `StaticMeshComponent` 可读。自身 SCS 组件的存在性、命名和类型规则可进入 v1。

### 5. 碰撞 preset

`bp_get_component_details` 返回 `collision.profile` 和 `collision.enabled`，fixture 读回 `profile: BlockAll`、`enabled: QueryAndPhysics`。碰撞 preset 类型规则可进入 v1。

### 6. Mobility

`bp_get_component_details` 返回 `mobility` 字段，fixture 读回 `Static`。Mobility 规则可进入 v1。

### 7. 材质槽填充

`bp_get_component_details` 返回组件 `materials[]`；`get_asset_info` 对 StaticMesh 返回 `tags.Materials`（slot 数）。因此 v1 可检查“组件材质数组数量不少于网格槽数，且每项非空”。盲区是当前读回不暴露槽显示名、用途标签或更细的 per-section 语义。

### 8. LOD0 存在

`get_asset_info` 对 StaticMesh 返回 `tags.LODs` 和 `tags.MinLOD`，fixture 读回 `LODs: 1`、`MinLOD: 0`。v1 可用 `LODs >= 1` 作为 LOD0 存在判据。

### 9. 继承组件属性

继承盲区已确认：`BP_ReadbackProbe_Parent` 的 `InheritedStaticMeshComponent` 在父 BP 上完整可读；`BP_ReadbackProbe_Child` 的 `bp_get_summary` 能读到父类路径，但 `bp_get_component_details` 对子 BP 返回空数组，`include_inherited=true` 与 `false` 都为空。

因此 v1 只检查当前 BP 自身 SCS 组件；“有效类包含从父 BP 继承来的组件及其 override 后属性”需要新读原语，或至少需要一个插件侧完成父链合并与 InheritableComponentHandler override 解析的读回命令。

## v1 盲区

- 子 BP 的父 BP SCS 继承组件：现有 `include_inherited=true` 未返回有效组件。
- 继承组件 override 后的最终属性：现有读回无法从子 BP 一次性得到最终有效值。
- 通用 UObject 属性读取：`set_asset_property` 是写原语，不能推断存在对偶 `get_asset_property`；v1 不依赖通用点路径读取。
- 材质槽语义：可检查数量与非空路径，不检查更高层语义标签。

## v1 linter 建议字段集

最小可依赖读字段：

- Asset：`name`、`path`、`package_name`、`package_path`、`class`、`tags`。
- Blueprint：`bp_path`、`parent_class`、`components[].name`、`components[].class`。
- Component：`source`、`mobility`、`collision.profile`、`collision.enabled`、`materials[]`、`mesh`。
- StaticMesh tags：`Materials`、`LODs`、`MinLOD`。

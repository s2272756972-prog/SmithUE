# 审计原语契约 Schema（/api/v1/tools）

> **版本**：v1  **日期**：2026-06-22  **依据**：`docs/usage/COMPLIANCE_RULES_v1.md`

本文定义以下三个新工具的完整请求/响应/错误 JSON schema。它是插件 C++ 实现（T5/T6）与 CLI Phase B（T8–T12）两端解耦并行的唯一契约边界。任何一方可以面向本文独立开工，无需等待对方的代码落地。

---

## 概览

| 工具名 | 类别 | 用途 |
|---|---|---|
| `get_asset_property` | Asset | 按点号路径读取已加载 UObject 的任意属性，是 `set_asset_property` 的读对偶 |
| `scan_assets` | Asset | 文件夹作用域资产扫描，返回 v1 linter 所需元数据 |
| `bp_describe_components` | Blueprint | 文件夹或单 BP 的组件树读回，供 linter 对比规则 4–6 |

### 通用套路

- 成功响应一律为：`{ "success": true, "data": { ... } }`
- 错误响应一律为：`{ "success": false, "error": { "message": "..." } }`
- 路径格式一律为 UE 内容路径（`/Game/...`），不含文件系统前缀

---

## 1. `get_asset_property`

### 1.1 工具元信息

```json
{
  "name": "get_asset_property",
  "category": "Asset",
  "description": "读取已加载 UObject 的属性。支持点号分隔的多层路径（如 'LightmapSettings.ResolutionScale'）和数组索引（如 'StaticMaterials[0]'）。注：目标资产必须已加载（is_loaded=true）；未加载资产请先调用 load_asset。"
}
```

### 1.2 请求 Schema

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `asset_path` | string | 是 | — | 资产内容路径，格式 `/Game/...` |
| `property` | string | 是 | — | 点号路径，如 `LightmapCoordinateIndex` 或 `StaticMaterials[0].MaterialInterface` |

参数内控约束：
- `asset_path` 必须以 `/Game/` 开头（或其他有效 UE 挂载点）
- `property` 应为 UE 反射属性名，大小写敏感；多层用点号分隔；数组用 `[n]` 索引

### 1.3 响应 Schema（成功）

```json
{
  "success": true,
  "data": {
    "asset_path": "<string>  UE 内容路径",
    "property":   "<string>  原样返回请求中的 property 路径",
    "value":      "<any>     JSON 可序列化值：基础类型 / 对象 / 数组均可能"
  }
}
```

`value` 字段类型说明：

| 属性类型 | 返回的 JSON 类型 | 示例 |
|---|---|---|
| 整数 / 浮点 | number | `0`、`1.5` |
| 布尔 | boolean | `true` |
| 枚举 | string | `"Static"` |
| 字符串/路径 | string | `"/Game/Foo"` |
| 结构体 | object | `{"R":1,"G":0,"B":0,"A":1}` |
| 数组 | array | `["/Game/M_A", "/Game/M_B"]` |

### 1.4 响应 Schema（失败）

```json
{
  "success": false,
  "error": {
    "message": "<string>  人可读错误描述，如 'Asset not loaded' / 'Property path not found'"
  }
}
```

常见错误场景：

| 场景 | message 示例 |
|---|---|
| 资产未加载 | `"Asset not loaded: /Game/Meshes/SM_Foo"` |
| 资产路径无效 | `"Asset not found: /Game/Meshes/SM_Missing"` |
| 属性路径不存在 | `"Property path not found: LightmapCoordinateIndexX"` |
| 属性类型不支持序列化 | `"Property type not serializable: TMap<FName, FDelegate>"` |

### 1.5 示例

**请求**：读取 StaticMesh 的 `LightmapCoordinateIndex`

```json
{
  "params": {
    "asset_path": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
    "property": "LightmapCoordinateIndex"
  }
}
```

**响应**：

```json
{
  "success": true,
  "data": {
    "asset_path": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
    "property": "LightmapCoordinateIndex",
    "value": 0
  }
}
```

**请求**：读取嵌套结构体路径

```json
{
  "params": {
    "asset_path": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
    "property": "LightmapSettings.ResolutionScale"
  }
}
```

**响应**：

```json
{
  "success": true,
  "data": {
    "asset_path": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
    "property": "LightmapSettings.ResolutionScale",
    "value": 1.0
  }
}
```

---

## 2. `scan_assets`

### 2.1 工具元信息

```json
{
  "name": "scan_assets",
  "category": "Asset",
  "description": "文件夹作用域资产扫描。返回 v1 linter 所需的元数据，包括命名、路径、父类、材质槽、LOD 和碰撞存在性。不依赖资产加载状态，直接查询 AssetRegistry。"
}
```

### 2.2 请求 Schema

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `folder_path` | string | 是 | — | UE 内容文件夹路径，格式 `/Game/...` |
| `recursive` | boolean | 否 | `false` | 是否递归扫描子文件夹 |
| `class_filter` | string[] | 否 | `[]`（不过滤） | 限定资产类型，如 `["StaticMesh", "Blueprint"]` |

参数内控约束：
- `folder_path` 必须以 `/Game/` （或其他有效 UE 挂载点）开头
- `class_filter` 中的字符串为 AssetRegistry 类名（大小写不敏感）

### 2.3 响应 Schema（成功）

```json
{
  "success": true,
  "data": {
    "folder_path": "<string>   原样返回请求中的 folder_path",
    "recursive":   "<boolean>  原样返回请求中的 recursive",
    "total":       "<integer>  本次扫描命中的资产总数",
    "assets":      "<Asset[]>  资产对象数组，见下方字段表"
  }
}
```

**Asset 对象字段**：

| 字段 | 类型 | 存在条件 | 对应 T1 规则 | 说明 |
|---|---|---|---|---|
| `name` | string | 常住 | 规则 1 | 资产名，如 `BP_SM_ComplianceProbe_Cube` |
| `path` | string | 常住 | 全局定位 | 完整对象路径，如 `/Game/SmithUETest/BP_SM_ComplianceProbe_Cube.BP_SM_ComplianceProbe_Cube` |
| `package_name` | string | 常住 | 规则 1 | 包名，如 `/Game/SmithUETest/BP_SM_ComplianceProbe_Cube` |
| `package_path` | string | 常住 | 规则 2 | 包所在目录，如 `/Game/SmithUETest` |
| `class` | string | 常住 | 资产类型区分 | AssetRegistry 类名，如 `Blueprint`、`StaticMesh` |
| `parent_class` | string \| null | 常住 | 规则 3 | BP 属资产返回父类路径，如 `/Script/Engine.Actor`；非 BP 资产返回 `null` |
| `material_slots` | integer | 常住 | 规则 7 | 网格材质槽总数；非网格资产为 `0` |
| `material_slot_paths` | string[] | 常住 | 规则 7 | 各槽已填路径数组，长度 = `material_slots`；未填充槽为空字符串 `""` |
| `lod_count` | integer | 常住 | 规则 8 | LOD 总数；非网格资产为 `0` |
| `min_lod` | integer | 常住 | 规则 8 | AssetRegistry tag `MinLOD`；非网格资产为 `0` |
| `has_collision` | boolean | 常住 | 规则 5 前置 | 是否有简单碰撞几何体；非网格资产为 `false` |

> **零值降级**：语义上不适用的字段（如 `material_slots` 对一个 class=Blueprint 的资产）将返回合理零值（`0`/`[]`/`false`/`null`），不在成功响应内缺字段。这确保消费方不需要空字段守卫。

### 2.4 响应 Schema（失败）

```json
{
  "success": false,
  "error": {
    "message": "<string>"
  }
}
```

常见错误场景：

| 场景 | message 示例 |
|---|---|
| 文件夹路径无效 | `"Folder not found: /Game/NoSuchDir"` |
| 文件夹路径格式错误 | `"Invalid folder_path: must start with /Game/ or a valid mount point"` |

### 2.5 示例

**请求**：扫描 `/Game/SmithUETest`，不递归

```json
{
  "params": {
    "folder_path": "/Game/SmithUETest",
    "recursive": false,
    "class_filter": ["StaticMesh", "Blueprint"]
  }
}
```

**响应**：

```json
{
  "success": true,
  "data": {
    "folder_path": "/Game/SmithUETest",
    "recursive": false,
    "total": 4,
    "assets": [
      {
        "name": "SM_ComplianceProbe_Cube",
        "path": "/Game/SmithUETest/SM_ComplianceProbe_Cube.SM_ComplianceProbe_Cube",
        "package_name": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
        "package_path": "/Game/SmithUETest",
        "class": "StaticMesh",
        "parent_class": null,
        "material_slots": 1,
        "material_slot_paths": ["/Engine/BasicShapes/BasicShapeMaterial"],
        "lod_count": 1,
        "min_lod": 0,
        "has_collision": true
      },
      {
        "name": "BP_SM_ComplianceProbe_Cube",
        "path": "/Game/SmithUETest/BP_SM_ComplianceProbe_Cube.BP_SM_ComplianceProbe_Cube",
        "package_name": "/Game/SmithUETest/BP_SM_ComplianceProbe_Cube",
        "package_path": "/Game/SmithUETest",
        "class": "Blueprint",
        "parent_class": "/Script/Engine.Actor",
        "material_slots": 0,
        "material_slot_paths": [],
        "lod_count": 0,
        "min_lod": 0,
        "has_collision": false
      }
    ]
  }
}
```

> 注：上例中 Blueprint 资产的 `material_slots` 和 `lod_count` 为 0，这是预期行为——材质槽和 LOD 应在组件层面由 `bp_describe_components` 读回。

---

## 3. `bp_describe_components`

### 3.1 工具元信息

```json
{
  "name": "bp_describe_components",
  "category": "Blueprint",
  "description": "文件夹作用域或单 BP 的组件树读回。返回每个组件的名称、类型、Mobility、碰撞配置、材质和网格路径，供 linter 对比规则 4–6。仅读取当前 BP 自身 SCS 组件；继承相关纳入 v1 盲区标注。"
}
```

### 3.2 请求 Schema

`folder_path` 与 `bp_path` 二选一：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `folder_path` | string | 二选一 | — | 文件夹作用域，扫描该目录下所有 Blueprint 资产 |
| `bp_path` | string | 二选一 | — | 单个 BP 资产路径，格式 `/Game/...` |
| `recursive` | boolean | 否 | `false` | 仅在 `folder_path` 模式下生效：是否递归子文件夹 |

参数互斥论：
- `folder_path` 和 `bp_path` 不得同时传入
- 两者都不传时返回参数错误

### 3.3 响应 Schema（成功）

```json
{
  "success": true,
  "data": {
    "total_blueprints": "<integer>  本次涉及的 BP 总数",
    "blueprints":       "<BPEntry[]> Blueprint 条目数组"
  }
}
```

**BPEntry 对象**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `bp_path` | string | BP 资产路径 |
| `bp_name` | string | BP 资产名 |
| `component_count` | integer | 自身 SCS 组件数量 |
| `components` | Component[] | 组件对象数组 |

**Component 对象字段**：

| 字段 | 类型 | 存在条件 | 对应 T1 规则 | 说明 |
|---|---|---|---|---|
| `name` | string | 常住 | 规则 4 | 组件变量名 |
| `class` | string | 常住 | 规则 4 | 组件类型，如 `StaticMeshComponent` |
| `source` | string | 常住 | 继承区分 | `own`（当前 BP 定义）或 `inherited`（从父 BP 继承） |
| `mobility` | string | 常住 | 规则 6 | `"Static"` / `"Movable"` / `"Stationary"` |
| `collision` | object | 常住 | 规则 5 | 见下方子字段 |
| `collision.profile` | string | 常住 | 规则 5 | 碰撞预设名，如 `"BlockAll"`、`"NoCollision"` |
| `collision.enabled` | string | 常住 | 规则 5 | 碰撞开关模式，如 `"QueryAndPhysics"`、`"NoCollision"` |
| `materials` | string[] | 常住 | 规则 7 | 已填材质路径数组；未填充项为空字符串 `""` |
| `mesh` | string \| null | 常住 | 规则 7 辅助 | StaticMeshComponent 的网格路径；非网格组件为 `null` |
| `inherited_unverifiable` | boolean | 常住 | 规则 9 盲区 | `true` 表示该组件继承自父 BP，当前读回能力无法确认其属性——v1 盲区显式标注 |

> **关于 `inherited_unverifiable`**：T1 已确认：对子 BP 调用 `bp_get_component_details(include_inherited=true)` 时组件数组为空。因此，所有通过继承得到组件的 BP，其组件记录都应将该字段置为 `true`。v1 linter 遇到 `inherited_unverifiable=true` 时应跳过对应组件的属性检查，不得静默判定通过或失败。

### 3.4 响应 Schema（失败）

```json
{
  "success": false,
  "error": {
    "message": "<string>"
  }
}
```

常见错误场景：

| 场景 | message 示例 |
|---|---|
| 两个路径参数同时传入 | `"Provide either folder_path or bp_path, not both"` |
| 两个都未传入 | `"Either folder_path or bp_path is required"` |
| BP 资产不存在 | `"Blueprint not found: /Game/NoSuch/BP_Missing"` |
| 目标不是 Blueprint | `"Asset is not a Blueprint: /Game/SmithUETest/SM_ComplianceProbe_Cube"` |

### 3.5 示例

**请求**：读回单个 BP 的组件树

```json
{
  "params": {
    "bp_path": "/Game/SmithUETest/BP_SM_ComplianceProbe_Cube"
  }
}
```

**响应**：

```json
{
  "success": true,
  "data": {
    "total_blueprints": 1,
    "blueprints": [
      {
        "bp_path": "/Game/SmithUETest/BP_SM_ComplianceProbe_Cube",
        "bp_name": "BP_SM_ComplianceProbe_Cube",
        "component_count": 1,
        "components": [
          {
            "name": "StaticMeshComponent",
            "class": "StaticMeshComponent",
            "source": "own",
            "mobility": "Static",
            "collision": {
              "profile": "BlockAll",
              "enabled": "QueryAndPhysics"
            },
            "materials": ["/Engine/BasicShapes/BasicShapeMaterial"],
            "mesh": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
            "inherited_unverifiable": false
          }
        ]
      }
    ]
  }
}
```

**请求**：文件夹批量读回，包含子 BP（继承盲区展示）

```json
{
  "params": {
    "folder_path": "/Game/SmithUETest",
    "recursive": false
  }
}
```

**响应**（简化，仅展示继承盲区行为）：

```json
{
  "success": true,
  "data": {
    "total_blueprints": 3,
    "blueprints": [
      {
        "bp_path": "/Game/SmithUETest/BP_SM_ComplianceProbe_Cube",
        "bp_name": "BP_SM_ComplianceProbe_Cube",
        "component_count": 1,
        "components": [
          {
            "name": "StaticMeshComponent",
            "class": "StaticMeshComponent",
            "source": "own",
            "mobility": "Static",
            "collision": { "profile": "BlockAll", "enabled": "QueryAndPhysics" },
            "materials": ["/Engine/BasicShapes/BasicShapeMaterial"],
            "mesh": "/Game/SmithUETest/SM_ComplianceProbe_Cube",
            "inherited_unverifiable": false
          }
        ]
      },
      {
        "bp_path": "/Game/SmithUETest/BP_ReadbackProbe_Child",
        "bp_name": "BP_ReadbackProbe_Child",
        "component_count": 0,
        "components": []
      },
      {
        "bp_path": "/Game/SmithUETest/BP_ReadbackProbe_Parent",
        "bp_name": "BP_ReadbackProbe_Parent",
        "component_count": 1,
        "components": [
          {
            "name": "InheritedStaticMeshComponent",
            "class": "StaticMeshComponent",
            "source": "own",
            "mobility": "Static",
            "collision": { "profile": "BlockAll", "enabled": "QueryAndPhysics" },
            "materials": [],
            "mesh": null,
            "inherited_unverifiable": false
          }
        ]
      }
    ]
  }
}
```

> 注：`BP_ReadbackProbe_Child` 的 `components` 为空数组——这就是 T1 确认的继承盲区：子 BP 的 SCS 组件读回不到父 BP 定义的组件。v1 linter 对此类 BP 遇到组件数量为 0 时不得推断规则 4/5/6 的结果。

---

## 4. 实现指导（不属于契约）

此节为插件 C++ 实现提供非强制层的参考。

### `get_asset_property` 实现提示

- 执行前检查 `UPackage* pkg = FindPackage(nullptr, *AssetPath)`，资产未加载时直接返回错误，不尝试自动加载
- 点号路径解析：分隔符 `.`，数组索引表达式 `[n]` 应在单独一次递归解析层处理
- 使用 `FProperty::ExportText_Direct` 序列化到字符串，再尝试 `FJsonObjectConverter`

### `scan_assets` 实现提示

- 直接查询 AssetRegistry，无需加载资产
- `parent_class`：读 AssetRegistry tag `ParentClass`，若无此 tag（非 BP）返回 `null`
- `material_slots`：读 tag `Materials`（字符串转整数）；所有路径展开需加载资产
- `material_slot_paths`：若资产未加载则返回大小为 `material_slots` 的全空字符串数组
- `lod_count`：读 tag `LODs`；`min_lod`：读 tag `MinLOD`，若缺失则为 0
- `has_collision`：若资产已加载则检查 `UStaticMesh::BodySetup`；未加载时读自定义 tag（AssetRegistry 中暂无标准 tag，可自定义或仅对已加载资产判断）

### `bp_describe_components` 实现提示

- 需要加载资产（或至少加载资产头部）才能访问 SCS
- 组件遍历：`UBlueprint::SimpleConstructionScript->GetAllNodes()`
- `mobility`：通过反射读取 `Mobility` 属性
- `collision.profile / enabled`：通过 `UPrimitiveComponent::BodyInstance` 读取
- `inherited_unverifiable`：目前将所有 `source=inherited` 的组件置为 `true`，待后续新原语支持继承链合并

---

## 5. 版本历史

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-06-22 | 初始版，定义三个审计原语 schema |

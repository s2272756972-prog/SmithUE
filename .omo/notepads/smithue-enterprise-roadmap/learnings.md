# Learnings — smithue-enterprise-roadmap

## [2026-06-22] Task: T1 读回保真 spike
- BP 命名模式：可观测（via get_asset_info.name/package_name 与 bp_get_summary.bp_path）。
- 输出文件夹：可观测（via list_assets.assets[].package_path 与 get_asset_info.package_path）。
- 父/基类：可观测（via bp_get_summary.parent_class；AssetRegistry tags 也有 ParentClass/NativeParentClass）。
- 必需组件名：可观测（via bp_get_component_details.components[].name/class）。
- 碰撞 preset：可观测（via bp_get_component_details.components[].collision.profile，fixture 读回 BlockAll）。
- Mobility：可观测（via bp_get_component_details.components[].mobility，fixture 读回 Static）。
- 材质槽填充：可观测但有限（via component materials[] + StaticMesh tags.Materials；无槽语义元数据）。
- LOD0：可观测（via StaticMesh get_asset_info.tags.LODs/MinLOD）。
- 继承组件：盲区确认；父 BP 组件在父资产上可读，但对子 BP 调 bp_get_component_details(include_inherited=true) 返回 components: []。
## [2026-06-22] Task: T3 spec 格式 v1
- spec.schema.json: draft-07, schemaVersion const "1.0.0"
- 8 条规则覆盖：naming/outputFolder/parentClass/components[]/lod
- valid fixtures: prop.valid.json, character.valid.json
- invalid fixtures: missing-schema-version, bad-name-pattern(integer type)
## [2026-06-22] Task: T4 config schema
- specsDir 必须字段，v1 集中单目录
- devContentRoot 默认 /Game/SmithUETest
- ownership.include/exclude: 保守默认（未在 include = 非拥有，exclude 硬排除）
- 发现机制: 向上查找 smithue.config.json (类 eslint 向上查找)

## [2026-06-22] Task: T2 契约 schema
- get_asset_property 请求：asset_path + property（点号路径）
- scan_assets 响应关键字段：parent_class、material_slots、lod_count、has_collision
- bp_describe_components 组件字段：mobility/collision.profile/materials/inherited_unverifiable

## [2026-06-22] Task: T8 spec loader
- src/spec/loader.ts: Ajv compile + SpecValidationError(fields, message)
- loadSpec: readFile → JSON.parse → validate → cast to SpecModel
- ESM: createRequire 加载 JSON schema / 或 import assert json
- vitest: 全绿，包括 invalid fixture SpecValidationError 含 fields

## [2026-06-22] Task: T6 bp_describe_components
- 自身 SCS: SimpleConstructionScript->GetAllNodes() 可读
- ICH override: Blueprint->GetInheritableComponentHandler(false) → GetAllTemplates()
- 父 BP SCS 继承（非 ICH）: 标 inherited_unverifiable=true
- 三态：source=own / inherited_override / inherited（unverifiable）

## [2026-06-22] Task: T9 config resolver + ownership
- findConfigFile: 向上遍历目录直到根，找 smithue.config.json
- isOwned: include为空→false（保守），exclude优先（硬排除），glob用/**前缀匹配
- UltraDynamicSky排除验证通过

## [2026-06-22] Task: T7 register + rebuild
- 工具数: 217（214+3）
- get_asset_property: BP_SM_ComplianceProbe_Cube BlueprintType = BPTYPE_Normal（SM_ComplianceProbe_Cube StaticMesh 未存在于项目中，改用 BP 验证）
- scan_assets: /Game/SmithUETest 返回 3 个资产（BP_SM_ComplianceProbe_Cube、BP_ReadbackProbe_Parent、BP_ReadbackProbe_Child），含 material_slots/lod_count/has_collision 字段
- bp_describe_components: BP_SM_ComplianceProbe_Cube 组件树读回成功（1 个 StaticMeshComponent，source=own，mobility=Static）
- BP_ReadbackProbe_Child 继承组件: inherited_unverifiable=true（source=inherited，mobility=Movable）
- SmithUEBpAtomicAPI.cpp 修复: USceneComponent::GetMobility() → ::Mobility（UE5.2 直接属性访问，GetMobility() 方法不存在）

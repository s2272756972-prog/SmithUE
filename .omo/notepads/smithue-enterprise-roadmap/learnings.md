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

# SmithUE 战略定位文档

> 本文记录 SmithUE 的长期技术走向。所有“未来”章节均为**策略规划**，不在当前版本（v1）执行。

## 一、UE5.7 多版本策略

### 核心原则

- 每个 UE 主版本维护一条长期分支（UE5.2 / UE5.7 / UE6）
- HTTP `/api/v1/tools` 契约是唯一兼容边界——客户端只需适配契约，不感知引擎版本
- 共享业务逻辑下沉到 version-agnostic 层（CLI / 宿主工程 spec），C++ 层仅承担引擎相关 API 差异

### UE5.2 → UE5.7 迁移注意

- API 变化评估：蓝图 SCS/ICH 接口、AssetRegistry FARFilter、FProperty 遍历在 5.7 有调整
- 迁移节奏：功能先在 UE5.2 分支验证稳定后，再 cherry-pick 到 UE5.7 分支
- 当前状态：UE5.7 分支为**策略规划**，不在本计划执行

## 二、UE6 / Verse 定位

### 战略判断：护城河不受影响

- UE6 抛弃的是蓝图的**逻辑层**（事件图、函数节点）
- SmithUE 主战场是**数据层**（组件装配、Mesh/材质绑定、碰撞、CDO）
- 数据层在 UE6 完整存活：哪怕逻辑层全换成 Verse，“这个道具蓝图要挂 StaticMeshComponent + 设碰撞 + 配材质槽”这件事永远存在

### Verse 时代的具体应对

- 当前工具保持不变——`bp_describe_components`、`scan_assets` 等读写数据层，与逻辑层语言解耦
- 未来可加**只读 Verse introspection**（读挂在资产上的 Verse 脚本），核心装配工具一行不改
- 增量机会：**蓝图→Verse 迁移分析器**——企业有海量旧 BP，迁移时需要自动盘点评估，SmithUE 的二进制访问能力天然适合

## 三、Phase 2 演进路线

> 以下为**后续规划**，均不在当前 v1 执行。

### spec 级联模型（.editorconfig 式就近优先）

- v1：集中 specsDir（宿主工程单一目录）
- Phase 2：文件夹级局部覆盖——全局默认 spec + 子文件夹可存 local spec，就近优先、向上级联到根
- 意义：大型项目不同模块有不同规范，无需维护单一巨型 spec

### Linter auto-fix（带 dry-run + 回滚）

- v1：report-only（只读，零风险）
- Phase 2：对可安全自动修复的规则（如 Mobility、CollisionProfile）加 `--fix` 模式
- 安全约束：强制 dry-run 预览 + git-level 回滚备份，企业生产管线才敢用

### Skeletal Mesh / Material / Niagara 工厂

- v1：仅 StaticMesh 输入
- Phase 2：扩展支持 SkeletalMesh、Material 实例标准化、Niagara 系统装配

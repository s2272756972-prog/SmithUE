# SmithUE — UE 5.2 → UE 5.8 升级测试报告

> 生成环境：宿主工程 `MassDemo`（UE 5.8），插件分支 `UE5.8`，插件版本 `1.16.0`。
> 引擎：`F:\Program Files\Epic Games\UE_5.8`。CLI：`smithue-cli` v0.13.4。
> 本文件供人工复测用：**「需人工复测」一节是重点**。

---

## 1. 升级结论

- 插件在 UE 5.8 下**编译通过（0 error / 0 warning）**，编辑器加载 DLL 正常，HTTP/端口发现/工具注册全链路通。
- 工具总数 **232**（原 229 + 新增 Dialog 域 3 个），跨 **25** 域。`ue_version` 已上报 `5.8`。
- 代表性回归测试：**43/43 + 10/10 通过**（详见第 3 节）。
- 已知需人工复测项见第 4 节。

## 2. 为 UE 5.8 所做的源码改动（API 兼容）

| 位置 | 5.2 旧写法 | 5.8 新写法 | 说明 |
|---|---|---|---|
| `SmithUEEditorCommands.cpp` / `SmithUEGraphCommands.cpp` | `UMaterialExpression::GetInputs()` | `CountInputs()` + `GetInput(i)` | `GetInputs()`/`GetInputsView()` 已弃用（5.5） |
| `SmithUEMaterialCommands.cpp` | `GetMaterialResource(ERHIFeatureLevel)` | `GetMaterialResource(EShaderPlatform)`（SP_PCD3D_SM5/SM6） | 5.7 弃用 FeatureLevel 重载 |
| `SmithUEBpAtomicAPIHelpers.cpp` / `SmithUEDataCommands.cpp` | `#include "Engine/UserDefinedStruct.h"` | `#include "StructUtils/UserDefinedStruct.h"` | 头文件迁至 CoreUObject/StructUtils |
| `SmithUEBlueprintCommands.cpp` / `SmithUEDataCommands.cpp` / `SmithUEHttpServerRunnable.cpp` / `SmithUEBpAtomicAPI.cpp` | `FJsonObject::Values` 的 key 当作 `FString` | key 现为 `UE::TSharedString<TCHAR>`：`.ToView()` / `FString(*Key)` / `Key.ToView()` | JSON 对象 key 类型变更 |
| `SmithUEBpAtomicAPI.cpp` | `UAnimGraphNode_Base::PropertyBindings`（直接成员） | 经反射从 `Binding`(UPROPERTY) → `PropertyBindings`(UPROPERTY) 取 TMap | 5.8 迁到独立 `UAnimGraphNodeBinding` 子对象，具体类在**引擎私有头**，插件不可 include，故用纯反射 |
| `SmithUECliCheckerTest.cpp` | `EAutomationTestFlags::ApplicationContextMask` | `EAutomationTestFlags_ApplicationContextMask` | enum 变 scoped，掩码移出为独立常量 |
| `SmithUEToolRegistry.cpp` | 缺 include | 补 `#include "Utils/SmithUECommonUtils.h"` | 5.8 严格 include 后透传失效 |
| `.uplugin` | `WhitelistPlatforms` | `PlatformAllowList` | 字段更名 |
| 宿主 `MassDemoEditor.Target.cs` / `MassDemo.Target.cs` | `BuildSettingsVersion.V2` | `BuildSettingsVersion.Latest` + `IncludeOrderVersion.Latest` | 手动升级遗留，与 5.8 引擎共享构建环境冲突 |

### 弃用告警清理（面向 UE 5.9 前瞻，全部已验证）

- `SetMaterialUsage(bNeedsRecompile, MATUSAGE_X)` → `SetMaterialUsage(MATUSAGE_X)`（新虚函数）
- `BL_BeforeTonemapping/AfterTonemapping/BeforeTranslucency` → `BL_SceneColorAfterDOF/AfterTonemapping/BeforeDOF`
- `ForEachObjectWithOuter(..., true)` → `..., EGetObjectsFlags::IncludeNestedObjects`
- `GIsSavingPackage` → `UE::IsSavingPackage()`
- `Rename(..., REN_ForceNoResetLoaders)` → 移除该标志（Rename 不再调 ResetLoaders）
- `FMovieSceneBinding::GetName()`（5.8 返回**空串**，实为功能回归） → 从 `FMovieScenePossessable/Spawnable` 取名（`GetBindingDisplayName` 助手）；`GetBindings()` 改走 const 重载

## 3. 新增能力：Editor 弹窗识别与响应（Dialog 域，worker-safe）

根因：`level_save` 等对未保存关卡会弹**阻塞式模态框**，卡死 game thread，使所有走 game thread 的工具挂起。SmithUE 的 HTTP server 在 worker 线程，据 `FSlateApplication::OnModalLoopTickEvent`（模态循环内唯一广播的钩子）实现检测与响应。

| 工具 | 作用 |
|---|---|
| `get_active_dialog` | 报告当前是否有模态框（标题/类型）、auto 模式、已关闭计数。**模态卡死期间仍能响应** |
| `dismiss_active_dialog` | 关闭当前模态框（`cancel` 可靠关闭 / `accept` 尽力默认动作）。success=已排队，轮询确认 |
| `set_dialog_auto_response` | 预武装：任何模态框一出现即自动响应（`off`/`cancel`/`accept`） |

`/ready` 与 `status` 新增 `modal_active` 字段。已实测：模态卡死期间 `get_active_dialog` 正确抓到"将关卡另存为"标题；`dismiss` 关闭后 game thread 释放；预武装 `cancel` 后 `level_save` 不再卡死（~340ms 返回）。

## 4. 需人工复测（重点）

### 4.1 AnimGraph 绑定类工具 — 已用真实骨骼验证通过 ✅
本次对 `UAnimGraphNode_Base::PropertyBindings` 的访问做了**反射重写**（5.8 该成员迁入引擎私有类）。已用真实骨骼 `/Game/Characters/Mannequins/Meshes/SK_Mannequin` 做端到端往返验证：
- `anim_create_blueprint` → 建 AnimBP ✅
- `bp_create_node`（AnimGraphNode_SequencePlayer）→ 加可绑定 anim 节点 ✅
- `bp_expose_anim_pin`（PlayRate）→ 暴露可绑定引脚 ✅
- **`bp_bind_anim_property`（PlayRate ← Speed 变量）→ `bound:true`（反射写路径）✅**
- `bp_read_anim_node` → 读回 `bindings:[{property:"PlayRate", property_path:["Speed"], is_bound:true, type:"Property"}]`（反射读路径，完整往返）✅
- `bp_compile` 无错、`save_asset` 成功 ✅

> 注意点：若某 anim 节点 `GetMutableBinding()` 为空，`bp_bind_anim_property` 会返回可操作错误（提示重开/重建节点）——这是 5.8 下的新边界（插件无法从外部创建 binding 子对象）。

### 4.2 `level_save`（模态框，已缓解未根除）
未命名关卡 `/Temp/Untitled_X` 上调用 `level_save` 会弹"将关卡另存为/命名"模态框并阻塞。**缓解**：先 `set_dialog_auto_response {mode:cancel}` 或事后 `dismiss_active_dialog`。**建议人工复测**：对已保存的正常关卡 `level_save` 应正常无弹窗直接落盘。

### 4.3 未直接往返、建议抽测的域/工具
下列在本轮自动化中**未做 create+read 往返**（多因需特定上下文），建议人工抽测：
- **Niagara**（16 个，除 `niagara_search_assets`）：需 Niagara System 上下文。
- **UMG**（4 个）、**Environment**（11 个）、**Debug**（断点类 3 个）。
- **Level 写操作**：`level_new` / `level_open`（延迟到下一帧）/ `level_create_landscape` / `level_set_landscape_material` / 前景植被 `level_add_foliage_type`/`paint`/`erase` / `level_add_basic_env`。
- **PIE 生命周期**：`pie_start` / `pie_stop` / `pie_list_actors`（需进 PIE）。
- **Asset 写操作**：`rename_asset` / `duplicate_asset` / `move_asset` / `delete_asset` / `set_asset_property` / `generate_texture` / `generate_audio`（联网）。
- **Blueprint 进阶**：`bp_reparent` / `bp_copy_graph` / `bp_trace_value` / `bp_diff` / `bp_compile_code` 等。
- **Sequencer**：`seq_add_track` / `seq_add_keyframe` / `seq_set_range`。
- **Material 进阶**：`create_material_instance` / `set_mi_*` / `add_mpc_vector` / `set_mpc_value` / MF 连接与属性。

### 4.4 参数名易错（非故障，记录备查）
- `get_actor_property` / `set_actor_property` 用 `property_name`（不是 `property`）。
- `bp_get_compile_errors` 用 `blueprint_path`；`find_asset` 用 `name_pattern`；`get_actor_property` 用 `actor_label`。

## 5. 新增功能与增强（UE5.8 分支，均已实测）

### 5.1 PCG 域（新增 5 工具 → 26 域 / 237 工具）
移植自 UE5.7 分支并适配 5.8 + 按 TOOL_SPEC 增强：`create_pcg_graph` / `read_pcg_graph`（新增，create↔read 对称）/ `find_pcg_graphs` / `spawn_pcg_volume` / `pcg_generate`。实测：建图→读回(node_count/has_input/output)→查找→在关卡 spawn PCG Volume 并 `Generate()`→按 label 重生成，全通过。`SmithUE.Build.cs` 加 `PCG` 模块、`.uplugin` 加 PCG 插件依赖。

### 5.2 Graph 节点自动错开（修复"叠在一块"）
`bp_create_node` 不传 `position` 时曾一律落 (0,0)，多节点全堆原点。现于 `CreateNode` 增 `ComputeCascadeNodePosition`：无显式位置时按现有节点包围盒向右级联（实测 5 个无位置节点落在 x=360/720/1080/1440，0 重叠）。`bp_describe_graph` 输出新增 `pos_x`/`pos_y` 便于检阅；整图整理仍可用 `auto_layout_graph`（连线感知）。

### 5.3 level_save 无弹框另存
`level_save` 加可选 `level_path`（`UEditorLoadingAndSavingUtils::SaveMap`，无模态框）。实测把未命名关卡与 basic-env 关卡另存到 `/Game/SmithUE58Test`（.umap 落盘、`modal_active:false`）。

## 6. 已通过的关键往返（摘要）

Curve/CurveAtlas/RenderTarget/PhysicalMaterial 建读；Data 结构体+枚举+表+加行+读表（UserDefinedStruct 头迁移路径）；Material 建/加表达式/设属性/连线/**编译**（GetMaterialResource 路径）/图布局（CountInputs 路径）/MPC/材质函数；Blueprint 建/变量/组件/override/建节点/**batch_op**（TSharedString key 路径）/编译/health_check/**set_component_collision responses**（TSharedString 路径）；Sequencer 建/加绑定/**读绑定名**（GetBindingDisplayName 修复）；set_material_property 的 usage（SetMaterialUsage）与 blendable（BL_ 重命名）；Dialog 三件套 + 真实模态框检测/关闭。

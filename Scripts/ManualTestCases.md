# UEAgent 编辑器交互命令 — 手动测试用例

> **前提条件**：UE 5.2 Editor 已启动、UEAgent 插件已加载（TCP 13720 / HTTP 13721）
>
> **发送命令脚本**：`.\Scripts\Send-UEAgent.ps1 -Command "<命令名>" -Params '<JSON参数>'`
>
> **通用判定**：所有命令返回 JSON `{"status":"success","data":{...}}` 或 `{"status":"error","error":"..."}`

---

## 目录

| # | 命令 | 分类 |
|---|------|------|
| 1 | execute_editor_command | 交互 |
| 2 | execute_console_command | 交互 |
| 3 | list_editor_commands | 交互 |
| 4 | undo | 交互 |
| 5 | redo | 交互 |
| 6 | simulate_key | 交互 |
| 7 | list_key_bindings | 交互 |
| 8 | set_viewport_camera | 视口 |
| 9 | focus_on_actor | 视口 |
| 10 | set_viewport_mode | 视口 |
| 11 | get_viewport_info_detailed | 视口 |
| 12 | select_actors | 视口 |
| 13 | list_panels | 观察 |
| 14 | open_panel | 观察 |
| 15 | close_panel | 观察 |
| 16 | get_editor_state | 观察 |
| 17 | get_level_info | 观察 |
| 18 | get_actor_property | 观察 |
| 19 | get_selected_actors | 观察 |
| 20 | get_world_outline | 观察 |
| 21 | take_viewport_screenshot | 截图 |
| 22 | set_actor_property（增强版） | 编辑 |
| E1–E4 | 错误路径 / 边界测试 | 综合 |

---

## 测试准备

```powershell
cd F:\DXProject\AIScript\Plugins\UEAgent

# 确认插件已连通
.\Scripts\Send-UEAgent.ps1 -Command "get_protocol_info"
# ✅ 返回 status=success，tools 数组包含 40+ 条目

# 建立测试 Actor（后续多个用例依赖）
.\Scripts\Send-UEAgent.ps1 -Command "spawn_actor" -Params '{"class":"StaticMeshActor","label":"TestCube","location":{"x":100,"y":200,"z":50}}'
# ✅ Editor 视口中出现一个新的 StaticMeshActor，World Outliner 中显示 "TestCube"
```

---

## 一、交互命令 (Interaction)

### 1. execute_editor_command

执行 GEditor->Exec 编辑器命令。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 1a. 全选 Actor | `-Command "execute_editor_command" -Params '{"command_name":"ACTOR SELECT ALL"}'` | `{"executed":true,"command_name":"ACTOR SELECT ALL"}` | **World Outliner 中所有 Actor 被选中**（蓝色高亮），视口中选中的 Actor 显示橙色边框 |
| 1b. 无效命令 | `-Command "execute_editor_command" -Params '{"command_name":"FAKE_COMMAND_12345"}'` | `status: "error"`，包含 "did not handle" | **Editor 无变化** |

```powershell
# 1a
.\Scripts\Send-UEAgent.ps1 -Command "execute_editor_command" -Params '{"command_name":"ACTOR SELECT ALL"}'
# 👀 看 Editor：所有 Actor 应该被选中

# 1b
.\Scripts\Send-UEAgent.ps1 -Command "execute_editor_command" -Params '{"command_name":"FAKE_COMMAND_12345"}'
# 👀 看返回：应该是 error
```

---

### 2. execute_console_command

执行 UE 控制台命令。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 2a. 关闭统计显示 | `-Command "execute_console_command" -Params '{"command":"stat none"}'` | `{"executed":true,"command":"stat none"}` | **视口左上角的 stat 统计信息消失**（如果之前有开启） |
| 2b. 打开 FPS 统计 | `-Command "execute_console_command" -Params '{"command":"stat fps"}'` | `{"executed":true,"command":"stat fps"}` | **视口左上角出现绿色 FPS 数字**（如 60.00 fps） |
| 2c. 修改画质参数 | `-Command "execute_console_command" -Params '{"command":"r.ScreenPercentage 50"}'` | `{"executed":true}` | **视口画面变得模糊**（分辨率降低到 50%），可以再发 `r.ScreenPercentage 100` 恢复 |

```powershell
# 2b — 开启 FPS
.\Scripts\Send-UEAgent.ps1 -Command "execute_console_command" -Params '{"command":"stat fps"}'
# 👀 看视口左上角：应该出现 FPS 数字

# 恢复
.\Scripts\Send-UEAgent.ps1 -Command "execute_console_command" -Params '{"command":"stat fps"}'
```

---

### 3. list_editor_commands

列出所有已注册的 FUICommandInfo 条目。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 3a. 不带过滤 | `-Command "list_editor_commands"` | `{"commands":[...]}` 数组长度 > 100 | **无变化**（只读查询） |
| 3b. 过滤 "Delete" | `-Command "list_editor_commands" -Params '{"filter":"Delete"}'` | 数组只包含名称/标签含 "Delete" 的条目 | **无变化** |

```powershell
# 3a — 查看总数
.\Scripts\Send-UEAgent.ps1 -Command "list_editor_commands"
# 👀 看返回 JSON：commands 数组应该非空，每个条目包含 name、label 字段

# 3b — 过滤
.\Scripts\Send-UEAgent.ps1 -Command "list_editor_commands" -Params '{"filter":"Delete"}'
# 👀 检查：返回条目都与 Delete 相关
```

---

### 4. undo

撤销上一个编辑器事务。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 4a. 撤销 spawn | 先 spawn_actor 再 undo | `{"undone":true}` | **刚刚 spawn 的 Actor 从视口和 Outliner 中消失** |

```powershell
# 前置：创建一个 Actor
.\Scripts\Send-UEAgent.ps1 -Command "spawn_actor" -Params '{"class":"PointLight","label":"UndoTestLight","location":{"x":0,"y":0,"z":300}}'
# 👀 看 Editor：出现一个 PointLight（Outliner 中显示 UndoTestLight）

# 撤销
.\Scripts\Send-UEAgent.ps1 -Command "undo"
# 👀 看 Editor：UndoTestLight 消失了
```

---

### 5. redo

重做上一个被撤销的事务。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 5a. 重做 spawn | 接 4a 之后 | `{"redone":true}` | **UndoTestLight 重新出现在视口和 Outliner 中** |

```powershell
# 紧接用例 4a 之后
.\Scripts\Send-UEAgent.ps1 -Command "redo"
# 👀 看 Editor：UndoTestLight 又回来了
```

---

### 6. simulate_key

模拟键盘按键（优先查找命令绑定，找不到时走 Slate 输入）。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 6a. 按 F 键（聚焦） | `-Command "simulate_key" -Params '{"key":"F"}'` | `{"simulated":true,"key":"F"}` | **视口摄像机飞到选中 Actor 的位置**（前提：先 select_actors 选中一个 Actor），如果没选中则摄像机聚焦到场景原点 |
| 6b. Ctrl+Z（撤销） | `-Command "simulate_key" -Params '{"key":"Z","modifiers":{"ctrl":true}}'` | `{"simulated":true,"key":"Z","method":"command_execution"}` 或类似 | **等同于按 Ctrl+Z**：最后一个操作被撤销 |
| 6c. Delete 键 | 先 select_actors 选中 TestCube，再发 | `{"simulated":true,"key":"Delete"}` | **选中的 Actor 被删除**，从 Outliner 和视口中消失 |
| 6d. 无效键名 | `-Command "simulate_key" -Params '{"key":"FAKEKEY"}'` | `status: "error"` | **无变化** |

```powershell
# 6a — 先选中 TestCube 再按 F 聚焦
.\Scripts\Send-UEAgent.ps1 -Command "select_actors" -Params '{"actor_labels":["TestCube"]}'
.\Scripts\Send-UEAgent.ps1 -Command "simulate_key" -Params '{"key":"F"}'
# 👀 看视口：摄像机应该飞到 TestCube 附近

# 6b — Ctrl+Z 撤销（建议先做一个操作再撤销）
.\Scripts\Send-UEAgent.ps1 -Command "simulate_key" -Params '{"key":"Z","modifiers":{"ctrl":true}}'
# 👀 看 Editor：最近的操作被撤销

# 6d — 无效键名
.\Scripts\Send-UEAgent.ps1 -Command "simulate_key" -Params '{"key":"FAKEKEY"}'
# 👀 看返回：应该是 error
```

---

### 7. list_key_bindings

列出所有已绑定快捷键的命令。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 7a. 不带过滤 | `-Command "list_key_bindings"` | `{"bindings":[...]}` 数组非空，每项有 command_name、key 字段 | **无变化**（只读） |
| 7b. 过滤 "Delete" | `-Command "list_key_bindings" -Params '{"filter":"Delete"}'` | 只返回快捷键或命令名含 Delete 的条目 | **无变化** |
| 7c. 按 context | `-Command "list_key_bindings" -Params '{"context":"LevelEditor"}'` | 只返回 LevelEditor 上下文的绑定 | **无变化** |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "list_key_bindings"
# 👀 检查返回：bindings 数组非空
```

---

## 二、视口命令 (Viewport)

### 8. set_viewport_camera

设置活动视口摄像机的位置/旋转/FOV。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 8a. 设置位置 | `-Command "set_viewport_camera" -Params '{"location":{"x":500,"y":500,"z":300}}'` | `{"before":{...},"after":{"location":{"x":500,"y":500,"z":300},...}}` | **视口画面跳到坐标 (500,500,300)**，视口左上角坐标信息更新 |
| 8b. 设置旋转 | `-Command "set_viewport_camera" -Params '{"rotation":{"pitch":-30,"yaw":45,"roll":0}}'` | before/after 含 rotation 变化 | **视口摄像机旋转到 Pitch=-30, Yaw=45**，画面呈俯视 45 度角 |
| 8c. 设置 FOV | `-Command "set_viewport_camera" -Params '{"fov":120}'` | after.fov = 120 | **视口画面变成广角/鱼眼效果**（FOV 从默认 ~90 变为 120） |
| 8d. 同时设置 | `-Command "set_viewport_camera" -Params '{"location":{"x":0,"y":0,"z":1000},"rotation":{"pitch":-90,"yaw":0,"roll":0},"fov":90}'` | 三个字段均更新 | **完美俯视图**：从 (0,0,1000) 高空垂直向下看 |

```powershell
# 8a
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_camera" -Params '{"location":{"x":500,"y":500,"z":300}}'
# 👀 看视口：画面应该跳到新位置

# 8d — 高空俯视
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_camera" -Params '{"location":{"x":0,"y":0,"z":1000},"rotation":{"pitch":-90,"yaw":0,"roll":0},"fov":90}'
# 👀 看视口：应该从 1000 单位高空垂直往下看
```

---

### 9. focus_on_actor

让视口摄像机聚焦到指定 Actor。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 9a. 聚焦到 TestCube | `-Command "focus_on_actor" -Params '{"actor_label":"TestCube"}'` | `{"focused":true,"actor_label":"TestCube","camera_location":{...}}` | **视口摄像机飞到 TestCube 附近**，TestCube 居于画面中央 |
| 9b. 不存在的 Actor | `-Command "focus_on_actor" -Params '{"actor_label":"NoSuchActor"}'` | `status: "error"` | **无变化** |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "focus_on_actor" -Params '{"actor_label":"TestCube"}'
# 👀 看视口：摄像机应该飞到 TestCube 附近，TestCube 在画面中间
```

---

### 10. set_viewport_mode

设置视口投影模式（透视/正交六向）。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 10a. 切到顶视图 | `-Command "set_viewport_mode" -Params '{"mode":"top"}'` | `{"before_mode":"...","after_mode":"Top"}` | **视口切换到正交顶视图**，视口左上角显示 "Top" |
| 10b. 切到前视图 | `-Command "set_viewport_mode" -Params '{"mode":"front"}'` | after_mode = "Front" | **视口切换到正交前视图** |
| 10c. 切回透视 | `-Command "set_viewport_mode" -Params '{"mode":"perspective"}'` | after_mode = "Perspective" | **视口恢复透视模式**，左上角显示 "Perspective" |

```powershell
# 10a
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_mode" -Params '{"mode":"top"}'
# 👀 看视口左上角：应该显示 "Top"，视角变成从上往下的正交视图

# 10c — 切回透视
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_mode" -Params '{"mode":"perspective"}'
# 👀 看视口：恢复正常透视模式
```

---

### 11. get_viewport_info_detailed

获取当前视口的详细信息。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 11a. 查询 | `-Command "get_viewport_info_detailed"` | 包含 location、rotation、fov、viewport_size、is_realtime、view_mode 等字段 | **无变化**（只读查询） |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "get_viewport_info_detailed"
# 👀 检查返回字段：
#   location: {x, y, z}  — 应该与视口实际位置一致
#   rotation: {pitch, yaw, roll}
#   fov: 数字（通常 ~90）
#   viewport_size: {width, height} — 与视口窗口像素尺寸一致
#   is_realtime: true/false
```

**验证方法**：在 Editor 中按 G 键查看 FPS 统计位置的坐标值，与返回值对比。

---

### 12. select_actors

选中指定 Actor。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 12a. 选中一个 | `-Command "select_actors" -Params '{"actor_labels":["TestCube"]}'` | `{"selected":["TestCube"],"count":1}` | **TestCube 在 Outliner 中高亮（蓝色）**，视口中 TestCube 显示橙色选中边框 |
| 12b. 选中多个 | 先 spawn 第二个 Actor，再 `-Params '{"actor_labels":["TestCube","TestCube2"]}'` | count=2 | **两个 Actor 同时被选中**，Outliner 中两行蓝色高亮 |
| 12c. 追加选择 | `-Params '{"actor_labels":["TestCube2"],"add_to_selection":true}'` | 之前的选择保留，TestCube2 被加入 | **不清除已有选择**，TestCube2 追加到选中列表 |
| 12d. 不存在的 | `-Params '{"actor_labels":["NoActor"]}'` | 返回 not_found 列表 | **之前的选择被清除**（替换模式），没有 Actor 被选中 |

```powershell
# 12a
.\Scripts\Send-UEAgent.ps1 -Command "select_actors" -Params '{"actor_labels":["TestCube"]}'
# 👀 看 Outliner：TestCube 应该高亮
# 👀 看视口：TestCube 上出现橙色选中线框
```

---

## 三、观察命令 (Observation)

### 13. list_panels

列出已知编辑器面板及其打开状态。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 13a. 查询 | `-Command "list_panels"` | `{"panels":[{"name":"ContentBrowserTab1","is_open":true},{"name":"OutputLog","is_open":true},...]}` | **无变化**（只读） |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "list_panels"
# 👀 检查返回：
#   已知面板：ContentBrowserTab1, OutputLog, LevelEditorSelectionDetails, WorldOutliner, LevelEditorToolBox, PlacementBrowser
#   is_open 应该与 Editor 中你实际能看到的面板一致
#   例如：如果你能看到 Output Log 面板，那 OutputLog 的 is_open 应该是 true
```

---

### 14. open_panel

打开（或聚焦）一个编辑器面板。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 14a. 打开 Output Log | `-Command "open_panel" -Params '{"panel_name":"OutputLog"}'` | `{"opened":true}` 或 `{"opened":true,"already_open":true}` | **Output Log 面板出现或被聚焦到前台**（如果之前被其他 Tab 遮挡，现在变为活动 Tab） |
| 14b. 打开 World Outliner | `-Command "open_panel" -Params '{"panel_name":"WorldOutliner"}'` | `{"opened":true}` | **World Outliner 面板出现** |
| 14c. 无效面板名 | `-Command "open_panel" -Params '{"panel_name":"FakePanel123"}'` | `status: "error"`，提示未知面板 | **无变化** |

```powershell
# 14a
.\Scripts\Send-UEAgent.ps1 -Command "open_panel" -Params '{"panel_name":"OutputLog"}'
# 👀 看 Editor 下方：Output Log 面板应该出现/聚焦
```

---

### 15. close_panel

关闭一个编辑器面板。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 15a. 关闭 Output Log | `-Command "close_panel" -Params '{"panel_name":"OutputLog"}'` | `{"closed":true}` | **Output Log 面板消失**（Tab 被关闭） |
| 15b. 再次关闭（幂等） | 同上 | `{"closed":true,"already_closed":true}` | **无变化**（已经关了） |
| 15c. 重新打开 | `-Command "open_panel" -Params '{"panel_name":"OutputLog"}'` | success | **Output Log 重新出现** |

```powershell
# 15a
.\Scripts\Send-UEAgent.ps1 -Command "close_panel" -Params '{"panel_name":"OutputLog"}'
# 👀 看 Editor：Output Log 面板应该消失

# 15c — 恢复
.\Scripts\Send-UEAgent.ps1 -Command "open_panel" -Params '{"panel_name":"OutputLog"}'
```

---

### 16. get_editor_state

获取编辑器当前综合状态快照。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 16a. 正常状态 | `-Command "get_editor_state"` | 包含 `is_pie`、`is_simulating`、`selected_actors`、`modal_dialog_open`、`active_viewport` 等 | **无变化**（只读） |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "get_editor_state"
# 👀 检查返回：
#   is_pie: false（你没有在 Play In Editor 模式）
#   is_simulating: false
#   selected_actors: 数组（与 Outliner 中高亮 Actor 一致）
#   modal_dialog_open: false（没有弹窗）
#   active_viewport: {type, location, rotation}
```

**验证方法**：手动在 Editor 中选中几个 Actor → 再次调用 → 确认 selected_actors 数组内容与你手动选的一致。

---

### 17. get_level_info

获取当前关卡信息。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 17a. 查询 | `-Command "get_level_info"` | `{"level_name":"...","map_path":"/Game/...","actor_count":N,"world_type":"Editor"}` | **无变化**（只读） |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "get_level_info"
# 👀 检查返回：
#   level_name: 应该与 Editor 标题栏显示的关卡名一致
#   actor_count: 与 Outliner 中的 Actor 数量一致
#   world_type: "Editor"
```

---

### 18. get_actor_property

读取 Actor 的反射属性值。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 18a. 读 bHidden | `-Command "get_actor_property" -Params '{"actor_label":"TestCube","property_name":"bHidden"}'` | `{"value":false,"type":"bool"}` | **无变化**（只读） |
| 18b. 读位置 | `-Command "get_actor_property" -Params '{"actor_label":"TestCube","property_name":"RelativeLocation"}'` | `{"value":"(X=100,Y=200,Z=50)","type":"struct"}` | **无变化** |
| 18c. 不存在的属性 | `-Params '{"actor_label":"TestCube","property_name":"FakeProperty"}'` | `status: "error"`，提示 property not found | **无变化** |
| 18d. 不支持的 struct | `-Params '{"actor_label":"TestCube","property_name":"Tags"}'` | `status: "error"`，提示 unsupported type | **无变化** |

```powershell
# 18a
.\Scripts\Send-UEAgent.ps1 -Command "get_actor_property" -Params '{"actor_label":"TestCube","property_name":"bHidden"}'
# 👀 检查返回：value 应该是 false（TestCube 当前是可见的）

# 18b
.\Scripts\Send-UEAgent.ps1 -Command "get_actor_property" -Params '{"actor_label":"TestCube","property_name":"RelativeLocation"}'
# 👀 检查返回：value 应该包含 X=100, Y=200, Z=50
```

**验证方法**：在 Outliner 中选中 TestCube → 在 Details 面板中查看对应属性值 → 与返回值对比。

---

### 19. get_selected_actors

获取当前选中的 Actor 列表。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 19a. 先选中再查询 | 先 `select_actors ["TestCube"]` 再查询 | `{"selected":[{"label":"TestCube","class":"StaticMeshActor","location":{...},"rotation":{...}}],"count":1}` | **无变化**（只读） |
| 19b. 无选中 | 先 `execute_editor_command` `SELECT NONE` 再查询 | `{"selected":[],"count":0}` | **无变化** |

```powershell
# 19a — 先选中
.\Scripts\Send-UEAgent.ps1 -Command "select_actors" -Params '{"actor_labels":["TestCube"]}'
.\Scripts\Send-UEAgent.ps1 -Command "get_selected_actors"
# 👀 检查返回：selected 数组应包含 TestCube，且 location 与 spawn 时设置的一致
```

---

### 20. get_world_outline

获取当前关卡所有 Actor 列表（含层级和文件夹信息）。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 20a. 完整列表 | `-Command "get_world_outline"` | `{"actors":[{"label":"TestCube","class":"StaticMeshActor","folder":"...","parent":"None",...},...],"count":N}` | **无变化**（只读） |
| 20b. 过滤 | `-Command "get_world_outline" -Params '{"filter":"Test"}'` | 只返回 label 含 "Test" 的 Actor | **无变化** |

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "get_world_outline"
# 👀 检查返回：
#   actors 数组中每个条目有 label、class、folder 字段
#   数量应该与 Outliner 中看到的 Actor 数量一致
#   如果有父子关系（Actor attached to Actor），parent 字段应为父 Actor 的 label
```

**验证方法**：打开 World Outliner，对比列出的 Actor 数量和名称。

---

## 四、截图命令 (Screenshot)

### 21. take_viewport_screenshot

将当前活动视口截图保存为 PNG 文件。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 / 磁盘验证 |
|------|------|---------|---------------------------|
| 21a. 正常截图 | `-Command "take_viewport_screenshot" -Params '{"path":"C:/temp/ueagent_test.png"}'` | `{"file_path":"C:/temp/ueagent_test.png","width":1920,"height":1080,"size_bytes":N}` | **Editor 无变化**，但 `C:\temp\ueagent_test.png` 文件被创建，打开后应该显示当前视口画面 |
| 21b. 不存在的目录 | `-Params '{"path":"Z:/fake/dir/test.png"}'` | `status: "error"`，提示目录不存在 | **无文件生成** |
| 21c. 非 .png 后缀 | `-Params '{"path":"C:/temp/test.jpg"}'` | `status: "error"`，提示必须 .png | **无文件生成** |

```powershell
# 确保 C:\temp 存在
New-Item -ItemType Directory -Path "C:\temp" -Force

# 21a — 截图
.\Scripts\Send-UEAgent.ps1 -Command "take_viewport_screenshot" -Params '{"path":"C:/temp/ueagent_test.png"}'
# 👀 检查返回：width 和 height 应该与你视口尺寸一致
# 👀 打开 C:\temp\ueagent_test.png：应该是视口当前画面的截图

# 验证 PNG 文件
(Get-Item "C:\temp\ueagent_test.png").Length  # 应该 > 1KB
```

**高级验证**：先用 `set_viewport_camera` 设定一个特定角度 → 截图 → 打开图片确认角度一致。

---

## 五、属性编辑（增强版）

### 22. set_actor_property（含 before/after diff）

设置 Actor 属性并返回变化前后的值。

| 用例 | 命令 | 预期返回 | Editor 中应该看到 |
|------|------|---------|------------------|
| 22a. 隐藏 Actor | `-Command "set_actor_property" -Params '{"actor_label":"TestCube","property_name":"bHidden","value":true}'` | `{"before":"False","after":"True","changed":true}` | **TestCube 从视口中消失**（变为隐藏状态），但 Outliner 中仍然存在（显示为灰色/半透明图标） |
| 22b. 恢复可见 | 同上 `"value":false` | `{"before":"True","after":"False","changed":true}` | **TestCube 重新出现在视口中** |
| 22c. 同值不变 | 再次 `"value":false` | `{"before":"False","after":"False","changed":false}` | **无变化** |
| 22d. 设数值 | `-Params '{"actor_label":"TestCube","property_name":"MinNetUpdateFrequency","value":5}'` | before/after 值不同 | **Details 面板中对应属性值更新为 5** |
| 22e. 不支持的复杂类型 | `-Params '{"actor_label":"TestCube","property_name":"Tags","value":"test"}'` | `status: "error"`，提示 complex type not supported | **无变化** |

```powershell
# 22a — 隐藏 TestCube
.\Scripts\Send-UEAgent.ps1 -Command "set_actor_property" -Params '{"actor_label":"TestCube","property_name":"bHidden","value":true}'
# 👀 看视口：TestCube 应该消失
# 👀 看 Outliner：TestCube 仍在，但图标可能变灰
# 👀 看返回：before=False, after=True, changed=true

# 22b — 恢复
.\Scripts\Send-UEAgent.ps1 -Command "set_actor_property" -Params '{"actor_label":"TestCube","property_name":"bHidden","value":false}'
# 👀 看视口：TestCube 应该重新出现
```

---

## 六、错误路径与边界测试

### E1. PIE 模式阻断

**操作步骤**：
1. 在 Editor 中点击 **Play** 按钮进入 PIE 模式
2. 发送修改类命令

```powershell
# 在 PIE 模式下测试
.\Scripts\Send-UEAgent.ps1 -Command "execute_editor_command" -Params '{"command_name":"ACTOR SELECT ALL"}'
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_camera" -Params '{"location":{"x":0,"y":0,"z":0}}'
.\Scripts\Send-UEAgent.ps1 -Command "simulate_key" -Params '{"key":"F"}'
```

| 预期返回 | Editor 中应该看到 |
|---------|------------------|
| 所有命令返回 `status: "error"`，错误信息包含 "PIE" 相关提示 | **Editor 处于运行状态，无任何非预期变化**（命令被拒绝） |

3. 点击 **Stop** 退出 PIE 模式后重新测试 → 命令应该恢复正常

---

### E2. 缺少必填参数

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "execute_editor_command"
.\Scripts\Send-UEAgent.ps1 -Command "simulate_key"
.\Scripts\Send-UEAgent.ps1 -Command "focus_on_actor"
.\Scripts\Send-UEAgent.ps1 -Command "take_viewport_screenshot"
.\Scripts\Send-UEAgent.ps1 -Command "set_actor_property"
.\Scripts\Send-UEAgent.ps1 -Command "select_actors"
```

| 预期返回 | Editor 中应该看到 |
|---------|------------------|
| 全部返回 `status: "error"`，提示 "Missing required parameter: xxx" | **无任何变化**（安全失败） |

---

### E3. 不存在的 Actor

```powershell
.\Scripts\Send-UEAgent.ps1 -Command "focus_on_actor" -Params '{"actor_label":"ActorThatDoesNotExist"}'
.\Scripts\Send-UEAgent.ps1 -Command "get_actor_property" -Params '{"actor_label":"ActorThatDoesNotExist","property_name":"bHidden"}'
.\Scripts\Send-UEAgent.ps1 -Command "set_actor_property" -Params '{"actor_label":"ActorThatDoesNotExist","property_name":"bHidden","value":true}'
```

| 预期返回 | Editor 中应该看到 |
|---------|------------------|
| 全部返回 `status: "error"`，提示 Actor not found | **无任何变化** |

---

### E4. 组合工作流（端到端）

一个完整的自动化工作流测试：

```powershell
# 步骤 1：Spawn Actor
.\Scripts\Send-UEAgent.ps1 -Command "spawn_actor" -Params '{"class":"PointLight","label":"E2ELight","location":{"x":300,"y":0,"z":200}}'
# 👀 Editor：出现一个 PointLight

# 步骤 2：聚焦到它
.\Scripts\Send-UEAgent.ps1 -Command "focus_on_actor" -Params '{"actor_label":"E2ELight"}'
# 👀 视口：摄像机飞到 E2ELight 附近

# 步骤 3：截图留证
.\Scripts\Send-UEAgent.ps1 -Command "take_viewport_screenshot" -Params '{"path":"C:/temp/e2e_before.png"}'
# 👀 C:\temp\e2e_before.png：包含 E2ELight 的视口画面

# 步骤 4：选中并查看属性
.\Scripts\Send-UEAgent.ps1 -Command "select_actors" -Params '{"actor_labels":["E2ELight"]}'
.\Scripts\Send-UEAgent.ps1 -Command "get_selected_actors"
# 👀 返回中 selected 应包含 E2ELight

# 步骤 5：修改属性
.\Scripts\Send-UEAgent.ps1 -Command "set_actor_property" -Params '{"actor_label":"E2ELight","property_name":"bHidden","value":true}'
# 👀 视口：E2ELight 光源消失（隐藏）

# 步骤 6：再次截图
.\Scripts\Send-UEAgent.ps1 -Command "take_viewport_screenshot" -Params '{"path":"C:/temp/e2e_after.png"}'
# 👀 C:\temp\e2e_after.png：E2ELight 已不在画面中

# 步骤 7：撤销 → 灯光回来
.\Scripts\Send-UEAgent.ps1 -Command "undo"
# 👀 视口：E2ELight 重新出现

# 步骤 8：查看状态
.\Scripts\Send-UEAgent.ps1 -Command "get_editor_state"
# 👀 返回：selected_actors 包含 E2ELight，is_pie=false

# 步骤 9：清理
.\Scripts\Send-UEAgent.ps1 -Command "select_actors" -Params '{"actor_labels":["E2ELight"]}'
.\Scripts\Send-UEAgent.ps1 -Command "simulate_key" -Params '{"key":"Delete"}'
# 👀 Editor：E2ELight 被删除

# 步骤 10：确认清理
.\Scripts\Send-UEAgent.ps1 -Command "get_world_outline" -Params '{"filter":"E2E"}'
# 👀 返回：actors 数组应为空或不包含 E2ELight
```

---

## 测试清理

```powershell
# 删除测试用 Actor（如果还存在）
.\Scripts\Send-UEAgent.ps1 -Command "delete_actor" -Params '{"actor_label":"TestCube"}'
.\Scripts\Send-UEAgent.ps1 -Command "delete_actor" -Params '{"actor_label":"UndoTestLight"}'

# 恢复视口
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_mode" -Params '{"mode":"perspective"}'
.\Scripts\Send-UEAgent.ps1 -Command "set_viewport_camera" -Params '{"location":{"x":0,"y":0,"z":200},"rotation":{"pitch":-15,"yaw":0,"roll":0},"fov":90}'

# 恢复控制台设置
.\Scripts\Send-UEAgent.ps1 -Command "execute_console_command" -Params '{"command":"stat none"}'
.\Scripts\Send-UEAgent.ps1 -Command "execute_console_command" -Params '{"command":"r.ScreenPercentage 100"}'

# 删除截图
Remove-Item "C:\temp\ueagent_test.png" -ErrorAction SilentlyContinue
Remove-Item "C:\temp\e2e_before.png" -ErrorAction SilentlyContinue
Remove-Item "C:\temp\e2e_after.png" -ErrorAction SilentlyContinue
```

---

## 快速检查清单

| # | 检查项 | 通过 |
|---|--------|------|
| 1 | execute_editor_command "ACTOR SELECT ALL" → Outliner 全选 | ☐ |
| 2 | execute_console_command "stat fps" → 视口出现 FPS | ☐ |
| 3 | list_editor_commands 返回 > 100 条 | ☐ |
| 4 | undo 撤销 spawn → Actor 消失 | ☐ |
| 5 | redo 恢复 → Actor 回来 | ☐ |
| 6 | simulate_key "F" → 视口聚焦到选中 Actor | ☐ |
| 7 | simulate_key 无效键名 → error | ☐ |
| 8 | list_key_bindings 返回非空数组 | ☐ |
| 9 | set_viewport_camera 改位置 → 视口画面变化 | ☐ |
| 10 | focus_on_actor → 视口飞到 Actor | ☐ |
| 11 | set_viewport_mode "top" → 正交顶视图 | ☐ |
| 12 | get_viewport_info_detailed 返回完整字段 | ☐ |
| 13 | select_actors → Outliner 高亮 | ☐ |
| 14 | list_panels 返回 6 个已知面板 | ☐ |
| 15 | open_panel/close_panel → 面板出现/消失 | ☐ |
| 16 | get_editor_state 字段齐全 | ☐ |
| 17 | get_level_info 关卡名与 Editor 一致 | ☐ |
| 18 | get_actor_property 读到正确值 | ☐ |
| 19 | get_selected_actors 与 Outliner 选中一致 | ☐ |
| 20 | get_world_outline 数量与 Outliner 一致 | ☐ |
| 21 | take_viewport_screenshot → 磁盘有 PNG | ☐ |
| 22 | set_actor_property bHidden → Actor 隐藏/显示 | ☐ |
| 23 | PIE 模式下命令被拒绝 | ☐ |
| 24 | 缺少参数 → error | ☐ |
| 25 | E4 端到端工作流全部通过 | ☐ |

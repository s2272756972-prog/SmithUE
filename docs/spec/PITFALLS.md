# SmithUE 踩坑沉淀 (PITFALLS)

> 本文件记录开发 SmithUE 插件 + smithue-cli 过程中踩过的真实坑。新增工具/改动前过一遍,避免重复踩。

## 1. CLI 与插件版本号分叉,不要直接比较

- **现象**:CLI `0.x` vs 插件 `1.x`,`checkVersionCompat` 比 major.minor 永远不匹配,每条命令开头刷误导性 warning。
- **根因**:两者是独立产品、独立版本体系。直接比版本号是错误设计。
- **正解**:删除版本警告,以 HTTP 协议契约保证兼容;版本号仅各自语义化。

## 2. `list_tools` 返回扁平数组,不是两层结构

- **现象**:`list` / `search` 永远返回 `[]`。
- **根因**:CLI 假设 `list_tools` 无参时返回 `{domains:[...]}`(两层 domain→tools),但服务器实际返回 **扁平** `{data:{tools:[{name,category,description,params}, ...]}}`;且参数名是 `category` 不是 `domain`。
- **正解**:`listTools()` 始终返回 `data.tools`;`search` 单次拉取后按关键词过滤;参数用 `category`。
- **教训**:CLI 对插件响应结构的假设会漂移 → 需要契约快照测试(TOOL_SPEC §8)。

## 3. UDataAsset / UPrimaryDataAsset 是抽象类

- **现象**:`create_data_asset` 默认基类创建后保存失败/不持久(`SaveLoadedAsset` 静默返回 false)。报 "Cannot instantiate abstract class"。
- **根因**:`UDataAsset` 和 `UPrimaryDataAsset` 都标了 `abstract`,不能直接实例化为资产。
- **正解**:`class_path` 必填,指向**具体子类**(C++ 或 Blueprint,如 `/Game/BP_X.BP_X_C`);创建前用 `HasAnyClassFlags(CLASS_Abstract)` 拒绝抽象类。
- **通用规则**:任何 `NewObject<T>` 创建资产前都该检查目标类是否抽象。

## 4. 引擎类型的模块归属要查清

- **现象**:链接错误 `LNK2019: UPhysicalMaterial::GetPrivateStaticClass ... unresolved`。
- **根因**:`UPhysicalMaterial` 在 **PhysicsCore** 模块,不在 Engine。
- **正解**:`SmithUE.Build.cs` 补 `"PhysicsCore"` 依赖。
- **教训**:新引擎类型不一定在 Engine 模块;链接报 `GetPrivateStaticClass unresolved` 基本就是缺模块依赖。

## 5. CreateUserDefinedStruct 会塞默认成员 MemberVar_0

- **现象**:`data_create_struct` 建的结构体多一个 `MemberVar_0`(bool);用它建 DataTable 加行时报 "missing entry for MemberVar_0"。
- **根因**:`FStructureEditorUtils::CreateUserDefinedStruct` 自动创建一个默认占位成员。
- **正解**:创建后捕获默认成员 GUID,**加完真实字段后**用 `RemoveVariable` 删除(结构体必须保留 ≥1 成员,所以要后删)。

## 6. PowerShell 5.1 会损坏 CJK(中文)文件

- **现象**:用 `Get-Content -Raw` + `.Replace()` + `Set-Content -Encoding UTF8` 改含中文的 README → 文件变乱码(`鍛戒护` 之类)。
- **根因**:PowerShell 5.1 默认按系统 ANSI(中文机器是 GBK)读 UTF-8 文件 → 内存里已是乱码 → 再按 UTF-8 写回 = 双重损坏。纯 ASCII 文件(C++ 代码)不受影响。
- **正解**:
  - 含中文的文件用 **Node**(`fs.readFileSync/writeFileSync('utf8')`)编辑,中文用 `\uXXXX` 转义写进脚本(脚本本身保持纯 ASCII)。
  - 或用 opencode 的 `write`/`edit` 工具(原生 UTF-8)。
  - 损坏后用 `git checkout -- <file>` 恢复。
- **codegen 同理**:生成 TOOLS.md 必须用 Node,不用 PowerShell。

## 7. PowerShell 向原生程序传 JSON 会吞引号/拆空格

- **现象**:`node cli.js exec cmd '{"a":"b c"}'` → CLI 收到 `{a:b c}`(引号被剥)或被空格拆成多个参数。
- **消费端(已装 npm 包)正解**(smithue-cli ≥ 0.14.0):
  - 首选 `smithue-cli exec <cmd> --params-file params.json` —— 参数写进文件,绕开 shell 引号解析。**CJK(中文)参数务必走 `--params-file`**:命令行/管道代码页会把中文损坏成 `??`(CLI 自 0.14.0 起 HTTP 请求头带 `charset=utf-8`,但仍需参数以 UTF-8 文件形式进入,才能全链路不坏)。
  - 若 `--params-file` 仍有格式/编码问题,用 skill 自带脚本(均自动发现端口、UTF-8 直读文件、直发 HTTP,中文往返实测不坏),按此降级:`scripts/smithue.ps1 <cmd> params.json`(纯 PowerShell,`Invoke-RestMethod`,零 node) → 再退到 `node scripts/smithue-exec.mjs <cmd> params.json`。
- **开发者(仓库内直跑源码)正解**:用 `node -e "const {execCommand}=require('./dist/commands/exec.js'); execCommand('cmd', {a:'b c'}, {})..."` —— 参数是 JS 对象字面量,完全绕过 shell 引号。

## 8. 编辑器运行时无法编译插件

- **现象**:UBT 链接阶段失败 / DLL 被占用。
- **正解**:`Stop-Process -Name UnrealEditor -Force` → 编译 → 重新启动编辑器。改动流程固定:关 → 编 → 启 → 验。

## 9. UBT 误判 "Target is up to date"

- **现象**:改了源码但 UBT 说 "Target is up to date",不重编。
- **正解**:删 `Intermediate/Build/Win64/x64/<Target>/ActionHistory.bin`(及顶层那个)强制重建。

## 10. /ready 等 worker 线程不能碰 UObject

- **现象**:`pie_active` 曾硬编码 false,因为 `/ready` handler 在 HTTP worker 线程,不能安全访问 `GEditor->PlayWorld`。
- **正解**:用 `FThreadSafeBool bPIEActive`,在 game thread 经 `FEditorDelegates::BeginPIE/EndPIE` 更新,worker 线程只读。版本号同理(StartServer 缓存 `PluginVersion`)。

## 11. 资产路径有 bare 与 full 两种形式

- **现象**:`/Game/Foo` vs `/Game/Foo.Foo`;`GetAssetByObjectPath` 对 bare 包路径解析失败。
- **正解**:接受两种形式 —— bare 解析失败时自动补 `.AssetName` 再试(见 `sync_content_browser`)。

## 12. npm 发布的坑

- **token 过期**:granular token 默认很短(7 天)。建议生成时选 90 天,或用 `npm login` 会话 token 发布。
- **registry 指向**:本机 registry 可能是 npmmirror(淘宝源),发布要显式 `--registry https://registry.npmjs.org/`。
- **2FA**:发布需要 2FA 或 bypass-2fa 的 granular token。email OTP(登录验证码)≠ authenticator TOTP;若账户仅启用 email 验证,`npm publish --otp=` 传 authenticator 码会 403,须用 bypass-2fa 的 granular token 或 web 登录会话。
- **已配置 CI 自动发布**(2026-07 起):push `main` 且 `package.json` 版本 > registry 时,GitHub Actions(`.github/workflows/release.yml`)自动跑门禁并 `npm publish`;认证走仓库 Secret `NPM_TOKEN`(granular / bypass-2fa / 仅 smithue-cli)。日常发布只需 `npm version` + `git push`,不再手动 OTP。
- **bypass-2fa token 正在废弃**(npm 公告:账户操作 2026-08、直接发布 2027-01 起受限)。长期宜迁移到 **Trusted Publishing(OIDC)**——GitHub Actions 免 token 直接认证。权威发布流程见 CLI 仓库 [`docs/RELEASE.md`](https://github.com/123dx-svg/smithue-cli/blob/main/docs/RELEASE.md)。

## 13. 内容浏览器选中文件夹路径带 /All 前缀

- **现象**:`get_content_browser_selection` 返回 `/All/Game/Foo`,但 `list_assets` 要 `/Game/Foo`。
- **正解**:调用 `FSmithUECommonUtils::NormalizeContentBrowserPath()`（内部使用引擎官方 `UContentBrowserDataSubsystem::TryConvertVirtualPath`）。字符串截断 (`RightChop(4)` / `Mid(4)`) 对插件路径错误（`/All/Plugins/Foo/BP` → 应为 `/Foo/BP`，截断只能得到 `/Plugins/Foo/BP`）。已在 commit 6c2fa73 修复。`SmithUE.Build.cs` 需含 `"ContentBrowserData"` 模块依赖。

## 14. 工具 description 未暴露硬边界 → AI 误选工具、错误无指向

- **现象**：Agent 看到 `bp_compile_code`，凭工具名直觉判断它能处理事件逻辑（Tick/BeginPlay 等），调用失败后收到裸 `"Invalid function signature"` 错误，无任何提示；只能翻 `SmithUEBpCompiler.cpp` 源码才发现"只支持函数图"的边界。
- **根因**：工具注册的 `description` 是 `"Compile Blueprint DSL into a Blueprint"`，未说明"只编译函数图"、不支持事件、不支持嵌套 if。错误字符串也无指向，不告知正确的替代工作流。同类问题在 14 个工具上同时存在（bp_validate_code、bp_batch_op、bp_focus_node、create_data_asset、add_widget、level_new/level_open 等）。
- **正解**：见 **[TOOL_SPEC §3.1](TOOL_SPEC.md)**。description 必须在"被挑选的那一刻"暴露硬边界；面向 AI 的常见误用错误必须可操作（指向正确工具或替代工作流）。对于 `bp_compile_code`：
  - 在 `ValidateSyntax` 中加窄事件守卫：签名行首 token 为 `event`，或 `ParseSignatureText` 失败且签名行含规范 UE 事件名 → 返回重定向错误（指向 `bp_override_function → bp_create_node → bp_batch_op → bp_compile`）。
  - 守卫**不能**扫描函数体 token、**不能**拒绝 `ParseSignatureText` 成功的签名（`void Tick()` 是合法函数名，应能编译）。
- **教训**：description 是 AI 的"说明书"，`/api/v1/tools` 输出和 TOOLS.md 是 AI 唯一可见的文档；藏在 .cpp 里的边界等于不存在。新增工具必须过 TOOL_SPEC §3.1 检查清单。

## 15. 材质输入索引 & set_expression_property 节点属性键盲区

- **现象**：连 WorldPositionOffset 时 `connect_material_pins` 的 `dest_input_index` 该填几无文档（TOOLS.md 只写到 6=OpacityMask）；`set_expression_property` 传错键（如给 Constant 传 `R` 而非 `value`）只回 "No recognized properties were set"，不告知合法键——只能翻 `SmithUEMaterialCommands.cpp` 的 `GetMaterialBaseInput()` / `HandleSetExpressionProperty()` 才知道。
- **根因**：工具 schema 把"按节点类型才合法的键"和"材质主输出索引表"藏在 .cpp 里，对 AI 不可见；错误信息无指向。
- **正解**（已落地，对齐 [TOOL_SPEC §3.1](TOOL_SPEC.md)）：
  - `dest_input_index` 描述补全 `7=WorldPositionOffset`（8+ 不支持）。
  - `set_expression_property` 描述列出按节点类型的合法键；失败错误改为**回显该节点的合法键**（`GetSettablePropertyKeys()` 助手按节点类型推出），如 `"Valid keys for this node: value (plus 'description')"`。
- **通用规则**：任何"按子类型 / 索引才合法"的参数，合法集必须进 description；不匹配时错误回显合法集。**描述里写工具契约（索引、键名），不写 UE 版本特有的引擎 API**（如 HLSL intrinsic 名）——后者随引擎版本漂移会误导。

## 16. UBT 编译:系统 dotnet 缺 .NET 10 → 用引擎自带 Build.bat

- **现象**:直接 `dotnet "{EngineRoot}\...\UnrealBuildTool.dll" {Project}Editor ...` 报 `You must install or update .NET to run this application. ... 'Microsoft.NETCore.App', version '10.0.0'`,机器只装到 9.0。
- **根因**:调用了 PATH 里的**系统** `dotnet`,而 UE 5.8 的 UBT 需要 .NET **10** runtime;系统未装 10 → 启动失败。
- **正解**:用引擎封装的 **`{EngineRoot}\Engine\Build\BatchFiles\Build.bat {Project}Editor Win64 Development "{ProjectRoot}\{Project}.uproject" -WaitMutex`**——它自动选用引擎**内置**的 dotnet(带 .NET 10)。或直接调 `{EngineRoot}\Engine\Binaries\ThirdParty\DotNet\<ver>\win-x64\dotnet.exe`。**别用系统 `dotnet`。**(v1.16.1 重编时实测:Build.bat 增量编译单个 .cpp + 链接 DLL 仅 ~5 秒。)

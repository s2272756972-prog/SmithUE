# 贡献指南

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh.md)

SmithUE 的成长离不开社区的贡献。**即使你不会写代码，也可以参与进来。**
无论你是美术、TA、策划还是 C++ 开发者，都有合适的方式让 SmithUE 变得更好。

## 贡献方式

| | 类型 | 参与者 | 内容 |
|---|---|---|---|
| 🐛 | 报告 Bug | 所有人 | 命令报错了？行为不符合预期？ |
| 💡 | 申请新功能 | 所有人 | 希望 AI 能够自动完成 SmithUE 目前还做不到的编辑器任务？ |
| 🎨 | 分享工作流 | 美术 / TA | 发现了一系列对工作流非常有帮助的命令组合？ |
| 📖 | 改进文档 | 所有人 | 发现说明不够清晰或者缺少示例？ |
| 🔧 | 添加新命令 | C++ 开发者 | 在 Plugin 中实现新的编辑器命令 |

## 面向非程序员（美术 & TA）

你不需要精通 C++ 也能为 SmithUE 做出贡献。最有价值的贡献往往来自每天都在使用编辑器的用户。

### 如何提交 Bug 报告

如果某个命令没有按预期工作，请在 GitHub 上提交一个 [Issue](https://github.com/123dx-svg/SmithUE/issues/new)。

一个优秀的 Bug 报告应包含：
- **你尝试执行的操作**，例如：“我让 AI 创建一个材质实例”
- **使用的命令**，例如：`create_material_instance`
- **发生的情况**，例如：报错 “Missing parameter 'parent'”
- **你的预期结果**，例如：在 /Game/Materials 下创建一个新的材质实例
- **你的 UE 版本**，例如：UE 5.2

你不需要提供代码或复现项目。清晰的文字描述就足够了。

### 如何申请新功能

思考一下你在 Unreal Editor 中有哪些耗时的手动操作：
- 在 20 个材质实例中修改相同的材质参数？
- 按特定模式批量重命名 Actor？
- 设置灯光预设？

提交一个 [Issue](https://github.com/123dx-svg/SmithUE/issues/new) 并描述：
- **你想要自动化的编辑器任务**
- **你执行该任务的频率**
- **理想的结果是什么样的**

就是这样。你的建议将决定我们接下来的开发方向。

### 分享工作流

如果你在美术管线中发现了一系列非常有用的 SmithUE 命令组合（例如：标准的角色材质设置流程、批量 LOD 配置流），请在 [GitHub Discussions](https://github.com/123dx-svg/SmithUE/discussions) 中分享。这可能会催生出新的命令或文档示例。

### Issue 标签

在你提交 Issue 时，可以添加标签来帮助我们分类：
- `bug`: 功能运行异常
- `feature-request`: 希望添加的新能力
- `workflow`: 探讨管线或使用模式
- `docs`: 文档不清晰或不完整

## 面向开发者

添加一个新命令大约需要 15 分钟。它能让 AI 代理以全新的方式与 Unreal Engine 交互。

### AI 辅助开发

如果你使用 OpenCode、Claude Code 或 Cline 等 AI 编码工具参与 SmithUE 的开发，建议安装配套的开发技能。它会帮助你的 AI 助手理解项目的架构、规范和工作流程。

```
# Skill 文件位置：
Docs/smithue-dev/SKILL.md
```

各工具安装方式：

- **OpenCode**: 将 `Docs/smithue-dev/` 复制到 `~/.agents/skills/smithue-dev/`
- **Claude Code**: 运行 `claude skill add ./Docs/smithue-dev/SKILL.md`
- **其他工具**: 将 `Docs/smithue-dev/SKILL.md` 的内容添加到 AI 工具的 system prompt 或 skill 配置中。

### 环境要求

开发和编译 SmithUE Plugin 需要：

- 已安装 Unreal Engine 5.2
- 安装了 “使用 C++ 的游戏开发” 工作负载的 Visual Studio 2022
- Node.js 18+（用于 MCP Server）
- Git（用于克隆仓库）

### 快速开始

```bash
git clone -b UE5.2 https://github.com/123dx-svg/SmithUE.git
cd SmithUE
```

### 仓库结构

项目分为 C++ Plugin 和 TypeScript MCP Server 两部分：

```text
SmithUE/
├── Source/SmithUE/
│   ├── Private/Commands/    # 领域命令实现
│   ├── Private/Transport/   # HTTP 服务与连接管理
│   ├── Private/UI/          # 编辑器状态指示器
│   └── Public/ToolRegistry/ # Schema 与 Registry 核心
├── Scripts/
│   └── SmithUE-MCP/         # TypeScript MCP Server
├── Docs/
│   └── smithue-dev/         # AI 开发技能
│       └── SKILL.md
├── CONTRIBUTING.md
└── SmithUE.uplugin
```

注意：添加新的 UE 命令不需要修改 `Scripts/SmithUE-MCP/`。MCP Server 会自动发现 Plugin 中的命令。

### 如何添加新命令

按照以下步骤在 SmithUE Plugin 中实现新命令。

#### 第 1 步：选择正确的领域文件

命令在 `Source/SmithUE/Private/Commands/` 中按领域分组。找到最适合你命令的文件。例如，检查类工具使用 `SmithUEObservationCommands.cpp`。

#### 第 2 步：添加头文件声明

在 `Source/SmithUE/Public/Commands/` 中对应的 `.h` 文件里声明你的处理函数。

```cpp
// SmithUEObservationCommands.h
private:
    static TSharedPtr<FJsonObject> HandleMyNewCommand(const TSharedPtr<FJsonObject>& Params);
```

#### 第 3 步：定义工具 Schema

在 `.cpp` 文件的 `RegisterTools` 函数中，通过定义 `FSmithUEToolSchema` 来注册你的命令。该 Schema 会告知系统和 AI 工具的功能以及它接收的参数。

```cpp
Registry.Register(
    FSmithUEToolSchema(
        TEXT("my_new_command"), 
        TEXT("Observation"), // 分类
        TEXT("Description of what the command does"),
        {
            FSmithUEToolParam(TEXT("param_name"), TEXT("string"), TEXT("Parameter description"), true)
        }
    ),
    [](const TSharedPtr<FJsonObject>& Params) { return HandleMyNewCommand(Params); }
);
```

#### 第 4 步：实现处理函数

实现处理函数。它必须接收一个用于参数的 `TSharedPtr<FJsonObject>`，并返回一个包含响应的 `TSharedPtr<FJsonObject>`。

```cpp
TSharedPtr<FJsonObject> FSmithUEObservationCommands::HandleMyNewCommand(const TSharedPtr<FJsonObject>& Params)
{
    FString MyParam;
    if (!Params->TryGetStringField(TEXT("param_name"), MyParam))
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Missing parameter: param_name"));
    }

    // 在此处编写逻辑...

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("result"), TEXT("Success!"));
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
```

#### 第 5 步：编译

使用 UBT 编译 Plugin。运行以下命令：

```powershell
dotnet "{EngineRoot}/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" {ProjectName}Editor Win64 Development "-Project={ProjectRoot}/{ProjectName}.uproject" -WaitMutex
```

请根据你的本地环境调整 `{EngineRoot}`、`{ProjectRoot}` 和 `{ProjectName}`。

#### 第 6 步：测试命令

在通过 MCP Server 尝试之前，你可以直接通过 HTTP 测试命令：

```bash
curl -X POST http://localhost:13721/api/v1/execute -H "Content-Type: application/json" -d '{"command":"my_new_command","params":{"param_name":"test"}}'
```

MCP Server 会自动从 Plugin 中发现新命令。不需要修改 TypeScript 代码。

### 代码规范

- **响应格式**: 所有处理函数必须通过 `FSmithUECommonUtils::CreateSuccessResponse(Data)` 或 `CreateErrorResponse(Message)` 返回 JSON 对象。最终的封包将包含 `status: "success"|"error"` 和 `data: {...}`。
- **日志**: 在 Plugin 中使用 `SMITHUE_LOG` 宏来记录统一的日志。
- **验证**: 始终在处理函数开始处验证参数。对于无效或缺失的输入，返回清晰的错误信息。
- **分类**: 命令分为 18 个领域（System, Project, Material, Asset, Editor, Interaction, Blueprint, Viewport, Observation, Analysis, Niagara, Level, Data, Sequencer, Environment, PIE, Animation, Input）。当 3 个或更多相关命令形成一个独特的组时，可以添加新领域。详情请参考 `Docs/smithue-dev/SKILL.md`。
- **完整参考**: 查看 [TOOLS.md](TOOLS.md) 获取包含参数 Schema 的完整工具参考。
- **兼容性**: 仅针对 Unreal Engine 5.2。避免使用 5.3 或更高版本中引入的 API。

### 示例：添加 "list_actors"

这是一个添加列出当前关卡中 Actor 命令的简化示例。

**注册:**
```cpp
Registry.Register(
    FSmithUEToolSchema(TEXT("list_actors"), TEXT("Observation"), TEXT("Lists all actors in the level")),
    [](const TSharedPtr<FJsonObject>& Params) { return HandleListActors(Params); }
);
```

**实现:**
```cpp
TSharedPtr<FJsonObject> FSmithUEObservationCommands::HandleListActors(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> ActorList;
    for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
    {
        TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
        ActorObj->SetStringField(TEXT("label"), It->GetActorLabel());
        ActorList.Add(MakeShared<FJsonValueObject>(ActorObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("actors"), ActorList);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
```

### Pull Request 检查清单

- [ ] 命令编译通过，无错误或警告。
- [ ] 处理函数返回正确的 JSON 封包格式。
- [ ] 参数已验证，且错误得到了妥善处理。
- [ ] 命令已在正确的领域文件中注册。
- [ ] 已通过 `curl` 或 MCP Server 验证功能。

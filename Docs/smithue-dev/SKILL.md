---
name: smithue-dev
description: SmithUE UE5 插件开发技能。标准化 AI 辅助贡献流程：添加命令、修复 Bug、编译、测试。当用户提到 SmithUE、ToolRegistry、FSmithUEToolSchema，或需要在 SmithUE 项目中添加/修改编辑器自动化命令时，使用此技能。
---

# SmithUE 开发技能

你是 SmithUE 贡献者助手。SmithUE 是一个 UE5 编辑器自动化插件，通过 HTTP（默认端口 13721）暴露编辑器命令，并通过 MCP 协议桥接至 AI 工具。命令集持续增长中，鼓励开发者在使用过程中新增和完善工具。

## 架构概览

SmithUE 采用清晰的分层架构，将虚幻引擎编辑器功能安全地暴露给 AI 智能体：

*   **UE5 C++ 插件**：核心引擎逻辑，在默认端口 13721 上运行 HTTP 服务。
*   **ToolRegistry 模式**：命令通过 `FSmithUEToolSchema` 定义，在 `RegisterTools()` 中注册。
*   **8 大领域**：命令分组为 System、Asset、Material、Editor、Blueprint、Viewport、Observation、Analysis。
*   **TypeScript MCP 服务**：元工具架构的桥接层。添加新 UE 命令无需修改 TypeScript 代码。
*   **响应封装**：所有命令返回 JSON 对象：`{ "status": "success"|"error", "data": { ... } }`。
*   **连接状态指示器**：编辑器右下角圆形图标实时显示 MCP 客户端连接状态（绿/红/红黄闪烁）。

## 开发前准备（重要）

### 拉取正确分支

开始开发前，必须拉取与当前引擎版本对应的仓库分支：

```bash
cd {YourProject}/Plugins/SmithUE
git fetch origin
git checkout UE5.2          # 对应 UE 5.2 引擎
git pull origin UE5.2
```

> 分支命名规则：`UE{主版本}.{次版本}`（如 `UE5.2`、`UE5.3`）。始终确保本地分支与你使用的引擎版本一致。

### 仓库地址

```
https://github.com/123dx-svg/SmithUE.git
```

## 添加新命令

鼓励在使用过程中发现需求并新增命令。按以下步骤实现：

1.  **选择领域**：在 `Source/SmithUE/Private/Commands/SmithUE{Domain}Commands.cpp` 中找到对应领域文件。
2.  **声明头文件**：在 `Source/SmithUE/Public/Commands/` 对应的 `.h` 文件中添加静态处理函数声明。
    ```cpp
    static TSharedPtr<FJsonObject> HandleMyCommand(const TSharedPtr<FJsonObject>& Params);
    ```
3.  **定义 Schema**：创建 `FSmithUEToolSchema`，包含名称、分类、描述和参数。
4.  **实现处理函数**：编写逻辑，返回 `TSharedPtr<FJsonObject>`，使用 status/data 封装格式。
5.  **注册**：在 `RegisterTools` 函数中调用 `Registry.Register(schema, handler)`。
6.  **编译**：使用 UnrealBuildTool 编译（路径根据你的引擎安装位置调整）：
    ```bash
    dotnet "{EngineRoot}/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
      {ProjectName}Editor Win64 Development \
      "-Project={ProjectRoot}/{ProjectName}.uproject" -WaitMutex
    ```
7.  **测试**：通过 curl 请求验证：
    ```bash
    curl -X POST http://localhost:13721/api/v1/execute \
      -H "Content-Type: application/json" \
      -d '{"command":"your_command","params":{}}'
    ```
8.  **提交**：验证通过后提交代码（见下方 Git 工作流）。

> **提示**：添加新命令后，MCP 元工具会自动发现它，无需修改 TypeScript 代码。使用 `smithue_list_domain()` 确认新命令已注册。

## 新增领域（Domain）

当现有 8 大领域（System、Asset、Material、Editor、Blueprint、Viewport、Observation、Analysis）无法涵盖你的新命令时，可以新增领域。遵循以下原则：

### 何时新增领域

*   现有领域中没有语义匹配的分组。
*   你的命令集围绕一个明确的功能主题（如 Animation、Audio、Landscape）。
*   预计该主题下会有 3 个以上命令。如果只有 1-2 个命令，优先归入最相近的现有领域。

### 新增步骤

1.  **创建命令文件**：
    ```text
    Source/SmithUE/Private/Commands/SmithUE{NewDomain}Commands.cpp
    Source/SmithUE/Public/Commands/SmithUE{NewDomain}Commands.h
    ```

2.  **头文件模板**：
    ```cpp
    #pragma once
    #include "CoreMinimal.h"

    class FSmithUEToolRegistry;

    class FSmithUE{NewDomain}Commands
    {
    public:
        static void RegisterTools(FSmithUEToolRegistry& Registry);
    private:
        // 每个命令一个静态处理函数
        static TSharedPtr<FJsonObject> HandleMyCommand(const TSharedPtr<FJsonObject>& Params);
    };
    ```

3.  **在模块中注册**：在 `SmithUEModule.cpp` 的 `StartupModule()` 中添加：
    ```cpp
    #include "Commands/SmithUE{NewDomain}Commands.h"
    // ... 在现有 RegisterTools 调用之后
    FSmithUE{NewDomain}Commands::RegisterTools(FSmithUEToolRegistry::Get());
    ```

4.  **Category 名称**：在 `FSmithUEToolSchema` 中使用与领域同名的 Category 字符串（首字母大写，如 `TEXT("Animation")`）。MCP 元工具的 `smithue_list_domain()` 会自动发现新领域。

### 命名原则

*   **领域名称**：使用 UE 生态中广泛认知的术语（如 `Animation` 而非 `Anim`，`Landscape` 而非 `Terrain`）。
*   **命令名称**：`动词_名词` 格式，小写下划线分隔（如 `play_animation`、`get_landscape_info`）。
*   **参数名称**：小写下划线分隔，语义明确（如 `actor_name`、`asset_path`）。

### 注意事项

*   新领域的所有命令必须遵循相同的代码规范（返回格式、参数校验、日志记录）。
*   不要创建过于宽泛的领域（如 "Misc" 或 "Utils"），保持每个领域有明确的职责边界。
*   添加完成后，使用 `smithue_list_domain()` 确认新领域已出现在列表中。

## 代码规范

*   **返回格式**：使用 `FSmithUECommonUtils::CreateSuccessResponse(Data)` 或 `CreateErrorResponse(Message)`。
*   **日志**：使用 `SMITHUE_LOG` 宏记录所有内部日志。
*   **参数校验**：在每个处理函数开头验证参数，对缺失或格式错误的输入返回清晰的错误信息。
*   **版本锁定**：严格使用与当前分支对应的 UE API 版本，不使用更高版本特性。
*   **依赖限制**：不允许引入第三方 C++ 库。

## 文件组织

```text
Source/SmithUE/
├── Private/Commands/SmithUE{Domain}Commands.cpp  # 命令实现
├── Public/Commands/SmithUE{Domain}Commands.h     # 声明
├── Private/Blueprint/                             # 底层蓝图 API
├── Private/Transport/                             # HTTP 服务 & 连接管理
├── Private/UI/                                    # 编辑器状态指示器
└── Public/ToolRegistry/                           # Schema/Registry 核心逻辑
```

MCP 服务（`Scripts/SmithUE-MCP/`）位于插件内部：
```text
Scripts/SmithUE-MCP/
├── src/          # TypeScript 源码（开发者修改）
├── dist/
│   └── bundle.js # 预构建单文件包（用户直接使用）
├── package.json
└── tsconfig.json
```

## Git 工作流

### 提交代码

```bash
cd {YourProject}/Plugins/SmithUE
git add .
git commit -m "feat(Domain): 简短描述你的改动"
git push origin UE5.2
```

### 处理冲突

如果推送时遇到冲突（其他开发者修改了相同文件）：

```bash
git pull origin UE5.2
# Git 会提示冲突文件
# 打开冲突文件，对比 <<<<<<< HEAD（你的版本）和 >>>>>>> origin（远程版本）
# 决定保留哪个版本，或手动合并两者
git add {冲突文件}
git commit -m "merge: 解决与远程的冲突"
git push origin UE5.2
```

> **原则**：遇到冲突时，对比线上和本地的区别，理解两边的改动意图，最终由你决定保留的版本。不要盲目覆盖他人代码。

### MCP 服务变更（仅开发者）

如果修改了 `Scripts/SmithUE-MCP/src/` 下的 TypeScript 源码：

```bash
cd {YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP
npm install    # 仅首次或依赖变更时
npm run build  # tsc 类型检查 + esbuild 打包
```

构建后 `dist/bundle.js` 会自动更新，将其一并提交即可。终端用户无需执行此步骤。

## 约束条件

*   **环境**：与分支版本对应的 UE 引擎（如 `UE5.2` 分支对应 UE 5.2）。
*   **网络**：默认 HTTP 端口 13721（可通过 `-ueagenthttpport=` 命令行参数覆盖）。
*   **MCP 协议**：采用元工具模式（3 个固定工具），不要在 TypeScript 中逐个注册工具。
*   **范围**：支持 `smithue serve`。命令行执行模式（`smithue exec`）在 v1 中不支持。
*   **分发**：`dist/bundle.js` 是预构建产物，提交到 Git，终端用户（如美术人员）无需 npm 操作。

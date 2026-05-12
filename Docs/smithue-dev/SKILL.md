---
name: smithue-dev
description: SmithUE/UEAgent UE5 插件开发技能。标准化 AI 辅助贡献流程：添加命令、修复 Bug、编译、测试。当用户提到 SmithUE、UEAgent、ToolRegistry、FSmithUEToolSchema，或需要在 SmithUE 项目中添加/修改编辑器自动化命令时，使用此技能。
---

# SmithUE 开发技能

你是 SmithUE 贡献者助手。SmithUE 是一个 UE5.2 编辑器自动化插件，通过 HTTP（端口 13721）暴露 65+ 编辑器命令，并通过 MCP 协议桥接至 AI 工具。

## 架构概览

SmithUE 采用清晰的分层架构，将虚幻引擎编辑器功能安全地暴露给 AI 智能体：

*   **UE5 C++ 插件**：核心引擎逻辑，在端口 13721 上运行 HTTP 服务。
*   **ToolRegistry 模式**：命令通过 `FSmithUEToolSchema` 定义，在 `RegisterTools()` 中注册。
*   **8 大领域**：命令分组为 System、Asset、Material、Editor、Blueprint、Viewport、Observation、Analysis。
*   **TypeScript MCP 服务**：自动发现 UE 插件中的命令的桥接层。添加新 UE 命令无需修改 TypeScript 代码。
*   **响应封装**：所有命令返回 JSON 对象：`{ "status": "success"|"error", "data": { ... } }`。

## 添加新命令

按以下步骤实现新的自动化命令：

1.  **选择领域**：在 `Source/SmithUE/Private/Commands/SmithUE{Domain}Commands.cpp` 中找到对应领域文件。
2.  **声明头文件**：在 `Source/SmithUE/Public/Commands/` 对应的 `.h` 文件中添加静态处理函数声明。
    ```cpp
    static TSharedPtr<FJsonObject> HandleMyCommand(const TSharedPtr<FJsonObject>& Params);
    ```
3.  **定义 Schema**：创建 `FSmithUEToolSchema`，包含名称、分类、描述和参数。
4.  **实现处理函数**：编写逻辑，返回 `TSharedPtr<FJsonObject>`，使用 status/data 封装格式。
5.  **注册**：在 `RegisterTools` 函数中调用 `Registry.Register(schema, handler)`。
6.  **编译**：使用 UnrealBuildTool 命令：
    ```bash
    dotnet "E:\Program Files\Epic Games\UE_5.2\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" SmithUE Win64 Development "-Project=F:\DXProject\AIScript\AIScript.uproject" -WaitMutex
    ```
7.  **测试**：通过 curl 请求验证：
    ```bash
    curl -X POST http://localhost:13721 -d '{"command":"your_command"}'
    ```

## 代码规范

*   **返回格式**：使用 `FSmithUECommonUtils::CreateSuccessResponse(Data)` 或 `CreateErrorResponse(Message)`。
*   **日志**：使用 `SMITHUE_LOG` 宏记录所有内部日志。
*   **参数校验**：在每个处理函数开头验证参数，对缺失或格式错误的输入返回清晰的错误信息。
*   **版本锁定**：严格使用 UE 5.2 API，不使用 5.3+ 特性。
*   **依赖限制**：不允许引入第三方 C++ 库。

## 文件组织

```text
Source/SmithUE/
├── Private/Commands/SmithUE{Domain}Commands.cpp  # 命令实现
├── Public/Commands/SmithUE{Domain}Commands.h     # 声明
├── Private/Blueprint/                             # 底层蓝图 API
└── Public/ToolRegistry/                           # Schema/Registry 核心逻辑
```

MCP 服务（`Scripts/SmithUE-MCP/`）位于插件内部：
```text
Scripts/SmithUE-MCP/src/
├── client.ts    # SmithUE HTTP 客户端
├── tools.ts     # MCP 元工具定义
└── index.ts     # 服务入口
```

## 约束条件

*   **环境**：仅支持 UE 5.2。
*   **网络**：默认 HTTP 端口 13721（可通过 `-ueagenthttpport=` 覆盖）。
*   **MCP 协议**：采用元工具模式（3 个固定工具），不要在 TypeScript 中逐个注册工具。
*   **范围**：支持 `smithue serve`。命令行执行模式（`smithue exec`）在 v1 中不支持。

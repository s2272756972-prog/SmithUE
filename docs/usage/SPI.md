# SmithUE SPI：/api/v1/tools HTTP 契约

任何实现以下协议的 server 都可被 `smithue-cli` 和 AI agent 无缝驱动，无需修改 CLI。

## 端口发现

1. server 启动时写端口文件：`%LOCALAPPDATA%\.smithue\<pid>.port`
2. 文件内容为 JSON：`{"port":13721,"pid":12345,"project_name":"MyProject","started_at":"ISO"}`
3. CLI 扫描目录，按 `started_at` 选最近实例

## `/ready` 端点

`GET /ready` → `{"ready": true, "data": {"tools_count": N}}`

server 未就绪时返回 503；就绪后返回 200。CLI 在调用工具前轮询此端点。

## `/api/v1/tools` 端点

`GET /api/v1/tools` → 返回工具列表：

```json
{
  "data": {
    "tools": [
      {
        "name": "tool_name",
        "category": "Category",
        "description": "描述",
        "parameters": [
          { "name": "param", "type": "string", "description": "desc", "required": true }
        ]
      }
    ]
  }
}
```

## `/api/v1/execute` 端点

`POST /api/v1/execute` body: `{"command":"tool_name","params":{...}}`

成功：`{"success":true,"data":{...}}`
失败：`{"success":false,"error":{"message":"..."}}`

## 版本承诺（v1）

- `/api/v1/tools` 工具列表和参数 schema 在 v1 内向后兼容
- 新工具只增不删（已有工具名/参数名不变）
- Breaking change 通过新 API 版本（`/api/v2/`）引入

## 参考实现

SmithUE UE5 C++ 插件：`Source/SmithUE/Private/Transport/SmithUEHttpServer.cpp`

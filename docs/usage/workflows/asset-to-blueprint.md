# 工作流：资产 → 合规襃图工厂

## 前置条件

- UE 编辑器已启动，SmithUE 插件 ready（`smithue-cli status` 返回 `ready:true`）
- 宿主工程已有 `smithue.config.json` + `.smithue/specs/<spec-id>.json`

## 步骤

### 1. 准备规范

```bash
# 方式 A：从黄金 BP 反推草稿
smithue-cli spec infer --from /Game/MyProject/Props/BP_Crate_Golden --out .smithue/specs/prop.json
# 然后人工确认 naming.pattern（标 needs-confirm 的字段）

# 方式 B：AI 生成（向 AI agent 描述规范，AI 读 spec schema 生成）
# "帮我生成一份道具襃图规范，继承 BP_PropBase，StaticMeshComponent，BlockAll碰撞"
```

### 2. dry-run 预览

```bash
smithue-cli factory --spec prop --dry-run --out plan.json
# 检查 plan.json：operations 列出将创建/跳过的 BP
```

### 3. 查看结果

dry-run 输出 `FactoryPlan`（JSON），包含每个资产的操作类型：
- `create_bp`：将创建
- `skip_existing`：BP 已存在，交 lint 审计
- `skip_name_collision`：命名冲突，需人工处理
- `skip_not_owned`：不在 ownership 范围

> v1 注意：`--apply` 尚未实现，使用 dry-run 规划后由 AI agent 调用原子工具执行。

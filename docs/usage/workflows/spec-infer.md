# 工作流：从黄金 BP 反推规范（spec infer）

## 用途

TA 手工做一个合规 BP → 工具自动反推 spec 草稿，免手写 JSON。

## 步骤

```bash
smithue-cli spec infer --from /Game/Props/BP_Crate_Golden --out .smithue/specs/prop.json
```

## 草稿说明

- `naming.required=false`：命名规则从单例无法确定，需人工确认后改 `true`
- `ownership.folderGlobs`：从 BP 所在目录推导，可按需扩展
- 继承组件（`inherited_unverifiable=true`）被排除在外，不进规范

## 人工确认清单

- [ ] `rules.naming.pattern`：修改为实际命名规则，改 `required: true`
- [ ] `ownership.folderGlobs`：扩展到所有目标目录
- [ ] 验证：跑 `smithue-cli lint --spec prop` 对黄金 BP 应 0 findings

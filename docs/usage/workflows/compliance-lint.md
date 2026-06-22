# 工作流：合规审计（Linter）

## 用途

扫描现有襃图，与 spec 规范比对，输出违规报告。report-only（不修改任何资产）。

## 步骤

```bash
# 审计并输出报告
smithue-cli lint --spec prop --out report.json

# CI 模式（有违规时非零退出）
smithue-cli lint --spec prop --out report.json || echo "COMPLIANCE VIOLATIONS FOUND"
```

## 报告格式

```json
{
  "spec_id": "prop",
  "findings": [
    {
      "asset_path": "/Game/Props/BP_OldCrate",
      "rule": "collision.profile",
      "expected": "BlockAll",
      "actual": "OverlapAll",
      "severity": "error"
    }
  ],
  "unverifiable": [
    "/Game/Props/BP_Child#InheritedMesh"
  ],
  "checked_assets": 12
}
```

`unverifiable`：继承组件盲区，无法自动判定（不算违规，需人工确认）。

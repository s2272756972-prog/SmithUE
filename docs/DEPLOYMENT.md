# SmithUE Deployment Guide / 部署指南

> 快速入门和基础安装请参见 [README.md](../README.md)。
> See [README.md](../README.md) for quick start and basic setup.

---

## 任意项目部署 / Deploy to Any UE Project

SmithUE 的 `UE5.1` 分支可以部署到任意 UE 5.1 项目，无需绑定特定项目目录。
The SmithUE `UE5.1` branch can be deployed into any UE 5.1 project. It is not tied to a specific project directory.

### 安装方式 / Installation Methods

#### 1. Symlink（推荐 / Recommended）

Symlink 模式让项目自动跟随 SmithUE 源码仓库更新。
Symlink keeps your project in sync with the SmithUE source repo automatically.

```powershell
# Install SmithUE into <YourProject>/Plugins via symlink (default mode)
& "<SmithUE-repo>\scripts\install-smithue.ps1" -ProjectPluginsDir "<YourProject>\Plugins"

# Force replace if SmithUE already exists (creates timestamped SmithUE.bak-<timestamp> backup)
& "<SmithUE-repo>\scripts\install-smithue.ps1" -ProjectPluginsDir "<YourProject>\Plugins" -Force
```

> **Note:** Windows 上创建符号链接需要开发者模式（Developer Mode）或管理员权限。两者都不可用时，请改用 `-Mode copy`。
> Symlink requires Windows Developer Mode or admin rights. If unavailable, use `-Mode copy`.

符号链接模式下，多个项目共享同一份插件源码，更新一处即可同步所有项目。脚本可重复执行：若目标已是指向同一源码的链接，直接报告成功并退出。
With symlinks, multiple projects share one copy of the plugin source. Update once, sync everywhere. The script is idempotent: if the target is already a link pointing at the same source, it reports success and exits.

#### 2. Copy（复制 / Copy）

```powershell
& "<SmithUE-repo>\scripts\install-smithue.ps1" -ProjectPluginsDir "<YourProject>\Plugins" -Mode copy
```

适用于需要独立副本、或无法使用管理员权限创建符号链接的场景。文件只复制一次，SmithUE 更新后需重新运行脚本（配合 `-Force`）。
For cases where an independent copy is needed, or symlink creation (admin rights) is unavailable. Files are copied once; future SmithUE updates require re-running the script (with `-Force`).

#### 3. Engine-Level Install（引擎级安装）

```powershell
# Install to the engine's plugin tree, available to all projects using this engine.
# With -EngineLevel the path is used AS-IS (no SmithUE subfolder is appended),
# so pass the full final directory including the plugin folder name:
& "<SmithUE-repo>\scripts\install-smithue.ps1" -ProjectPluginsDir "<UE_Engine>\Engine\Plugins\Marketplace\SmithUE" -EngineLevel -Mode copy
```

将插件安装到 `<UE_Engine>\Engine\Plugins\Marketplace`，所有使用该引擎的项目均可启用。
Installs into `<UE_Engine>\Engine\Plugins\Marketplace`, making the plugin available to every project on that engine.

### 参数说明 / Script Parameters

| 参数 / Parameter | 必填 / Required | 默认值 / Default | 说明 / Description |
|---|---|---|---|
| `-ProjectPluginsDir` | ✅ | (none) | Target plugins directory (`<YourProject>\Plugins`). With `-EngineLevel`, used as-is as the final install path. |
| `-Source` | ❌ | Script parent dir (repo root) | Path to SmithUE repo root (must contain `SmithUE.uplugin`) |
| `-Mode` | ❌ | `symlink` | `symlink` or `copy` |
| `-EngineLevel` | ❌ | off | Use `-ProjectPluginsDir` as-is instead of appending a `SmithUE` subfolder (for engine-level installs) |
| `-Force` | ❌ | off | Replace existing install (existing copy is backed up as `SmithUE.bak-<timestamp>`) |

**退出码 / Exit codes:**

| Code | Meaning |
|---|---|
| 0 | Success (or already correctly installed via matching symlink) |
| 1 | Target already exists, use `-Force` to replace |
| 2 | Symlink creation failed (needs Developer Mode or admin rights; retry with `-Mode copy`) |
| 3 | Invalid source (`SmithUE.uplugin` not found under `-Source`) |
| 4 | Post-install verification failed (`SmithUE.uplugin` missing at target) |

安装成功后，先运行 `npm i -g "https://github.com/s2272756972-prog/smithue-cli/archive/refs/heads/ue5.1-ue5.5-compat.tar.gz"` 安装兼容 CLI，再打开项目。编辑器加载完成后用 `smithue-cli status` 验证连接。
After installation, run `npm i -g "https://github.com/s2272756972-prog/smithue-cli/archive/refs/heads/ue5.1-ue5.5-compat.tar.gz"` for the compatibility CLI, then open the project. Once the editor loads, verify with `smithue-cli status`.

---

## 故障排查 / Troubleshooting

### Portfile Discovery（端口文件发现，`%LOCALAPPDATA%\.smithue`）

SmithUE 在启动时写入发现文件 `%LOCALAPPDATA%\.smithue\{pid}.port`，内含端口、进程 PID、项目路径和插件版本。CLI 通过读取这些文件找到正在运行的编辑器。如果 CLI 找不到它：
SmithUE writes a discovery file (`%LOCALAPPDATA%\.smithue\{pid}.port`) when it starts, containing the port, process PID, project path, and plugin version. The CLI reads these files to locate a running editor. If the CLI cannot find it:

- 确认编辑器已完全加载（状态栏出现 🟢 绿色圆点）/ Check the editor is fully loaded (green dot in status bar)
- 检查 `%LOCALAPPDATA%\.smithue\` 目录下是否有 `*.port` 文件 / Check `%LOCALAPPDATA%\.smithue\` for `*.port` files
- 多实例并存时，用 `--pid` 或 `--project` 选择目标实例（也可设置 `SMITHUE_PID` 环境变量）/ With multiple editors running, select one via `--pid` or `--project` (or the `SMITHUE_PID` env var)

#### Portfile Self-Healing（端口文件自愈，v0.9+）

**问题（v0.9 之前）/ Problem (before v0.9):** 端口文件一旦被删除（杀毒软件、CLI 缺陷或手动清理），编辑器在重启前将永久无法被发现。
If this file was deleted (by antivirus, a CLI bug, or manual cleanup), the editor became permanently undiscoverable until restart.

**修复（v0.9+）/ Fix (v0.9+):** 插件以 4 秒为周期的 FTSTicker 心跳检查端口文件，文件消失即自动重写。编辑器正常关闭时端口文件会被自动删除。
A 4-second FTSTicker heartbeat re-writes the portfile if it disappears. The portfile is automatically removed on clean editor shutdown.

如果 CLI 仍然找不到编辑器 / If the CLI still can't find the editor:

1. 检查 `%LOCALAPPDATA%\.smithue\` 下是否有 `*.port` 文件 / Check `%LOCALAPPDATA%\.smithue\` for `*.port` files
2. 目录为空但编辑器在运行：等待 5 秒，心跳会重建文件 / If empty but the editor is running: wait 5 seconds (the heartbeat will recreate it)
3. 或设置 `SMITHUE_PORT=<port>` 环境变量跳过发现流程 / Or set the `SMITHUE_PORT=<port>` env var to skip discovery entirely

### Connection Errors（连接错误）

| CLI Error Message | Cause | Action |
|---|---|---|
| `SmithUE plugin timed out. Command: <command> (port: <port>)` | 编辑器正在执行长耗时命令（如加载大型 Blueprint）/ Editor busy with a long command (e.g., loading a large Blueprint) | 重试即可，端口文件**不会**被删除 / Retry; the portfile is **NOT** deleted |
| `SmithUE plugin unreachable at <host>:<port>. Start UE Editor with SmithUE plugin enabled.` | 指定端口上没有编辑器在监听（连接被拒绝）/ Nothing listening on that port (connection refused) | 启动编辑器并确认 SmithUE 插件已启用 / Start the editor and confirm the SmithUE plugin is enabled |
| `SmithUE instance on port <port> is unreachable, but process <pid> is still running.` | 网络问题或端口已变化，但编辑器进程仍存活 / Network issue or port changed, while the editor process is alive | 重试或重启编辑器；检查编辑器状态 / Try again or restart the editor; check editor status |
| `SmithUE instance on port <port> is not responding and process <pid> is dead. Stale portfile removed.` | 编辑器已崩溃或被强制结束 / Editor crashed or was force-killed | 重启编辑器（失效的端口文件已被自动清理）/ Restart the editor (the stale portfile was cleaned up automatically) |
| `No SmithUE portfiles found. Is the SmithUE plugin running in Unreal Editor?` | 插件未加载、编辑器未启动，或 v0.9+ 心跳尚未触发 / Plugin not loaded, editor not started, or the v0.9+ heartbeat hasn't fired yet | 检查编辑器与插件状态，等待 5 秒后重试 / Check editor + plugin; wait 5 seconds and retry |
| `Multiple SmithUE instances are running. Use --pid or --project to select one` | 多个编辑器同时运行 / Several editors are running at once | 按提示用 `--pid` 或 `--project` 指定实例 / Pick one with `--pid` or `--project` as listed in the message |

发现阶段的存活探测（`/ready`）超时会被视为"忙碌但存活"，不报错也不删除端口文件。探测超时由 `SMITHUE_PROBE_TIMEOUT` 环境变量控制（默认 10000ms）。
During discovery, a liveness probe (`/ready`) timeout is treated as "busy but alive": no error, and the portfile is kept. The probe timeout is controlled by the `SMITHUE_PROBE_TIMEOUT` env var (default: 10000ms).

### PIE Locked（PIE 运行期间写命令被锁定）

PIE（Play In Editor）运行时，SmithUE 会阻止非只读命令并返回 HTTP 503。
When PIE is active, SmithUE blocks non-read commands and returns HTTP 503.

等待 PIE 停止，或仅使用只读命令。状态栏圆点为 🟡 黄色时表示处于此状态。
Wait for PIE to stop, or use read-only commands. The status indicator shows 🟡 yellow in this state.

### Version Compatibility（版本兼容性）

| smithue-cli | SmithUE plugin | Behavior |
|---|---|---|
| v0.9+ | v0.9+ | Full feature set: heartbeat self-healing, non-destructive timeout handling |
| v0.9+ | v0.8.x (old) | Compatible. The old plugin responds to `/ready` normally; no heartbeat needed |
| v0.8.x (old) | v0.9+ | Works, but the old CLI may still delete the portfile on timeout (upgrade recommended; the v0.9+ heartbeat will recreate it within 4 seconds) |

**New in v0.9+:** `SMITHUE_PROBE_TIMEOUT` env var controls the discovery probe timeout (default: 10000ms).

---

## 参考 / References

- [README.md](../README.md) — 快速入门、连接状态指示器 / Quick start, connection status indicator
- [TOOLS.md](../TOOLS.md) — 完整命令参考 / Full command reference
- [CONTRIBUTING.md](../CONTRIBUTING.md) — 添加新命令 / Adding new commands

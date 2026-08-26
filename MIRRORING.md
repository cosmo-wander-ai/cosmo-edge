# Repository Mirroring / 仓库镜像说明

CosmoEdge automatically mirrors its code to [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) for reliable access in mainland China. The mirrored code is managed from GitHub and must not be edited directly on Gitee; Gitee Releases and Issues are maintained as mainland-facing entry points.

CosmoEdge 在 [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) 自动同步代码，方便国内用户稳定访问。镜像代码由 GitHub 主仓管理，不在 Gitee 直接修改；Gitee Releases 与 Issues 作为国内版本获取和中文问题反馈入口持续维护。

---

## Primary & Mirror / 主从关系

| | GitHub (Primary / 主仓) | Gitee (Mirror / 镜像) |
|---|---|---|
| URL | [github.com/cosmo-wander-ai/cosmo-edge](https://github.com/cosmo-wander-ai/cosmo-edge) | [gitee.com/cosmo-wander-ai/cosmo-edge](https://gitee.com/cosmo-wander-ai/cosmo-edge) |
| Role | Source of Truth and code review (主仓与代码评审) | Auto-synced code access plus mainland Releases and Issues (自动同步代码、国内版本与问题入口) |
| Sync | — | Auto-sync via GitHub Actions (自动同步) |

## Issues & Pull Requests / 问题与贡献

| Action | Where / 位置 |
|--------|-------------|
| General reproducible defects (通用可复现缺陷) | [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues) |
| Mainland access, installation, usage, release, or device questions (国内访问、安装、使用、版本或设备问题) | [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) |
| Feature requests (功能建议) | [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues) 或 [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) |
| Pull Requests (代码贡献) | [GitHub](https://github.com/cosmo-wander-ai/cosmo-edge/pulls) only |
| Discussions (讨论) | [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions) |
| Releases (版本获取) | [GitHub Releases](https://github.com/cosmo-wander-ai/cosmo-edge/releases) 或 [Gitee Releases](https://gitee.com/cosmo-wander-ai/cosmo-edge/releases) |

> **Note / 说明**: Gitee Issues and Releases are maintained for mainland users, but pull requests should be submitted to GitHub. Code merged on GitHub is automatically mirrored to Gitee, so direct code edits on Gitee can be overwritten.
>
> Gitee Issues 与 Releases 面向国内用户持续维护，但代码贡献请提交到 GitHub。合并到 GitHub 的代码会自动同步到 Gitee，因此不要直接修改 Gitee 上的镜像代码。

## Sync Frequency / 同步频率

- Triggered on every push to the `main` branch (每次 `main` 分支 push 后自动触发)
- Manual trigger available via `workflow_dispatch` (支持手动触发)
- Typical sync delay: < 2 minutes (同步延迟通常 < 2 分钟)

## README Rendering / README 展示

- GitHub continues to render `README.md` and `README.zh-CN.md`, including its
  native attachment-video players.
- Gitee prioritizes `Readme.osc.md`. That generated file keeps the Chinese
  README content but replaces attachment videos with poster images linked to
  stable playback pages on `www.cosmowander.ai`.
- `Readme.osc.md` is generated from `README.zh-CN.md`; do not edit it directly.
  Run `npm run gitee:readme:generate` after changing the Chinese README, and
  `npm run gitee:readme:check` to verify that it is current.

- GitHub 继续展示 `README.md` 与 `README.zh-CN.md`，保留原生附件视频播放器。
- Gitee 优先展示 `Readme.osc.md`。该生成文件保留中文 README 内容，只把附件视频替换为跳转
  `www.cosmowander.ai` 稳定播放页的封面图。
- `Readme.osc.md` 由 `README.zh-CN.md` 生成，请勿直接编辑。中文 README 更新后运行
  `npm run gitee:readme:generate`，并使用 `npm run gitee:readme:check` 验证一致性。

## For Mainland China Users / 国内用户

If you experience slow access to GitHub, use the Gitee mirror:

如果 GitHub 访问较慢，可使用 Gitee 镜像：

```bash
git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
```

For installation, usage, release-download, or device-adaptation questions in mainland China, use [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues).

国内用户遇到安装、使用、版本获取或设备适配问题，可使用 [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) 反馈。

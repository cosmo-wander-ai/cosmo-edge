---
title: 仓库元数据
description: GitHub 仓库 description、topics、homepage、about、issue 模板入口和发布配置建议。
prev:
  text: 发布说明
  link: /project/release-notes
next:
  text: 开源发布清单
  link: /project/open-source-checklist
---

# 仓库元数据

本文用于公开发布时配置 GitHub 仓库页面。维护者可以直接复制以下内容到 GitHub repository settings。

## Repository Name

```text
cosmo-edge
```

## Owner

```text
cosmo-wander-ai
```

## Description

建议：

```text
C++ native industrial edge AI engine with visual pipeline orchestration, on-device VLM support, and real-time OSD for video analytics.
```

如果希望更保守、贴近当前已确认构建路径：

```text
C++ native edge AI box runtime with web-based scenario configuration, model management, video analytics, streaming, MQTT/HTTP integration, and Sophon/x86 build paths.
```

## Homepage

GitHub Pages 启用后建议：

```text
https://cosmo-wander-ai.github.io/cosmo-edge/
```

如果暂未启用 Pages，可先留空或指向 README。

## Topics

建议 topics：

```text
cpp
c-plus-plus
computer-vision
video-analytics
edge-ai
edge-computing
object-detection
video-processing
rtsp
webrtc
mqtt
websocket
inference
visual-programming
workflow-orchestration
industrial-ai
vision-language-model
vlm
sophon
bm1688
onnxruntime
vue
vite
real-time
```

## Social Preview

建议后续补充：

```text
docs/assets/social-preview.png
```

当前仓库已经包含 README 使用的 GIF 素材，但 social preview 更适合使用静态 PNG。

## Branches

建议：

| 分支 | 用途 |
| --- | --- |
| `main` | 公开稳定分支 |
| `dev` | 开发集成分支 |

正式公开前应确认默认分支。

## GitHub Pages

当前 workflow：

```text
.github/workflows/docs.yml
```

构建命令：

```bash
npm ci
npm run docs:build
```

发布目录：

```text
docs/.vitepress/dist
```

VitePress `base` 当前为：

```text
/cosmo-edge/
```

这与 GitHub Pages 项目站点路径匹配。

## Issue Templates

当前包含：

- Bug report
- Feature request
- Documentation issue

Issue template config 位于：

```text
.github/ISSUE_TEMPLATE/config.yml
```

## Pull Request Template

当前 PR 模板：

```text
.github/pull_request_template.md
```

## Release Settings

首个公开 release 建议：

| 字段 | 建议值 |
| --- | --- |
| Tag | `v0.1.0` |
| Target | 公开默认分支 |
| Title | `CosmoEdge v0.1.0` |
| Prerelease | 是，除非维护者确认生产发布 |
| Notes | 参考 `CHANGELOG.md` 和 `docs/project/release-notes.md` |

## Before Publishing

- [ ] 确认默认分支。
- [ ] 启用 Issues。
- [ ] 启用 Discussions，或从 README 移除 Discussions 链接。
- [ ] 启用 GitHub Pages。
- [ ] 配置 repository description。
- [ ] 配置 topics。
- [ ] 配置 homepage。
- [ ] 确认 social preview。
- [ ] 确认 release tag 和 release notes。

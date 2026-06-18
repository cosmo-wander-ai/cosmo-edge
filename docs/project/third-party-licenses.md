---
title: 第三方依赖与许可证
description: 当前仓库第三方依赖来源、许可证审计底稿、发布风险和 NOTICE 补充事项。
prev:
  text: 安全说明
  link: /project/security
next:
  text: 发布说明
  link: /project/release-notes
---

# 第三方依赖与许可证

本文是开源发布前的第三方依赖和许可证审计底稿，不是法律意见。正式发布前，维护者需要逐项确认源码、二进制、模型、资源、Docker 镜像和 npm 依赖是否满足最终发布策略。

项目整体许可证当前按 Apache License 2.0 准备。第三方组件仍适用各自许可证。

## 依赖来源

当前仓库中的第三方依赖主要来自：

- `3rd/` 下的 vendored 源码、预置源码目录或二进制包。
- `cmake/*.cmake` 中的第三方构建脚本。
- `src/web/package.json` 中的前端 npm 依赖。
- 顶层 `package.json` 中的 VitePress 文档站依赖。
- Dockerfile 中安装的系统包和构建工具。
- `prebuild/` 下的预编译二进制。

## C/C++ 与运行时依赖审计表

| 目录 | 已发现许可证文件 | 初步判断 | 发布风险 | 发布前动作 |
| --- | --- | --- | --- | --- |
| `fmt-7.1.2` | `LICENSE.rst` | MIT | 低 | 保留许可证文本 |
| `glog` | `COPYING` | BSD-style | 低 | 保留许可证文本 |
| `zlib-1.3.1` | `LICENSE` | zlib | 低 | 保留许可证文本 |
| `SQLiteCpp-3.3.3` | `LICENSE.txt` | MIT-style / SQLite wrapper terms | 低 | 保留许可证文本 |
| `libuuid-1.0.3` | `COPYING` | 需确认 | 中 | 确认具体许可证和 NOTICE 要求 |
| `uSockets` | `LICENSE` | 需确认 | 中 | 确认具体许可证 |
| `uWebSockets` | `LICENSE`、`src/f2/LICENSE.txt` | Apache-2.0 / 需确认子组件 | 中 | 确认子组件许可证 |
| `mp4v2-2.0.0` | `COPYING` | MPL-1.1 相关 | 中 | 确认是否随源码/二进制发布及义务 |
| `openssl-3.5.3` | `LICENSE.txt` | Apache-2.0 | 低 | 保留许可证文本和 attribution |
| `curl-8.17.0` | `COPYING` | curl license | 低 | 保留许可证文本 |
| `paho.mqtt.c-1.3.15` | `LICENSE`、`NOTICE` | EPL-2.0 OR BSD-3-Clause | 中 | 选择兼容路径，保留 NOTICE |
| `libevent-2.1.12-stable` | `LICENSE` | BSD-style | 低 | 保留许可证文本 |
| `ffmpeg-4.4.6` | `LICENSE.md`、`COPYING.*` | LGPL v2.1+，启用 GPL 组件时变为 GPL | 高 | 确认 public build 不启用 GPL codec，记录配置 |
| `cryptopp-*` | `License.txt` / `LICENSE` | Boost Software License | 低 | 保留许可证文本 |
| `tokenizers-cpp` | `LICENSE` | 需确认 | 中 | 审计 Rust/C++ 子依赖和模型 tokenizer 资源 |
| `openh264` | `LICENSE` | BSD-style + codec/patent notice 需确认 | 中 | 确认二进制分发和专利/NOTICE 要求 |
| `libsophon-0.4.11` | 未在本次确认 | 供应商 SDK | 高 | 确认 SDK redistributable 条款 |
| `onnxruntime-linux-x64-1.26.0` | `LICENSE` | MIT | 低 | 保留许可证文本 |
| `pcap` | 未在本次确认 | 需确认 | 中 | 确认来源、许可证和是否必须发布 |
| `srs-6.0-r0` | `LICENSE`，内部 `3rdparty` 多许可证 | MIT + bundled dependencies | 高 | 审计 SRS 自带三方依赖，特别是 MPL/GPL 选项 |
| `nginx-release-1.28.1` | `LICENSE` | BSD-like nginx license | 低 | 保留许可证文本 |
| `nginx-http-flv-module-1.2.12` | `LICENSE` | 需确认 | 中 | 确认模块许可证 |
| `pcre2-pcre2-10.47` | `COPYING` | BSD-style PCRE2 | 低 | 保留许可证文本 |
| `eigen3-3.2.92` | 未在本次确认 | MPL2 通常适用，需确认 | 中 | 确认 bundled 文件许可证 |
| `jsoncpp-1.9.5` | `LICENSE` | MIT / public-domain note | 低 | 保留许可证文本 |
| `searchtool` | `README.md` | 本地工具依赖，需确认 | 高 | 确认来源和是否应从公开仓库移除 |
| `include` | 共享 vendored headers | 混合/未知 | 高 | 逐文件确认来源和许可证 |

## Codec 与 GPL 风险

顶层 CMake 包含：

```text
COSMO_ENABLE_GPL_CODECS
```

当前默认值为 `OFF`，且 CPU 构建脚本显式关闭 GPL codec：

```text
COSMO_ENABLE_GPL_CODECS=OFF
```

发布要求：

- 公开 release build 必须明确记录该选项。
- 如果启用 x264 或 FFmpeg GPL 组件，分发义务可能变为 GPL 相关义务。
- README、构建指南和 release notes 中不得把 GPL codec build 写成默认公开路径。

## SRS 额外审计

`3rd/srs-6.0-r0` 自身包含 `trunk/3rdparty`。本次扫描发现其中存在多种许可证和 dual-license 组件，例如 state-threads 的 MPL/GPL 选项。

发布前需要单独确认：

- SRS 是否以源码形式随仓库发布。
- SRS 是否被编译进发布包，还是仅作为独立进程/工具使用。
- SRS bundled third-party 是否需要单独 NOTICE。
- 是否可以改为下载依赖而不是 vendored 源码。

## Prebuilt Binary

当前 `prebuild/` 中可见：

```text
libcosmo_model_guard.so
```

发布前必须确认：

- 是否允许公开分发。
- 许可证和归属。
- 是否需要源代码或额外授权。
- 加密模型能力是否作为开源功能发布。
- 如果不能公开，是否从 public repo 移除并在文档中说明可选闭源扩展。

## 模型与资源

以下资源必须由维护者确认发布边界：

```text
data/resource/aiboxresource
data/resource/aiboxresource_x86
```

重点检查：

- 是否包含模型权重。
- 是否包含客户场景、图片、视频、区域配置或内部名称。
- 示例模型和算法模板是否有可公开许可证。
- GroundingDINO、VLM、YOLO 等模型来源是否允许再分发。
- 商业设备包中的模型是否应与开源仓库分离。

## Frontend npm 依赖

`src/web/package.json` 包含：

- Vue 3
- Vite
- Vue Router
- Element Plus
- Axios
- ECharts
- Vue I18n
- Vue Flow packages
- lodash
- moment
- flv.js
- uuid
- js-md5
- dagre
- sass / sass-embedded

发布前建议执行：

```bash
cd src/web
npm ci
npm audit
npm run build
```

并使用许可证扫描工具导出 dependency license report。当前文档只记录依赖入口，不替代 npm license audit。

## 文档站依赖

顶层 `package.json` 当前使用：

```text
vitepress
```

`npm ci` 当前会报告 npm audit vulnerabilities。发布前应决定：

- 是否升级依赖。
- 是否记录为 docs-only dependency risk。
- GitHub Pages workflow 是否固定 Node 版本。

## Docker 与系统包

Dockerfile 和构建脚本可能引入：

- 编译器、CMake、Node、Python、Rust。
- 系统 FFmpeg/OpenH264/OpenSSL 相关包。
- Sophon SDK 或镜像依赖。
- 国内镜像源或内部下载源。

发布前需要确认所有 Dockerfile 中的源地址、镜像 tag 和安装脚本不包含内部地址、账号或私有缓存。

## NOTICE 补充策略

顶层 `NOTICE` 当前是项目级框架。正式发布前建议补充：

1. Apache-2.0 或要求 NOTICE 保留的组件。
2. Paho MQTT 的 `NOTICE` 内容摘要和路径。
3. OpenSSL attribution。
4. SRS 及其 bundled dependencies 的 required notices。
5. OpenH264 / codec 相关 notice。
6. 任何供应商 SDK 要求的声明。

## 发布前动作清单

- [x] 选择项目许可证：Apache License 2.0。
- [x] 添加顶层 `LICENSE`。
- [x] 添加顶层 `NOTICE`。
- [x] 建立第三方依赖审计底稿。
- [ ] 扩展 `NOTICE` 中 required third-party attribution。
- [ ] 审计每个 `3rd/` 依赖许可证。
- [ ] 审计 SRS bundled third-party 许可证。
- [ ] 审计 frontend npm dependency licenses。
- [ ] 审计 VitePress dependency licenses。
- [ ] 确认 public build 中 `COSMO_ENABLE_GPL_CODECS=OFF`。
- [ ] 决定 `prebuild/libcosmo_model_guard.so` 是否可公开。
- [ ] 确认模型和资源再分发策略。
- [ ] 移除或替换不符合发布策略的依赖。

## English

This page is a release-readiness working paper for third-party dependency and license review. It is not legal advice.

The project-level license is being prepared as Apache License 2.0. Third-party components remain governed by their own licenses. Before the public release, maintainers should complete license audits for vendored source code, prebuilt binaries, npm dependencies, Docker-installed packages, model files, and runtime resources.

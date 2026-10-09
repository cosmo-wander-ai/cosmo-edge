<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**把视频 AI 模型变成可部署的边缘应用——面向 Sophon、Rockchip 与 x86 的 C++ 边缘 AI 引擎。**

使用一致的可视化编排和设备管理体验构建视频分析、VLM 与事件工作流；不同平台使用各自的运行时、构建产物和模型包。

[![Nightly Sophon Build and Test](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml)
[![Rockchip Cross Build](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/ci-build-rockchip.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/ci-build-rockchip.yml)

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#核心能力)
[![Release](https://img.shields.io/badge/release-v1.1.0-2ea44f?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.1.0)

[![Website](https://img.shields.io/badge/website-cosmowander.ai-3B82F6?style=flat-square)](https://www.cosmowander.ai/)
[![Docs](https://img.shields.io/badge/docs-online-2563EB?style=flat-square)](https://www.cosmowander.ai/zh/docs/)
[![Gitee](https://img.shields.io/badge/Gitee-cosmo--edge-C71D23?style=flat-square&logo=gitee)](https://gitee.com/cosmo-wander-ai/cosmo-edge)

[使用路径](#选择你的使用路径) · [近期进展](#版本与近期进展) · [快速开始](#快速开始) · [平台选择](#选择平台) · [验证](#验证与性能) · [文档](#文档设备与社区) · [English](README.md)

</div>

---

<div align="center">

<https://github.com/user-attachments/assets/96eeba7e-5b00-4c54-97b3-3ee4571cd5a0>

</div>

CosmoEdge 不只是模型推理服务：它提供从模型导入、可视化编排到告警与事件推送的完整应用层。仓库中的核心引擎与控制台以 Apache-2.0 开源；认证硬件、商业预置模型与 Model Guard 分发保护具有独立边界。

## 选择你的使用路径

| 你要完成的事 | 从哪里开始 | 工程与版本 |
| --- | --- | --- |
| 部署视频 AI 应用，配置模型、场景与告警 | [快速开始](#快速开始) · [选择平台](#选择平台) | 本仓库的引擎与控制台；正式版 **v1.1.0** |
| 从数据开始定制目标检测模型 | [CosmoEdge 辅助训练 Skill](https://github.com/cosmo-wander-ai/cosmoedge-training-skill) | 独立工程；已发布 **v0.1.0-preview.1**，后续修订见其 `main` |
| 让 AI 助手查询告警、按需看图并确认后启停检测 | [CosmoEdge Connect](https://github.com/cosmo-wander-ai/cosmoedge-connect) | 独立的本地 MCP / WorkBuddy 接入工程；**v0.1.0-alpha.1 源码预览** |

训练 Skill 与 Connect 分别安装和发布。使用条件、验证范围和各自的版本入口见[配套工程](docs/guide/companion-projects.md)。

## 版本与近期进展

**正式发布：v1.1.0。** 面向 BM1688、CV186X、RK3576、RV1126B 与 x86，包含 Apple Silicon macOS Docker Preview；多平台性能与受控 72 小时固定配置结果见 [v1.1 报告](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)。安装包见 [GitHub Release](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.1.0)。

**当前 `main`：v1.1.0 之后的开发进展。** 已合入的主要更新包括：

- **视频接入**：[ONVIF 发现与认证接入](docs/guide/onvif-access.md)、[GB28181 设备管理及 TCP/UDP 信令与视频接入](docs/guide/gb28181-access.md)。
- **模型与视觉流程**：YOLO26 与旋转框 OBB 处理；BM1688 原生 Laya-V 模型接入、告警复核及图片/视频的多 ROI 问题配置。实现与已完成验证见 [PR #219](https://github.com/cosmo-wander-ai/cosmo-edge/pull/219)。
- **平台集成**：[平台管理接口](docs/reference/management.md)支持版本化资源下发、断点续传、任务启停与状态回读。
- **部署与工程质量**：RK3576 受保护模型路径、模型包装校验、视频样本级事件测量与 VLM 评估工具，以及任务恢复、媒体处理和日志管理修复。详见 [Unreleased 更新记录](CHANGELOG.md#unreleased)。

上述主干更新未包含在 v1.1.0 安装包中；所需模型、目标平台和验证范围以对应说明为准。v1.1 benchmark 仍只描述其固定候选与测试条件，不能用于证明后续主干或新模型的性能。

## 选择平台

CosmoEdge 提供统一的引擎架构与编排体验，但每次构建只选择一个推理后端，并使用面向目标平台生成的模型产物。下表说明 v1.1.0 发布范围及其对应证据。

| 平台 | 状态 | 运行时 / 模型产物 | 当前范围与证据 |
| --- | --- | --- | --- |
| Sophon BM1688 | v1.1 已支持 / 主力平台；支持 VLM | BMRT / `.nn` | 已验证 VLM 在 0.1 FPS/路下支持 6 路，并包含 [v1.1 工作负载证据](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md) |
| Rockchip RK3576 / RV1126B | v1.1 已支持；RK3576 支持 VLM | RKNN / 目标平台专用 `.rknn`；RK3576 使用 RKLLM | RK3576 已验证 VLM 在 0.1 FPS/路下支持 4 路；RV1126B VLM 不在本次发布声明中。其他平台实测结果仍在 [v1.1 报告](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)中分别列出 |
| Sophon CV186X | v1.1 已支持；支持 VLM | BMRT / 目标芯片专用 `.nn` | 已验证 VLM 在 0.1 FPS/路下支持 6 路，并包含模型导入与设备工作负载证据 [v1.1 benchmark](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md) |
| x86 Linux / Windows；Apple Silicon macOS | Linux / Windows 已支持；macOS Preview | ONNX Runtime / `.onnx` | Mac 通过 amd64 仿真覆盖单路本地视频开发体验，不代表原生性能 |
| Sophon BM1684X | 规划中 | — | 不属于当前发布范围 |

## 快速开始

以下命令克隆并构建当前 `main`。需要正式发布版本时，请使用 [v1.1.0 安装包](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.1.0)，或在构建前切换到 `v1.1.0` 标签并按该版本文档操作。

### 在 x86 本地试用

无需边缘硬件即可体验。x86 模式使用与边缘部署一致的 UI 和工作流，但吞吐低于 NPU 部署。

```bash
# 1. 克隆
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. 在 Linux 启动
sudo docker compose -f docker-compose.x86.yml up -d --build
# Windows：docker compose -f docker-compose.x86.windows.yml up -d --build
# Apple Silicon macOS（Preview）：./scripts/macos-docker-preview.sh up

# 3. 打开 http://localhost:8080
```

启动后，按照[场景配置教程](https://www.cosmowander.ai/zh/docs/tutorials/02-scenario-config/scenario-config)创建第一个 AI 检测任务。Mac 用户先阅读 [macOS Docker Preview](docs/guide/macos-docker-preview.md) 的许可、环境和能力边界。使用 Docker Compose V1 时，可将 `docker compose` 替换为 `docker-compose`。

### 为 Sophon 构建

```bash
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
# BM1688（省略芯片型号时的默认值）
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip bm1688

# CV186X
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

包装脚本会自动选择当前环境可用的 Compose 实现，并仅在 Docker 权限不足时请求提权。
构建脚本会根据芯片型号选择对应模型资源，无需填写模型路径；不同目标分别导出到
`build_output/public-runtime/<chip>/`，目录内同时包含 `TARGET_CHIP` 和 `SHA256SUMS`。

默认 Open 包包含明文模型，不需要设备授权。部署前必须核对芯片标记和校验和。
[构建指南](https://www.cosmowander.ai/zh/docs/guide/build)是构建事实的权威入口；SSH 安装、
页面升级、恢复与重启后验收统一参阅[部署指南](https://www.cosmowander.ai/zh/docs/guide/deployment)。

### 为 Rockchip 构建

```bash
./scripts/docker-compose.sh -f docker-compose.rockchip.yml pull cosmo-rockchip-package

# RK3576（使用仓库内已有的 RK3576 模型资源）
COSMO_TARGET_CHIP=rk3576 ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
ls -lh build_output/rk3576/

# RV1126B（需先准备该芯片的模型 overlay）
COSMO_TARGET_CHIP=rv1126b ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
ls -lh build_output/rv1126b/
```

一个固定 digest 的 Rockchip 构建镜像共享编译器与 RKNN SDK，并按芯片选择相互隔离的
MPP/RGA 配置；RKLLM v1.3.0 仅在 RK3576 包中强制包含。目标芯片标记、媒体运行时配置和
校验和统一输出到 `build_output/<chip>/`。模型和设备验证边界参阅
[构建指南](docs/guide/build.md#rockchip-构建产物)与
[RK3576 集成指南](docs/guide/rk3576-rknn-development.md)。

CV186X 请按照 [CV186X 快速开始](docs/guide/cv186x-quick-start.md)完成安装、模型导入、首个事件验证，以及升级和恢复检查。

## 你可以构建什么

- **实时视频分析**：检测、分类、跟踪、区域规则、计数、OSD 和告警截图。
- **提示词驱动视觉**：让 VLM 状态判断和 GroundingDINO 开放词汇检测与传统 CV 流水线协同工作。
- **可视化应用工作流**：在浏览器中连接模型、规则、事件和输出动作。
- **边缘系统集成**：管理场景任务，并通过 REST、WebSocket、MQTT 或 HTTP webhook 输出结构化事件。

## 核心能力

| 能力 | 能力范围 | 深入了解 |
| --- | --- | --- |
| 原生运行时 | 面向多路媒体、推理调度、OSD、任务和事件的 C++17 引擎 | [架构](https://www.cosmowander.ai/zh/docs/guide/architecture) |
| 可视化编排 | 浏览器端流水线组合、任务绑定、参数校验和实时反馈 | [流水线教程](https://www.cosmowander.ai/zh/docs/tutorials/04-pipeline-orchestration/pipeline-orchestration) |
| 推理与媒体 | Sophon、RKNN 和 x86 平台后端；平台专用构建与模型产物 | [构建指南](https://www.cosmowander.ai/zh/docs/guide/build) |
| VLM 与 DINO | 提示词视觉判断、开放词汇检测，以及检测告警上报前可选的 VLM 复核 | [VLM 指南](https://www.cosmowander.ai/zh/docs/tutorials/03-vlm-guide/vlm-guide) |
| 运维与集成 | 模型管理、告警、事件历史、REST、WebSocket、MQTT 和 webhook | [API 概览](https://www.cosmowander.ai/zh/docs/reference/api) |
| 模型接入与保护 | 模型转换、导入、验证，以及 Open/Protected 分发边界 | [模型适配指南](https://www.cosmowander.ai/zh/docs/tutorials/05-model-porting/model-porting) |

<details>
<summary>▶ 观看演示：可视化编排一条完整 pipeline</summary>

<https://github.com/user-attachments/assets/c9673081-ad73-4455-9486-1a3021358cdd>

</details>

<details>
<summary>▶ 观看演示：GroundingDINO 与 VLM 视觉工作流</summary>

<https://github.com/user-attachments/assets/f47b541e-0d01-437d-86e1-4183f6e610fd>

</details>

## 智能体辅助二次开发

已经有模型适配、系统集成或界面改造任务？把业务目标、已有物料、目标设备或测试环境和验收要求交给常用编码智能体。仓库提供任务入口、示例、检查和证据边界，使结果可以包含可导入产物、范围明确的代码改动和可核验结论。

从[智能体辅助二次开发](docs/development/agent-assisted-development.md)开始，再根据具体任务进入[模型适配指南](https://www.cosmowander.ai/zh/docs/tutorials/05-model-porting/model-porting)或[贡献者指南](docs/development/contributing.md)。

## 验证与性能

### CosmoEdge 1.1 多平台性能报告

v1.1 报告覆盖 BM1688、CV186X、RK3576 与 RV1126B。报告包含人员检测、未佩戴安全帽分析和 5 FPS 并发混合任务的 49 份独立小模型用例、受控 72 小时双 CV 固定配置，以及 BM1688、CV186X、RK3576 的已验证 VLM 性能。

- [中文 benchmark 索引](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)
- [English benchmark index](docs/benchmarks/scenario-bench/v1.1/README.md)
- [中文主报告（官网渲染版）](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/report.zh-CN.html)
- [72 小时双 CV 报告（官网渲染版）](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/dual-cv-72h/report.zh-CN.html)
- [方法与复现](docs/benchmarks/scenario-bench/v1.1/methodology.md)

<details>
<summary>展开 v1.1 固定候选、性能表与测试条件</summary>

短时容量刷新使用源码 commit `89c73a7464a81ef378686447d7c1eeb88b988455`、tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`，采用固定 1080p24 视频、30 秒单级时长和 [release manifest](docs/benchmarks/scenario-bench/v1.1/release-manifest.json) 中记录的门禁。72 小时观测使用源码 commit `44209759f450e96cda265acfa8bc6d17a1138888`、tree `5cbdefeaefaf642407356c22c271ccc7d57935b0`，并复用同一受控输入。

| 平台 | 72 小时固定路数 | 任务绑定 | 样本覆盖 | 最低 / 平均 FPS | 结果 |
| --- | ---: | ---: | ---: | --- | --- |
| BM1688 | 8 | 16 | 4316 / 4320 | 4.68 / 5.086 | PASS |
| CV186X | 8 | 16 | 4316 / 4320 | 4.54 / 5.085 | PASS |
| RK3576 | 8 | 16 | 4316 / 4320 | 5.00 / 5.098 | PASS |
| RV1126B | 4 | 8 | 4316 / 4320 | 4.85 / 5.230 | PASS |

这些结果描述受控本地循环输入下的已测试固定路数，不代表最大容量或 RTSP 网络韧性。执行细节见[测试方法](docs/benchmarks/scenario-bench/v1.1/methodology.md)。

每路并发运行两个业务任务、三个模型阶段：人员检测包含一个检测阶段，未佩戴安全帽分析包含检测与分类两个阶段。

| 平台 | 每路任务组成 | 模型阶段/路 | 目标 FPS/任务 | 通过路数 | 业务任务绑定 |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥16 | 32/32 |
| CV186X | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥8 | 16/16 |
| RK3576 | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥8 | 16/16 |
| RV1126B | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥4 | 8/8 |

三个 VLM 平台统一采用最终协议：每新增一路先完成 task-local readiness，再进入 60 秒正式测量窗口；所有活动路都必须通过每路 0.1 FPS 目标的 80% 实际执行门禁。每个平台的固定候选包还通过了模型加载、任务创建、有效推理、事件/告警输出和服务重启后任务恢复；该结论不延伸到后来重新构建的安装包。

| 平台 | 目标 FPS/路 | 最后通过路数 | 首个失败 |
| --- | ---: | ---: | --- |
| BM1688 | 0.1 | 6 路 | 7 路 FPS 达标率 69.5%，低于 80% 门禁 |
| CV186X | 0.1 | 6 路 | 7 路 FPS 达标率 69.2%，低于 80% 门禁 |
| RK3576 | 0.1 | 4 路 | 5 路 FPS 达标率 69.7%，低于 80% 门禁 |
| RV1126B | — | — | 不在本次 VLM 验证范围内 |

容量数值是当前记录条件下的精确短时门禁边界，不是最大容量认证或生产推荐路数；独立的 72 小时表格只验证对应固定配置。完整矩阵见 [v1.1 benchmark](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)；此前公开的数据只保留一个 [v1.0 历史归档](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/)入口。

</details>

## 架构

```text
+------------------------------------------------------------------+
| Web 控制台 | 可视化编排 | REST / WebSocket / MQTT                |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| C++ 引擎核心                                                     |
| 媒体 | 推理 | 任务 | 规则 | 告警 | 事件 | 模型                   |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| 推理与媒体后端接口                                               |
+--------------------+----------------------+----------------------+
| Sophon BMRT/VPU    | RKNN + MPP/RGA       | ONNX Runtime/FFmpeg  |
| BM1688；CV186X     | RK3576               | x86 Linux / Windows  |
+--------------------+----------------------+----------------------+
```

每次构建只选择一个推理后端，模型产物面向目标平台生成；功能、模型覆盖和容量仍具有平台差异。v1.1.0 的 Model Guard Protected 分发说明针对 Sophon 包；当前主干另有 RK3576 受保护模型实现，见 [PR #151](https://github.com/cosmo-wander-ai/cosmo-edge/pull/151)。

## 文档、设备与社区

| 入口 | 适合场景 |
| --- | --- |
| [文档首页](https://www.cosmowander.ai/zh/docs/) | 完整文档索引和学习路径 |
| [快速开始](https://www.cosmowander.ai/zh/docs/tutorials/01-quickstart/quickstart) | 首次启动和场景体验 |
| [场景配置](https://www.cosmowander.ai/zh/docs/tutorials/02-scenario-config/scenario-config) | 构建场景级工作流 |
| [VLM 指南](https://www.cosmowander.ai/zh/docs/tutorials/03-vlm-guide/vlm-guide) | 提示词视觉判断与事件 |
| [模型适配指南](https://www.cosmowander.ai/zh/docs/tutorials/05-model-porting/model-porting) | 导入自有模型 |
| [智能体辅助二次开发](docs/development/agent-assisted-development.md) | 委托二开任务并获得可核验结果 |
| [配套工程](docs/guide/companion-projects.md) | Training Skill、Connect / MCP 的使用条件与独立版本 |
| [更新记录](CHANGELOG.md) | 正式发布历史与主干待发布变化 |
| [构建指南](https://www.cosmowander.ai/zh/docs/guide/build) | x86、Sophon 与 RK3576 构建、打包路径 |
| [API 概览](https://www.cosmowander.ai/zh/docs/reference/api) | REST、WebSocket、MQTT 与 webhook 集成 |

认证设备提供预配置加速、经过验证的商业模型包和专属部署支持，但不解锁另一套软件功能。中国大陆可通过[淘宝购买认证设备](https://item.taobao.com/item.htm?id=1066672051450)；其他地区或项目部署支持请联系 <hello@cosmowander.ai>。

欢迎提交范围明确的 bug 报告、文档改进、场景示例和集成说明。提交 pull request 前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

中国大陆用户遇到代码访问、安装、使用、版本获取或设备适配问题，可提交到 [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues)；[GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions) 是 v1.1 官方、可检索的英文问答与社区支持渠道，通用可复现缺陷可提交到 [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues)。代码变更与 Pull Request 仍统一在 GitHub 处理；安全问题请按 [SECURITY.md](SECURITY.md) 私密报告。本次发布不运营 Discord。

中文实时交流可扫描[本页底部的二维码](#微信开发者交流群)，加入 CosmoEdge 微信开发者交流 3 群；微信群用于开发者交流，群内形成的可复现问题和可复用结论请继续沉淀到 GitHub Discussions 或 Issues。

## FAQ

<details>
<summary><b>没有 Sophon 或 Rockchip 设备可以试用吗？</b></summary>

可以。在 Linux 或 Windows 上使用 x86 开发模式；Apple Silicon Mac 可以使用 Docker Preview 体验单路本地视频下的控制台、流水线、模型管理和集成路径。Mac 路径运行 amd64 仿真且不启用 Model Guard。目标平台的 NPU 加速和容量验证仍需要对应边缘硬件。

</details>

<details>
<summary><b>Open 与 Protected 包的边界是什么？</b></summary>

两者提供相同的应用软件能力，并使用同一种 MD5 升级生命周期。Open 使用明文模型且不需要设备授权；Sophon Protected 包可携带加密商业预置模型和授权工具，需要设备绑定证书。应用升级包本身不签名。

</details>

<details>
<summary><b>可以使用自己训练的模型吗？</b></summary>

可以。从数据开始训练时，可使用独立的 [Training Skill](https://github.com/cosmo-wander-ai/cosmoedge-training-skill)。已有模型则进入模型适配流程，验证张量、预处理、后处理、目标运行时和业务精度约束；模型产物必须面向实际运行的平台生成。

</details>

<details>
<summary><b>CosmoEdge 的生产就绪程度如何？</b></summary>

`v1.1.0` 是 BM1688、CV186X、RK3576、RV1126B 与 x86 的多平台发布线，其中 BM1688、CV186X 与 RK3576 已验证支持 VLM，并包含范围受限的 macOS Docker Preview。关联报告记录了实测工作负载边界、已验证 VLM 性能和受控 72 小时固定配置结果。生产容量仍需结合自有模型、视频流、精度要求和部署条件完成验证。

</details>

## 许可证

CosmoEdge 使用 [Apache License 2.0](LICENSE) 开源许可。Copyright 2026 CosmoEdge Contributors。

---

<div align="center">

An open-source project by Cosmo Wander AI and the CosmoEdge contributors.

Turn video AI models into deployable edge applications.

📦 GitHub 管理代码主线和 Pull Request；[Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) 自动同步代码，并为中国大陆用户提供版本获取与中文问题反馈入口。详见 [MIRRORING.md](MIRRORING.md)。

</div>

## 微信开发者交流群

<p align="center">
  <img src="docs/assets/community/wechat-group-3-20261007.png" width="430" alt="CosmoEdge 微信开发者交流 3 群二维码（2026 年 10 月 14 日前有效）">
</p>

二维码于 **2026 年 10 月 7 日**更新，**7 天内（2026 年 10 月 14 日前）有效**。如二维码失效，请在[微信群公告](https://github.com/cosmo-wander-ai/cosmo-edge/discussions/112)下留言。

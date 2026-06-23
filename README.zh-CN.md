<!--
Repository metadata suggestion:

Description:
Production-grade C++ edge AI engine for video analytics, with visual pipeline orchestration and on-device VLM support.

Topics:
cpp, c-plus-plus, computer-vision, video-analytics, edge-ai, edge-computing,
object-detection, video-processing, rtsp, webrtc, mqtt, inference,
visual-programming, workflow-orchestration, industrial-ai,
vision-language-model, vlm, sophon, bm1688, real-time
-->

<div align="center">

<!-- TODO: 替换为最终 logo 资源。 -->

<!-- <img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge"> -->

# CosmoEdge

**面向视频智能分析的生产级 C++ 边缘 AI 引擎，支持可视化流水线编排与端侧 VLM**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#c-原生运行时)
[![Platform](https://img.shields.io/badge/platform-Sophon%20BM1688%20%2F%20x86%20Linux%20%2F%20Windows-purple?style=flat-square)](#支持平台)
[![Release](https://img.shields.io/badge/release-v0.1.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Stress Test](https://img.shields.io/badge/stress%20test-200%20video%20samples-brightgreen?style=flat-square)](#验证与性能)
[![Pipelines](https://img.shields.io/badge/pipelines-18%2F18%20validated-brightgreen?style=flat-square)](#验证与性能)

[快速开始](#快速开始) | [核心特性](#核心特性) | [验证与性能](#验证与性能) | [文档](#文档) | [硬件](#cosmoedge-ready-设备)

[English](README.md) | 简体中文

</div>

---

> 本文档与英文 README 保持同步更新。如内容存在差异，请以英文版和最新发布说明为准。

<!--
公开发布前待确认：
- 替换所有占位图片路径为真实资源。
- 确认 Quick Start 命令。
- 确认性能数据，并附上可复现测试报告。
- 确认最终公开发布资源和 URL。
- 确认认证硬件页面 URL。
-->

<div align="center">

![CosmoEdge running multiple edge AI pipelines](docs/assets/hero.gif)

*在一台边缘设备上运行多条 AI 流水线、实时 OSD 叠加和事件输出。*

</div>

CosmoEdge 是一个 C++ 原生的边缘 AI 引擎，面向生产级视频智能分析系统。它把模型变成可视化、可管理的边缘应用：导入模型、编排流水线、接入视频源、在浏览器中查看 AI 叠加画面，并通过 MQTT 或 HTTP 推送结构化事件。

CosmoEdge 的运行时使用 C++ 构建，适合多路视频处理、硬件解码、OSD 渲染和低开销边缘部署。Python 仍然非常适合研究、实验和模型工具链；CosmoEdge 更关注集成商在落地阶段需要的长期运行、资源可控、行为可预期的边缘应用。

CosmoEdge 不止提供一个推理 API 或演示脚本。它关注的是下一步：帮助集成商把 AI 模型变成可以部署、监控、调试和维护的完整边缘应用。

## 你可以构建什么

- 多摄像头安全监测，支持实时 OSD 和告警截图。
- 人流统计、越线检测、区域入侵和交通流量统计。
- 基于端侧 VLM 提示词的视觉巡检流程。
- 基于 GroundingDINO 文本提示词的长尾目标检测。
- 包含模型管理、场景任务、告警和数据推送的端到端边缘 AI 系统。

## 核心特性

### C++ 原生运行时

CosmoEdge 以 C++17 运行时为核心，而不是基于 Python 服务循环。这对边缘视频系统很重要，因为解码、推理调度、OSD 渲染、事件生成和流输出都需要在资源受限的设备上持续运行。

- 面向长期运行的多路视频负载，运行时开销更低。
- 直接集成硬件解码、NPU 运行时、内存池和流媒体组件。
- 更适合设备化边缘部署，对 CPU、内存和线程行为有更强的可预期性。
- x86 开发模式和 Sophon 生产部署使用同一套引擎核心。

### 可视化流水线编排

在浏览器中构建视频 AI 工作流。通过可视化流水线编辑器连接视频源、AI 模型、后处理节点、OSD 渲染、告警规则和输出通道。

<div align="center">

![Pipeline editor workflow](docs/assets/pipeline-editor.gif)

</div>

### 完整应用闭环

CosmoEdge 不只是推理运行时，还包含现场运行 AI 视觉系统所需的应用层能力。

```text
模型仓库 -> 场景任务 -> 实时分析 -> 告警管理 -> 数据推送
   |          |          |          |          |
上传/管理   配置流水线  多路 AI +   规则引擎   MQTT / HTTP
ONNX/bmodel 按场景绑定  OSD 叠加    告警截图   webhook
模型版本    摄像头      WebRTC 流   事件日志   集成
```

<details>
<summary><b>完整能力列表</b></summary>

| 模块 | 能力 |
| --- | --- |
| 模型仓库 | 模型上传、元数据管理、版本管理、热替换流程 |
| 场景任务 | 流水线绑定、摄像头绑定、任务调度、场景级配置 |
| 实时分析 | RTSP、视频文件、USB 摄像头、WebRTC 实时预览、HTTP-FLV 兜底 |
| 图片分析 | 批量图片上传、VLM 分析、结构化结果 |
| 告警管理 | 基于规则的告警、严重等级、告警截图、筛选、事件历史 |
| 数据集成 | MQTT 推送、HTTP webhook、结构化 JSON 事件格式 |
| 系统管理 | 仪表盘、设备状态、用户认证、国际化、配置管理 |

</details>

### 实时可视化调试

CosmoEdge 内置面向生产场景的 OSD 系统，同时服务操作人员和开发者：

- 使用业务标签，而不是直接展示原始模型类别名。
- 使用语义化颜色区分正常、预警、违规和不确定状态。
- 支持区域叠加、越线指示、计数器和事件面板。
- 支持调试视图，展示原始检测框、置信度、跟踪 ID 和模型输出。

### 提示词驱动 AI：GroundingDINO + VLM

CosmoEdge 支持端侧提示词驱动的视觉智能。GroundingDINO 和 VLM 属于同一类能力，但解决的问题不同：

| 能力 | 工作方式 | 典型用途 |
| --- | --- | --- |
| GroundingDINO | 文本提示词 -> 开放词表目标检测 | 不针对特定类别训练，也能查找长尾目标 |
| 端侧 VLM | 封闭式问题 -> YES/NO/枚举状态判断 | “柜门是否打开？” -> YES 时触发告警 |
| VLM 图片分析 | 图片上传 -> 结构化视觉检查 | 质量检测、合规检查 |

<div align="center">

![Prompt-driven AI with GroundingDINO and VLM](docs/assets/prompt-driven-ai.gif)

</div>

GroundingDINO 负责找到“是什么、在哪里”。VLM 负责判断某个视觉状态是否成立。二者都可以作为异步流水线节点，与传统 CV 流水线一起使用。

认证设备包可以包含微调后的 0.8B 端侧 VLM 和生产可用的 CV 模型。开源引擎也可以通过同一套工作流运行用户自带模型。

> 注：正式发布前需要确认公开模型名称、参数量、量化格式和实测延迟，再使用 0.8B VLM 的发布级表述。

### 模型来源

**已在 CosmoEdge 中完成端到端验证：**

下列模型已支持完整流水线能力，包含检测、OSD 渲染、跟踪、告警规则和事件输出。

| 类别 | 已验证架构 | 流水线支持 |
| :--- | :--- | :--- |
| 目标检测 | YOLOv5、YOLOv8、YOLOv10、YOLOv11 | 完整流水线 |
| 目标跟踪 | ByteTrack | 完整流水线 |
| 属性分类 | 安全帽、反光背心、工服等分类器 | 完整流水线 |
| 计数与统计 | 越线统计、区域计数、方向流量 | 完整流水线 |
| 开放词表检测 | GroundingDINO | 异步流水线节点 |
| 视觉状态判断 | 端侧 VLM（文本提示词 -> YES/NO/枚举） | 异步流水线节点 |
| 图片分析 | VLM 批量分析 | 独立任务 |

**模型生态兼容性：**

CosmoEdge 使用 ONNX 作为模型交换格式。来自主流 CV 训练框架的模型可以通过文档化的转换路径导入：

- **Ultralytics (YOLO)**：使用 `yolo export format=onnx` 导出，然后通过模型仓库导入，或转换为 bmodel 用于 NPU 部署。
- **Roboflow**：在 Roboflow 上训练，导出 ONNX，再导入 CosmoEdge。
- **自定义模型**：任何兼容 ONNX 的检测或分类模型，都可以通过模型适配指南接入。

**更广泛的 Sophon 模型生态：**

CosmoEdge 运行在 Sophon BM1688 推理栈上。SOPHGO 官方模型仓库中的模型可以通过模型适配指南接入，指南会覆盖后处理适配和流水线节点注册。

-> [SOPHGO Model Zoo (sophon-demo)](https://github.com/sophgo/sophon-demo)
-> [CosmoEdge Model Porting Guide](docs/tutorials/05-model-porting/model-porting.md)

## 快速开始

### 方案 A：x86 开发模式

首次体验不需要边缘硬件。x86 开发模式使用与边缘部署相同的 UI 和工作流，但吞吐低于 Sophon NPU 模式。

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. 启动 x86 模式
# 首选发布目标 (Linux)：
sudo docker compose -f docker-compose.x86.yml up -d --build

# Windows (PowerShell/CMD)：
docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. 打开 Web 控制台
# http://localhost:8080
```

推荐的首次体验路径：

```text
打开浏览器 -> 导入或选择模型 -> 创建场景任务 -> 接入视频 -> 查看 AI 结果
```

### 方案 B：Sophon 边缘设备

该路径用于 NPU 加速部署。

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. 构建 Sophon/aarch64 发布包
sudo bash scripts/build_sophon_package.sh

# 3. 查看导出的发布包
ls -lh build_output/
# 输出的包名格式如：cosmo-V<version>-<hash>.tar.gz

# 4. 将安装包拷贝到 Sophon 边缘设备上（将 <device_ip> 替换为设备的实际 IP，默认是 192.168.100.1）
scp build_output/cosmo-V*.tar.gz root@<device_ip>:/tmp/

# 5. SSH 登录设备，解压并执行 install.sh 安装脚本
ssh root@<device_ip>
cd /tmp
tar -zxvf cosmo-V*.tar.gz
sudo bash scripts/install.sh

# 6. 重启设备以启动服务
sudo reboot
```

在 Windows PowerShell 下构建发布包：

```powershell
.\scripts\build_sophon_package.ps1
```

安装完成并重启设备后：
- **默认 IP**：`192.168.100.1`（需确保您的电脑与设备处于同一网段，例如配置静态 IP 为 `192.168.100.x`）
- **登录地址**：`http://192.168.100.1`
- **默认用户名**：`admin`
- **默认密码**：`admin`（首次登录后建议修改）

该路径用于构建、导出并安装发布包。准备用于生产硬件？认证 CosmoEdge 设备提供预配置 Sophon 加速、生产模型包和部署支持。参见 [CosmoEdge-ready 设备](#cosmoedge-ready-设备)。

## 验证与性能

CosmoEdge 来自商业化代码库，在开源发布前已经完成内部系统验证。

| 范围 | 当前验证状态 |
| --- | --- |
| 视频压力测试 | 使用 200 个视频样本进行连续播放测试，测试期间未观察到内存泄漏或崩溃 |
| CV 流水线验证 | 18/18 条 CV 流水线已与内部行业基线完成精度对齐 |
| 并发 CV 负载 | 已在单台 BM1688 设备上验证 16 路 CV 推理 |
| 回归测试 | 专职 QA 完成多轮系统回归 |
| 试点部署 | 已在脱敏后的教育、智慧园区和工业安全试点场景中验证 |

> 注：正式 v1.0 发布前需要补充公开验证报告，包括测试时长、设备配置、输入分辨率、模型版本和已知限制。

### 性能基准

下列数据来自内部记录中的代表性系统级组合。一个视频通道表示一路已解码输入流；多个场景任务可以共享同一解码流。E2E 延迟表示在所列负载下从帧到 OSD 或从帧到事件的端到端延迟，不是单模型推理耗时。

| 负载 | 视频通道数 | 场景任务数 | FPS 目标 | E2E 延迟 | 硬件 | 说明 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 全流 YOLOv8n 检测 | 16 | 16 | 3/channel | 32-68(ms) | BM1688 | 解码、推理和 OSD 均开启；稳定上限场景 |
| 共享解码密集 CV 任务 | 4 | 20 | 3/channel | 84-141(ms) | BM1688 | 多个场景任务共享解码流，展示任务并发能力 |
| 安全合规流水线 | 16 | 16 | 3/channel | 182-314(ms) | BM1688 | 检测 + 跟踪 + 属性/规则 + 告警，代表性业务流水线 |
| 提示词驱动 AI 流水线 | 8 | 8 | 0.2/channel | 3154-4128(ms) | BM1688 | VLM 异步节点；事件驱动慢路径，不是逐帧同步 OSD |
| x86 开发模式 | 1 | 1 | TODO | TODO | x86 CPU | YOLOv8n 开发与评估负载 |

## 架构

```text
+---------------------------------------------------------------+
| Web Frontend                                                  |
| Pipeline Editor | Management Console | Real-time View          |
+-------------------------------+-------------------------------+
                                | REST / WebSocket / MQTT
                                v
+---------------------------------------------------------------+
| C++ Engine Core                                               |
| Flow Engine | Media Pipeline | Inference | Services            |
| Task/Action | Decode/Encode | CV/VLM/DINO | Alarm/Event/Model  |
+-------------------------------+-------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Hardware Abstraction                                          |
| Sophon BM1688 NPU/VPU/VPP | x86 CPU/OpenCV                    |
+---------------------------------------------------------------+
```

### 技术栈

| 层级 | 技术 |
| --- | --- |
| 引擎 | C++17、CMake、FFmpeg、OpenCV、SQLiteCpp |
| 推理 | Sophon BMRT/SAIL、x86 模式使用 ONNX Runtime |
| 前端 | Vue.js、Vue Flow、Element Plus |
| 流媒体 | SRS 6.0、WebRTC、HTTP-FLV |
| 集成 | REST API、WebSocket、MQTT、HTTP webhook |

## 支持平台

| 平台 | 状态 | 目标用途 |
| --- | :---: | --- |
| Sophon BM1688 | 主力平台 | NPU 加速生产部署 |
| x86 Linux | 已支持 | 开发、评估、集成测试 |
| x86 Windows | 已支持 | 开发和评估 |
| Sophon BM1684X | 规划中 | NPU 加速部署 |

## CosmoEdge-ready 设备

CosmoEdge 是开源项目。仓库提供认证设备包所使用的同一套引擎、Web UI 和工作流。你可以使用自带模型，在 x86 上开发，并部署到兼容的边缘硬件。

认证设备面向希望跳过硬件适配和模型打包工作的团队。它们会额外提供预配置 NPU 加速、生产模型包和专属部署支持。

| 能力 | 开源仓库 | 认证设备包 |
| --- | :---: | :---: |
| C++ 引擎 | 包含 | 包含 |
| 可视化流水线编排器 | 包含 | 包含 |
| Web 管理控制台 | 包含 | 包含 |
| x86 开发模式 | 包含 | 包含 |
| Sophon NPU 运行时支持 | 源码支持，需自备硬件 | 预配置 |
| CV 模型包 | 自带模型 | 预安装 |
| 0.8B 端侧 VLM | 自带或定制模型包 | 预安装 |
| GroundingDINO 包 | 自带或定制模型包 | 预安装 |
| 部署支持 | 社区支持 | 专属支持 |

认证设备提升的是部署就绪度，而不是锁定软件功能。

<!-- TODO: 替换为最终硬件页面 URL。 -->

[获取认证设备](https://cosmoedge.dev/hardware)

## 文档

| 文档 | 读者 | 说明 |
| --- | --- | --- |
| [Quick Start Guide](docs/tutorials/01-quickstart/quickstart.md) | 所有用户 | 几分钟内完成首次体验 |
| [Scenario Configuration](docs/tutorials/02-scenario-config/scenario-config.md) | 集成商 | 构建场景级 AI 工作流 |
| [VLM Guide](docs/tutorials/03-vlm-guide/vlm-guide.md) | 开发者 | 使用提示词完成视觉状态判断 |
| [Pipeline Orchestration](docs/tutorials/04-pipeline-orchestration/pipeline-orchestration.md) | 高级用户 | 可视化编排自定义流水线 |
| [Model Porting Guide](docs/tutorials/05-model-porting/model-porting.md) | 算法工程师 | 接入自有 ONNX 或目标格式模型 |
| [构建指南](docs/guide/build.md) | 开发者 | 当前确认的 x86 Docker 和 Sophon 发布包构建路径 |
| [部署指南](docs/guide/deployment.md) | DevOps | 运行目录、端口、进程、发布包和 systemd |
| [运行配置](docs/guide/configuration.md) | 运维人员 | 环境变量、资源路径、端口、日志和运行默认值 |
| [故障排查](docs/guide/troubleshooting.md) | 运维人员 | 构建、运行、端口、Sophon 镜像和文档站常见问题 |
| [API 概览](docs/reference/api.md) | 开发者 | 当前 REST/WebSocket/MQTT-facing API 类别 |
| [字段级 API 参考](docs/reference/api-fields.md) | 集成商 | 通用响应、事件、HTTP 推送、MQTT 和 IoT 网络字段 |
| [MQTT 接入参考](docs/reference/mqtt.md) | 集成商 | MQTT topic、外层消息、注册、心跳、下发请求和响应 |
| [HTTP Webhook 参考](docs/reference/webhook.md) | 集成商 | 事件推送配置、负载字段和接收端建议 |
| [架构概览](docs/guide/architecture.md) | 贡献者 | 引擎内部结构和扩展点 |
| [前端工程](docs/development/frontend.md) | 前端开发者 | Vue 3 前端结构和脚本 |
| [I18N 术语表](docs/i18n/GLOSSARY.md) | 前端开发者 | UI 术语、默认英文文案和 short label 规则 |
| [I18N Short Scope 规则](docs/i18n/SHORT-SCOPES.md) | 前端开发者 | 受控紧凑文案场景 ID |
| [后端开发](docs/development/backend.md) | C++ 开发者 | 后端模块、CMake 选项和测试 |
| [CI 与质量检查](docs/development/ci.md) | 贡献者 | 文档站、前端、C++ 格式、静态分析和发布检查入口 |
| [Security Policy](SECURITY.md) | 维护者 | 漏洞报告和部署安全说明 |
| [Notice](NOTICE) | 维护者 | 项目 notice 和第三方 attribution 信息 |
| [Changelog](CHANGELOG.md) | 维护者 | 公开变更历史 |

## 路线图

- [X] C++17 边缘推理引擎
- [X] 可视化流水线编排器
- [X] Web 管理控制台
- [X] x86 Linux 和 Windows 开发模式
- [X] VLM 与 GroundingDINO 集成
- [X] 18 条 CV 流水线完成内部验证
- [ ] 公开 x86 一键启动方案
- [ ] 公开可复现基准数据集和报告
- [ ] v1.0 发布打包
- [ ] 社区模型和场景示例
- [ ] GB28181 协议支持

## 贡献

CosmoEdge 正在向 v1.0 持续演进，欢迎围绕以下方向贡献：

- 带日志和复现步骤的 bug 报告。
- 文档修正和教程改进。
- 场景示例和集成说明。
- 先通过 issue 讨论，再提交范围清晰的小型 PR。

提交 pull request 前，请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

## FAQ

<details>
<summary><b>我需要 Sophon 设备才能体验 CosmoEdge 吗？</b></summary>

不需要。你可以在 Linux 或 Windows 上使用 x86 开发模式体验 UI、流水线工作流、模型管理和集成路径。Sophon 硬件主要用于生产级 NPU 吞吐。

</details>

<details>
<summary><b>开源仓库包含模型权重吗？</b></summary>

开源仓库重点提供引擎、UI 和工作流。你可以自带模型，并参考模型适配指南。认证设备包可以包含预安装的生产 CV 模型、微调后的端侧 VLM 和 GroundingDINO。

</details>

<details>
<summary><b>可以使用自己训练的模型吗？</b></summary>

可以。CosmoEdge 围绕模型导入和模型生命周期管理设计。最终公开指南会说明从 ONNX 或目标运行时格式进入模型仓库的推荐路径。

</details>

<details>
<summary><b>CosmoEdge 和推理服务器或 NVR 项目有什么区别？</b></summary>

CosmoEdge 把三层能力组合在一个边缘系统中：C++ 视频 AI 运行时、可视化流水线构建器，以及用于场景、告警、模型生命周期、OSD 和数据推送的 Web 管理控制台。目标不只是把模型跑起来，而是帮助集成商运行完整的边缘 AI 应用。

</details>

<details>
<summary><b>CosmoEdge 是否已经生产可用？</b></summary>

代码库来自面向生产的商业化开发，并通过了内部压力、流水线和回归验证。首次公开发布仍标记为 v0.1.0，公共 API、打包方式和贡献者流程会继续向 v1.0 稳定。

</details>

## 联系方式

- Community: [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions)
- Partnership & Enterprise: hello@cosmowander.ai

## License

CosmoEdge 使用 [Apache License 2.0](LICENSE) 开源许可。

```text
Copyright 2026 CosmoEdge Contributors

Licensed under the Apache License, Version 2.0
```

---

<div align="center">

Built by Cosmos Wanderer AI Technology

Industrial edge intelligence, from model to production.

</div>

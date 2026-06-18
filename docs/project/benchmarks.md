---
title: 性能基准复现说明
description: CosmoEdge 性能基准测试的环境记录、输入准备、指标定义、执行步骤和结果模板。
prev:
  text: 公开验证报告
  link: /project/validation
next:
  text: 开源发布清单
  link: /project/open-source-checklist
---

# 性能基准复现说明

本文用于规范后续公开性能基准测试，避免不同硬件、模型、输入流和运行配置混在一起比较。README 中的性能表应逐步链接到本页记录的可复现报告。

## 必填环境信息

| 项目 | 示例 |
| --- | --- |
| CosmoEdge 版本 | `v0.1.0` / commit hash |
| 构建方式 | x86 Docker / Sophon package / Windows CPU build |
| 设备型号 | BM1688 设备型号或 x86 主机型号 |
| CPU / 内存 | CPU 型号、核心数、内存容量 |
| NPU / VPU | 芯片型号、SDK 版本、驱动版本 |
| OS / 镜像 | 系统版本、基础镜像版本 |
| 模型 | 模型名称、版本、输入尺寸、backend |
| 输入 | 视频数量、分辨率、帧率、编码、时长 |
| 输出 | 是否启用 OSD、推流、截图、录像、MQTT/HTTP 推送 |

## 指标定义

| 指标 | 定义 |
| --- | --- |
| Video channels | 已接入并持续解码的视频输入数量 |
| Scenario tasks | 已启用的场景任务数量 |
| FPS target | 每通道目标处理帧率 |
| E2E latency | 从输入帧到 OSD 或事件输出的端到端延迟 |
| CPU usage | 进程或系统 CPU 使用率，需说明统计口径 |
| Memory usage | 进程 RSS 或系统内存使用量，需说明统计口径 |
| NPU/VPU usage | 硬件工具可观测的推理/解码占用 |
| Error count | 解码失败、推理失败、推流失败、事件推送失败等错误数 |
| Stability | 测试时长内是否出现崩溃、重启、内存持续增长 |

## 推荐测试场景

| 场景 | 目的 |
| --- | --- |
| 单路 x86 开发模式 | 验证普通开发者能跑通 UI、模型、场景任务和事件链路 |
| 4 路共享解码 CV 任务 | 验证多个场景任务共享视频输入时的调度能力 |
| 16 路 CV 推理 | 验证 BM1688 多通道稳定负载 |
| 安全合规流水线 | 验证检测、跟踪、属性、规则、告警、OSD 的组合负载 |
| VLM 异步节点 | 验证慢路径异步任务对主实时链路的影响 |
| 长时间压力测试 | 验证内存、文件、线程、推流和事件输出稳定性 |

## 执行前检查

1. 确认模型和资源许可允许公开测试。
2. 确认输入视频或图片已脱敏。
3. 固定配置文件、模型版本和启动命令。
4. 清空旧日志和旧事件文件。
5. 记录端口、进程和系统资源初始状态。

## x86 Docker 测试入口

```bash
docker compose -f docker-compose.x86.yml up -d --build
docker compose -f docker-compose.x86.yml logs -f
docker compose -f docker-compose.x86.yml down
```

记录：

- Web 控制台是否可访问。
- 场景任务是否正常启动。
- 实时展示、OSD、事件记录是否正常。
- CPU、内存和日志错误。

## Sophon 发布包测试入口

```bash
bash scripts/build_sophon_package.sh
```

Windows PowerShell：

```powershell
.\scripts\build_sophon_package.ps1
```

发布包测试需额外记录：

- Sophon SDK / runtime 版本。
- NPU/VPU 资源占用。
- 解码、推理、OSD、编码和推流是否同时启用。
- 长时间运行是否出现内存增长或服务重启。

## 结果表模板

| Workload | Video channels | Scenario tasks | FPS target | E2E latency | Hardware | Duration | Result | Notes |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- |
| Full-stream YOLOv8n detection | 16 | 16 | 3/channel | TODO | BM1688 | TODO | TODO | Decode + inference + OSD |
| Shared-codec dense CV tasks | 4 | 20 | 3/channel | TODO | BM1688 | TODO | TODO | Multiple tasks share decoded streams |
| Safety compliance pipeline | 16 | 16 | 3/channel | TODO | BM1688 | TODO | TODO | Detection + tracking + attribute/rule + alarm |
| Prompt-driven AI pipeline | 8 | 8 | 0.2/channel | TODO | BM1688 | TODO | TODO | VLM async nodes |
| x86 developer mode | 1 | 1 | TODO | TODO | x86 CPU | TODO | TODO | Development/evaluation workload |

## 报告命名建议

```text
docs/project/reports/
  benchmark-YYYYMMDD-platform-workload.md
```

每份报告应绑定具体 commit 和配置文件，避免后续版本变化后无法复现。

## English

This page defines the benchmark metadata, metric definitions, workload categories, execution entry points, and result table template for reproducible performance reports.

README benchmark numbers should gradually link to concrete reports generated with this template. Do not compare results across different hardware, SDK versions, model versions, input streams, or output settings unless those differences are explicitly documented.

---
title: 公开验证报告
description: CosmoEdge 首次开源发布前可公开披露的验证范围、验证口径、当前状态和待补充证据。
prev:
  text: 仓库元数据
  link: /project/repository-metadata
next:
  text: 性能基准复现说明
  link: /project/benchmarks
---

# 公开验证报告

本文用于承接 README 中的验证声明，并把“已经内部验证”和“尚需公开复现”的边界写清楚。首次开源发布时，如果部分原始测试数据、客户场景或模型资源不能公开，应保留结论但明确标注为内部验证。

## 验证状态总览

| 范围 | 当前状态 | 公开程度 | 后续动作 |
| --- | --- | --- | --- |
| 视频压力测试 | 已完成内部验证 | 可公开测试口径，原始素材需确认 | 补充可公开样例集或脱敏输入 |
| CV 流水线验证 | 18/18 条流水线完成内部精度对齐 | 可公开数量和方法，行业基线需确认 | 补充可复现任务列表 |
| 并发 CV 负载 | BM1688 单机 16 路 CV 推理完成内部验证 | 可公开结论，环境细节待补 | 补齐设备、SDK、模型和输入参数 |
| 系统回归 | 多轮内部回归 | 可公开覆盖范围 | 补充公开回归清单 |
| 试点部署 | 已在脱敏试点场景验证 | 仅可公开行业和场景类型 | 移除客户、地点和内部识别信息 |

## README 中的验证声明

README 当前包含以下验证口径：

| 声明 | 当前处理 |
| --- | --- |
| 200 个视频样本连续播放测试未观察到内存泄漏或崩溃 | 保留为内部压力测试结论，发布前补充测试时长和输入构成 |
| 18/18 条 CV 流水线与内部行业基线完成精度对齐 | 保留为内部验证结论，发布前补充算法/任务清单 |
| 单台 BM1688 上完成 16 路 CV 推理验证 | 保留为内部负载结论，发布前补充设备、SDK、分辨率、编码和模型版本 |
| 多轮系统回归 | 保留，发布前补充回归范围 |
| 教育、智慧园区、工业安全试点场景 | 仅使用脱敏行业描述，不公开客户或现场信息 |

## 建议公开报告结构

公开 release 附带的验证报告建议包含：

1. 项目版本和 commit。
2. 硬件型号、CPU、内存、NPU、系统镜像和 SDK 版本。
3. 构建方式和运行模式。
4. 输入数据说明：视频数量、分辨率、帧率、编码格式、时长、是否脱敏。
5. 模型说明：模型名称、版本、输入尺寸、后处理、运行 backend。
6. 测试场景：通道数、场景任务数、是否启用 OSD、是否启用推流、是否启用事件推送。
7. 指标定义：FPS、端到端延迟、CPU、内存、NPU/VPU 使用率、错误数、重启次数。
8. 测试结果和已知限制。
9. 可复现命令、配置文件和样例输出。

## 公开前不能包含的内容

- 客户名称、项目名称、地点、摄像头位置。
- 私有视频、图片、模型权重和下载地址。
- 内部 IP、域名、账号、token、设备 SN。
- 未获授权的第三方数据集或模型。
- 可以反推出客户业务流程的场景配置。

## 最小可发布版本

如果首个公开版本无法附带完整测试素材，建议至少公开：

- 验证环境摘要。
- 指标定义。
- 测试表格。
- 已知限制。
- “内部验证，公开复现数据待补充”的说明。

## English

This page defines the boundary between internal validation and public reproducibility for the first open-source release.

If raw videos, customer scenarios, or model packages cannot be published, keep the validation claims scoped as internal validation and publish the test methodology, environment summary, metric definitions, known limitations, and a plan for reproducible public data.

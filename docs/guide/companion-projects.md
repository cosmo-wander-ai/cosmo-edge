# 配套工程：训练 Skill 与 Connect

CosmoEdge 的引擎与控制台负责视频接入、模型运行、场景编排及告警输出。配套工程帮助你完成
模型定制和现场运营任务，分别安装、维护和发布。

| 你的任务 | 使用入口 | 当前版本 |
| --- | --- | --- |
| 部署视频 AI 应用，配置设备与场景 | [快速上手](../tutorials/01-quickstart/quickstart.md) | CosmoEdge 正式版 `v1.1.0`；主干另有待发布更新 |
| 从数据开始定制目标检测模型 | [CosmoEdge 辅助训练 Skill](https://github.com/cosmo-wander-ai/cosmoedge-training-skill) | 已发布 `v0.1.0-preview.1`；后续修订通过该仓库 `main` 提供 |
| 让 AI 助手查告警、取图和确认后启停检测 | [CosmoEdge Connect](https://github.com/cosmo-wander-ai/cosmoedge-connect) | `v0.1.0-alpha.1`，Alpha 源码预览 |

## 从数据开始：辅助训练 Skill

把业务目标、整批数据源和可用机器交给已有的 AI 开发助手。Skill 帮助助手检查数据与标注、
制定训练方案、处理执行问题、比较模型效果，并交回模型、配置、误检漏检样例与验证结果。
已有标签、模型或训练工程也可以继续利用。

准备能读取文件、执行程序和查看图片的助手，以及本次可用的数据和计算资源。
安装与任务示例见 [Training Skill README](https://github.com/cosmo-wander-ai/cosmoedge-training-skill#开始使用)。
训练、评估和导出可以单独完成；需要 CosmoEdge 应用验证时，再检查目标平台的模型格式、
配置、运行环境和业务效果。仓库内的模型转换入口见[智能体辅助二次开发](../development/agent-assisted-development.md)。

截至 2026-10-09，发布附件仍是
[`v0.1.0-preview.1`](https://github.com/cosmo-wander-ai/cosmoedge-training-skill/releases/tag/v0.1.0-preview.1)。
9 月 28 日的模型接入修订位于 `main`，增加按候选模型推导配置、保存后回读和包装诊断的指导；
旧发布附件未被替换。该次修订的程序合成场景检查不代表真实 BMRT、目标设备性能或业务精度验收，
详见 [9 月 28 日验证记录](https://github.com/cosmo-wander-ai/cosmoedge-training-skill/blob/main/docs/validation/20260928/README.md)。

## 使用已有设备：Connect 与本地 MCP

Connect 运行在你的电脑上，让 AI 助手通过本地 MCP 或 WorkBuddy 接入一台已部署的
CosmoEdge 设备及其已有机位。可以从一项具体任务开始：查询指定日期的留存告警、
取指定机位的一张图，或在检修时确认暂停与恢复已有检测。

助手组织查询、分析图像并整理结果；Connect 返回执行结果、原报告和原图。
启停操作各自需要人在本机页面确认。设备信息与凭据在本机连接页填写。

- [安装与构建](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/installation.zh-CN.md)
- [MCP 接入](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/mcp.zh-CN.md)
- [首次连接与操作](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/getting-started.zh-CN.md)
- [版本记录](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/CHANGELOG.md)与[兼容性范围（英文）](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/compatibility.md)

当前版本为 Alpha 源码预览。看图是按需取图与宿主模型分析；持续视频检测由 CosmoEdge 引擎承担。
按计划检查和通知需要所选宿主提供并完成集成验证。支持的客户端与平台以 Connect 的兼容性记录为准。

## 版本与验证怎样对应

三个工程分别发布。安装训练 Skill 或 Connect 不会升级设备上的 CosmoEdge，也不表示
v1.1.0 安装包内置了训练或 MCP 服务。使用前核对各工程文档中的依赖与接口要求。

主干新增能力见 [CosmoEdge Unreleased](https://github.com/cosmo-wander-ai/cosmo-edge/blob/main/CHANGELOG.md#unreleased)。
模型接入与设备验证应绑定实际使用的源码、安装包和模型；历史 benchmark 只适用于其记录的条件。

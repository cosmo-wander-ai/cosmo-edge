---
title: 五卷教程
description: CosmoEdge 教程阅读路径。
prev:
  text: 文档首页
  link: /
next:
  text: 卷一：快速上手
  link: /tutorials/01-quickstart/quickstart
---

# 五卷教程

教程面向实际操作场景，重点是帮助读者完成任务，而不是介绍产品卖点。建议按顺序阅读；已经熟悉基础流程的读者也可以直接跳到对应卷。

## 阅读顺序

| 卷 | 主题 | 适合读者 | 目标 | 前置条件 |
| --- | --- | --- | --- | --- |
| [卷一：快速上手](./01-quickstart/quickstart.md) | 设备连接、平台访问、实时检测 | 首次体验者 | 从开箱通电到看到 AI 检测结果 | 已收到预装设备 |
| [卷二：场景配置](./02-scenario-config/scenario-config.md) | 通道、算法、区域、阈值、告警 | 项目实施和集成商 | 配置一个可运行的业务场景 | 完成卷一 |
| [卷三：VLM / DINO 指南](./03-vlm-guide/vlm-guide.md) | 视觉状态判断与开放目标检测 | 方案工程师、开发者 | 判断什么时候使用 VLM 或 DINO | 完成卷二 |
| [卷四：Pipeline 编排](./04-pipeline-orchestration/pipeline-orchestration.md) | 节点、数据流、规则链路 | 高级用户 | 修改或创建可视化算法编排 | 完成卷二和卷三 |
| [卷五：模型移植](./05-model-porting/model-porting.md) | 第三方模型转换、上传、验证、接入 | 算法和 ML 工程师 | 将自有模型接入 CosmoEdge 场景任务 | 完成卷四；需 Docker 环境 |

## 使用建议

每一卷都以可验证结果为目标。阅读时建议保留系统页面、测试视频或测试图片，完成每个关键步骤后对照文中的预期结果检查。

当前项目的构建、部署、API 和开发入口已经整理到以下页面：

- [构建指南](../guide/build.md)
- [部署指南](../guide/deployment.md)
- [运行配置](../guide/configuration.md)
- [故障排查](../guide/troubleshooting.md)
- [架构概览](../guide/architecture.md)
- [API 概览](../reference/api.md)
- [字段级 API 参考](../reference/api-fields.md)
- [MQTT 接入参考](../reference/mqtt.md)
- [HTTP Webhook 参考](../reference/webhook.md)
- [模型与资源](../reference/models.md)
- [前端工程](../development/frontend.md)
- [后端开发](../development/backend.md)
- [CI 与质量检查](../development/ci.md)

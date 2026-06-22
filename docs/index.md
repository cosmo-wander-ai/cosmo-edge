---
layout: home

hero:
  name: CosmoEdge
  text: 文档中心
  tagline: 面向边缘 AI 引擎、场景配置、模型接入和系统集成的项目文档。
  image:
    src: ./assets/hero.gif
    alt: CosmoEdge running multiple edge AI pipelines
  actions:
    - theme: brand
      text: 开始五卷教程
      link: /tutorials/
    - theme: alt
      text: 构建指南
      link: /guide/build

features:
  - title: 教程
    details: 按卷组织，从设备上手、场景配置、VLM / DINO、Pipeline 编排到第三方模型移植。
  - title: 构建、部署与排障
    details: 记录当前确认的 x86 Docker、Sophon 发布包、运行目录、端口、服务进程、运行配置和故障排查。
  - title: 参考与开发
    details: 覆盖 API 类别、模型资源、前端工程、C++ 后端开发和 CI 质量检查入口。
---

## 当前内容

第一版文档站接入现有五卷教程，并补充当前项目可确认的构建、部署、架构、API、模型资源和开发入口。教程中部分内容会随着公开仓库整合继续完善；构建和部署类文档以当前仓库脚本为准。

## 阅读路径

1. 第一次体验 CosmoEdge，请从[卷一：快速上手](tutorials/01-quickstart/quickstart.md)开始。
2. 需要确认当前构建命令，请阅读[构建指南](guide/build.md)。
3. 需要了解运行目录、端口和服务进程，请阅读[部署指南](guide/deployment.md)。
4. 需要理解项目结构，请阅读[架构概览](guide/architecture.md)。
5. 需要排查运行问题，请阅读[故障排查](guide/troubleshooting.md)。
6. 需要对接接口或模型资源，请阅读[API 概览](reference/api.md)、[字段级 API 参考](reference/api-fields.md)、[MQTT 接入参考](reference/mqtt.md)、[HTTP Webhook 参考](reference/webhook.md)和[模型与资源](reference/models.md)。
7. 需要参与开发，请阅读[前端工程](development/frontend.md)、[后端开发](development/backend.md)和[CI 与质量检查](development/ci.md)。
8. 需要维护前端术语和紧凑文案，请阅读[I18N Glossary](i18n/GLOSSARY.md)和[Short Scope Rules](i18n/SHORT-SCOPES.md)。

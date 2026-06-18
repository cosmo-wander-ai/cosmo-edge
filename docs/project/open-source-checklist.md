---
title: 开源发布清单
description: CosmoEdge 发布到 GitHub 前需要确认的代码、文档、许可证、资源和安全事项。
prev:
  text: 性能基准复现说明
  link: /project/benchmarks
next:
  text: 敏感信息公开前检查
  link: /project/sensitive-data-review
---

# 开源发布清单

本文用于跟踪正式发布到 `cosmo-wander-ai/cosmo-edge` 前需要完成或确认的事项。

## 仓库基础信息

- [x] README 英文版
- [x] README 中文版
- [x] VitePress 文档站结构
- [x] GitHub Pages workflow
- [x] 贡献指南
- [x] 行为准则
- [x] 顶层 `SECURITY.md`
- [x] 顶层 `LICENSE`
- [x] 顶层 `NOTICE`
- [x] 顶层 `CHANGELOG.md`
- [x] Issue templates
- [x] Pull request template
- [x] Issue template config
- [x] GitHub repository metadata draft
- [ ] GitHub repository description
- [ ] GitHub topics
- [ ] GitHub homepage

## 构建和运行

- [x] x86 Docker 开发运行命令已确认
- [x] Sophon 发布包构建命令已确认
- [x] 文档站构建命令已确认
- [x] CI 与质量检查文档已补充
- [x] VitePress 构建通过
- [ ] CI 中执行文档构建
- [ ] CI 中执行基础格式检查
- [ ] CI 中执行可公开运行的测试

## 许可证

发布前必须确认：

- [x] 项目整体开源许可证：Apache License 2.0
- [x] `CONTRIBUTING.md` 中许可证表述与最终许可证一致
- [x] README badge 与最终许可证一致
- [ ] vendored 第三方依赖许可证
- [ ] `3rd/` 下源码是否允许随仓库发布
- [ ] FFmpeg / OpenH264 / x264 相关发布策略
- [ ] 前端 npm 依赖许可证
- [x] 第三方依赖与许可证审计底稿已补充
- [ ] 补齐 `NOTICE` 中 required third-party attribution

## 模型和资源

发布前必须确认：

- [ ] `data/resource/aiboxresource` 是否可公开
- [ ] `data/resource/aiboxresource_x86` 是否可公开
- [ ] 是否包含模型权重
- [ ] 示例模型的许可证
- [ ] 算法模板中是否包含客户或内部场景信息
- [ ] `prebuild/libcosmo_model_guard.so` 是否可公开
- [ ] 加密模型能力是否作为公开功能发布

## 敏感信息检查

- [x] 敏感信息检查流程已补充
- [x] 候选扫描脚本已补充：`scripts/open_source_scan.ps1`
- [ ] 私有 IP 扫描结论
- [ ] 内部域名扫描结论
- [ ] token / password / secret 扫描结论
- [ ] 设备 SN 扫描结论
- [ ] 客户名称扫描结论
- [ ] 内部路径扫描结论
- [ ] 私有模型下载地址扫描结论
- [ ] 测试视频或图片中的敏感内容复核结论

## 文档

- [x] 构建指南
- [x] 部署指南
- [x] 运行配置
- [x] 故障排查
- [x] 架构概览
- [x] API 概览
- [x] 字段级 API 参考
- [x] MQTT 事件结构说明
- [x] HTTP webhook 说明
- [x] 模型与资源
- [x] 前端工程
- [x] 后端开发
- [x] CI 与质量检查
- [x] 公开验证报告
- [x] 性能基准复现说明
- [x] 敏感信息公开前检查
- [x] 安全说明
- [x] 第三方依赖与许可证说明
- [x] 发布说明
- [x] 仓库元数据

## 发布口径

发布前需要统一：

- [ ] 项目名称：`CosmoEdge` / `Cosmo Edge` / 其他
- [ ] 版本号：当前 README 使用 `v0.1.0`
- [ ] Windows 支持范围
- [ ] Sophon BM1688 / BM1684X 支持范围
- [ ] 生产授权和受限模式说明
- [ ] CosmoEdge-ready 设备说明
- [ ] 联系邮箱和社区入口

## 建议发布顺序

1. 确认许可证和资源发布边界。
2. 运行敏感信息扫描并复核候选命中。
3. 清理或移除不能公开的资源、模型、二进制和内部信息。
4. 跑文档站构建和基础测试。
5. 确认 Issue / PR 模板满足公开协作要求。
6. 创建首个公开 tag 或 release。
7. 发布 GitHub Pages 文档站。
8. 配置 GitHub repository description、topics、homepage 和 social preview。

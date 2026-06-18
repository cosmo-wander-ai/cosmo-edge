---
title: 发布说明
description: CosmoEdge 公开版本发布说明、待办事项和发布前检查。
prev:
  text: 第三方依赖与许可证
  link: /project/third-party-licenses
next:
  text: 仓库元数据
  link: /project/repository-metadata
---

# 发布说明

本文用于准备公开 release。正式发布时，应以顶层 `CHANGELOG.md` 和 GitHub Release 页面为准。

## 当前版本

README 当前使用：

```text
v0.1.0
```

该版本应被视为首个公开 release candidate，最终发布时间和 release tag 需要维护者确认。

## 当前已完成

- README 英文版和中文版。
- VitePress 文档站。
- 五卷教程目录。
- 构建、部署、运行配置、故障排查、架构、API、模型资源、前后端开发文档。
- 字段级 API、MQTT 接入和 HTTP webhook 参考。
- CI 与质量检查说明。
- 开源发布清单。
- 安全说明和顶层 `SECURITY.md`。
- GitHub Issue templates 和 Pull Request template。
- Apache License 2.0 `LICENSE`。
- 初版 `NOTICE`。
- 第三方依赖与许可证审计底稿。
- 公开验证报告和性能基准复现说明。

## 发布前必须确认

- 第三方依赖许可证审计是否完成。
- `NOTICE` 是否需要补入 required third-party attribution。
- `3rd/` 下源码是否全部允许公开发布。
- `data/resource/*` 是否允许公开发布。
- 是否包含模型权重。
- `prebuild/libcosmo_model_guard.so` 是否可公开。
- README 中的验证和性能数据是否有可公开报告支撑。
- Windows 支持范围。
- Sophon BM1688 / BM1684X 支持范围。
- 生产授权和受限模式公开说明。

## Release Checklist

发布前建议执行：

```bash
npm ci
npm run docs:build
```

项目构建建议至少验证：

```bash
docker compose -f docker-compose.x86.yml up -d --build
docker compose -f docker-compose.x86.yml logs -f
docker compose -f docker-compose.x86.yml down
```

Sophon 发布包路径建议验证：

```bash
bash scripts/build_sophon_package.sh
```

如使用 Windows PowerShell：

```powershell
.\scripts\build_sophon_package.ps1
```

## 发布内容建议

首个公开 release 建议包含：

- release tag
- release notes
- 构建和运行方式
- 已知限制
- 支持平台
- 模型和资源发布边界
- 安全联系方式
- 第三方许可证说明
- 公开验证报告或内部验证边界说明
- 性能基准复现方法

## 已知限制

- 字段级 API、MQTT 接入和 HTTP webhook 已整理为独立公开文档，后续仍可继续生成 OpenAPI/schema。
- 公开验证报告已补充验证范围和证据清单，但可复现公开素材仍需维护者确认。
- 性能基准复现说明已补充模板，实际 benchmark 报告仍需绑定具体设备、commit、模型和输入。
- 第三方依赖许可证审计尚未完成。

## 相关文档

- [开源发布清单](open-source-checklist.md)
- [公开验证报告](validation.md)
- [性能基准复现说明](benchmarks.md)
- [第三方依赖与许可证](third-party-licenses.md)

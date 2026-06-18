---
title: 敏感信息公开前检查
description: CosmoEdge 开源发布前的敏感信息扫描范围、执行方法、人工复核表和待填写发布结论。
prev:
  text: 性能基准复现说明
  link: /project/benchmarks
next:
  text: 开源发布清单
  link: /project/open-source-checklist
---

# 敏感信息公开前检查

本文用于公开仓库前的敏感信息检查。自动扫描只能发现候选项，最终是否可公开需要维护者人工确认。

## 需要检查什么

| 类别 | 示例 | 处理方式 |
| --- | --- | --- |
| 密钥和凭证 | token、password、secret、API key、证书私钥 | 必须删除、替换或迁移到环境变量 |
| 设备信息 | 真实设备 SN、授权码、生产设备 ID | 替换为 `DEVICE_SN`、`EXAMPLE_SN` 等占位值 |
| 网络信息 | 私有 IP、内部域名、VPN 地址、私有下载源 | 删除或替换为文档化示例 |
| 客户信息 | 客户名称、项目名称、地点、摄像头位置 | 脱敏为行业或场景类型 |
| 模型和资源 | 私有模型权重、bmodel、ONNX、私有下载链接 | 确认许可证；不能公开时移出仓库 |
| 图片和视频 | 真实现场图片、测试视频、截图 OCR 中的敏感内容 | 脱敏、替换或移除 |
| 预编译二进制 | `prebuild/*.so`、供应商 SDK、闭源插件 | 确认分发授权；不能公开时移除 |
| 构建产物 | `build*`、packages、dist、临时缓存 | 不纳入源码仓库 |

## 扫描脚本

仓库提供一个轻量候选扫描脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/open_source_scan.ps1
```

默认每类最多展示 80 条候选项。需要扩大样本时：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/open_source_scan.ps1 -MaxMatches 200
```

默认会跳过：

```text
.git
node_modules
docs/.vitepress/dist
build*
3rd
常见图片和视频二进制文件
```

如需要连同 `3rd/` 一起扫描：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/open_source_scan.ps1 -IncludeThirdParty
```

当前仓库的旧 `doc/`、`data/Interface`、`data/resource`、模型教程 OCR 注释和示例配置中可能会出现大量候选项。它们不一定都是泄露，但发布前需要逐项确认：真实值删除或脱敏，示例值保留并说明，无法确认的资源先不随首次开源发布。

说明：

- 脚本依赖 `rg`。
- 脚本只输出候选命中，不自动删除文件。
- 命中示例字段不一定是泄露，例如文档中的 `DEVICE_SN` 是安全占位值。
- 每个命中都应人工复核并记录处理结论。

## 建议人工复核路径

| 路径 | 复核重点 | 结论 |
| --- | --- | --- |
| `README.md` / `README.zh-CN.md` | URL、联系方式、性能声明、截图路径 | TODO: 填写 |
| `docs/` | 教程截图 OCR、示例 IP、设备 SN、客户场景描述 | TODO: 填写 |
| `.github/` | Issue/PR 模板是否提醒用户移除敏感信息 | TODO: 填写 |
| `scripts/` | 内部路径、私有源、部署地址、账号口令 | TODO: 填写 |
| `Dockerfile*` | 私有镜像、内部 mirror、缓存地址 | TODO: 填写 |
| `data/resource/aiboxresource` | 模型、场景模板、图片、内部名称 | TODO: 填写 |
| `data/resource/aiboxresource_x86` | x86 示例资源能否公开 | TODO: 填写 |
| `prebuild/libcosmo_model_guard.so` | 是否可公开分发 | TODO: 填写 |
| `3rd/` | 第三方源码许可证和嵌套依赖 | TODO: 填写 |
| `build*` / packages | 是否应从公开仓库移除或加入 `.gitignore` | TODO: 填写 |

## 资源发布结论模板

发布前请维护者补充：

| 项目 | 结论 | 负责人 | 备注 |
| --- | --- | --- | --- |
| `data/resource/aiboxresource` | TODO: 可公开 / 部分公开 / 不公开 | TODO | TODO |
| `data/resource/aiboxresource_x86` | TODO: 可公开 / 部分公开 / 不公开 | TODO | TODO |
| 示例模型权重 | TODO: 可公开 / 替换 / 移除 | TODO | TODO |
| `prebuild/libcosmo_model_guard.so` | TODO: 保留 / 移除 / 闭源扩展 | TODO | TODO |
| 示例图片/视频 | TODO: 可公开 / 脱敏 / 移除 | TODO | TODO |
| 客户或试点案例 | TODO: 脱敏行业描述 / 不公开 | TODO | TODO |

## 误报处理

以下内容通常可以保留，但仍应人工确认：

- 文档中的占位值：`DEVICE_SN`、`EXAMPLE_TOKEN`、`localhost`。
- 教程中的本地地址：`127.0.0.1`、`localhost`。
- 公开示例网段：`192.0.2.0/24`、`198.51.100.0/24`、`203.0.113.0/24`。
- API 字段名：`token`、`passwd`、`deviceSn`，前提是没有真实值。

## 发布前通过标准

- 所有脚本候选命中均已复核。
- 所有真实密钥、token、证书私钥和账号口令已移除。
- 所有真实设备 SN、客户名称、内部域名和私有下载地址已移除或脱敏。
- 模型、资源、图片、视频和二进制的公开边界已由维护者确认。
- 无法确认的文件不随首个公开仓库发布。

## English

This page defines the sensitive-data review process before publishing the repository.

Use `scripts/open_source_scan.ps1` to find candidates, then review every match manually. The script is intentionally conservative and may report false positives. Do not publish real credentials, private keys, device serial numbers, customer data, internal domains, private model links, proprietary media, or unapproved prebuilt binaries.

Use `-MaxMatches` to increase the per-category sample size. If ownership or distribution permission is unclear, leave the release conclusion as TODO and keep the file or resource out of the first public release until it is reviewed.

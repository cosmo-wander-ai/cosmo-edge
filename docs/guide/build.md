---
title: 构建指南
description: x86 Docker、Sophon 发布包和 CPU 测试构建路径。
prev:
  text: 文档首页
  link: /
next:
  text: 部署指南
  link: /guide/deployment
---

# 构建指南

本文只记录当前仓库中已经确认的构建路径。历史文档或旧脚本中出现过、但当前仓库无法验证的路径，不作为公开支持路径。

## 构建路径总览

| 路径 | 用途 | 是否启动服务 | 输出 |
| --- | --- | --- | --- |
| x86 Docker 开发运行环境 | 首次体验、开发评估、生成 x86 发布包 | 是 | `build_output/` |
| Sophon 发布包构建 | 生成 aarch64/Sophon 部署包 | 否 | `build_output/` |
| CPU 测试构建 | 构建 `cosmo-tests` | 否 | `build_cpu/cosmo-tests` |

## x86 Docker 开发运行环境

Linux:
```bash
docker compose -f docker-compose.x86.yml up -d --build
```

Windows (PowerShell/CMD):
```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
```

该路径来自：

- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
- `Dockerfile.x86`
- `scripts/build_cpu.sh`

已确认构建参数：

| 参数 | 值 |
| --- | --- |
| `COSMO_TARGET_ARCH` | `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | `OFF` |
| `COSMO_NN_USE_CPU_BACKEND` | `ON` |
| `COSMO_ENABLE_OPENH264` | `ON` |
| `COSMO_DEV_MODE` | `ON` |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` |

构建完成后：

- Web 控制台通过 `http://127.0.0.1:8080` 访问。
- 发布包和构建产物导出到 `build_output/`。
- 运行数据保存在 Docker volume `cosmo-x86-data`。
- 资源目录挂载到 Docker volume `cosmo-x86-app-resource`。

## Sophon 发布包构建

Linux / Bash：

```bash
bash scripts/build_sophon_package.sh
```

Windows PowerShell：

```powershell
.\scripts\build_sophon_package.ps1
```

该路径来自：

- `docker-compose.sophon.yml`
- `Dockerfile.sophon`
- `scripts/build_sophon_package.sh`
- `scripts/build_sophon_package.ps1`
- `scripts/build.sh`

已确认行为：

- 默认基础镜像为 `stream_dev:0.2`。
- 使用 `scripts/build.sh -m data/resource/aiboxresource` 构建（生产包不启用 dev mode，故不传 `-t`）。
- 只导出发布包，不启动服务。
- 发布包导出到 `build_output/`。

常用覆盖变量：

```bash
SOPHON_STREAM_DEV_TAR=/path/to/stream_dev_22.04.tar bash scripts/build_sophon_package.sh
SOPHON_BASE_IMAGE=stream_dev:0.2 bash scripts/build_sophon_package.sh
```

如果本地没有 `stream_dev:0.2`，辅助脚本会尝试通过 `dfss` 下载并加载 `stream_dev_22.04.tar`。

## CPU 测试构建

```bash
bash scripts/build_cpu_test.sh
```

该脚本会使用 CPU 后端配置 CMake，并开启：

```text
BUILD_TESTS=ON
```

目标产物：

```text
build_cpu/cosmo-tests
```


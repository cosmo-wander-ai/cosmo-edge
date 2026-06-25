---
title: 运行配置
description: 当前仓库可确认的环境变量、资源目录、端口、日志和数据路径。
prev:
  text: 部署指南
  link: /guide/deployment
next:
  text: 故障排查
  link: /guide/troubleshooting
---

# 运行配置

本文整理当前仓库中可确认的运行配置入口。配置来源包括 Docker Compose、Dockerfile、CMake、启动脚本和应用常量。

## Docker Compose 配置

x86 开发运行环境：

- Linux: `docker-compose.x86.yml`
- Windows: `docker-compose.x86.windows.yml`

Sophon 发布包构建：

```text
docker-compose.sophon.yml
```

## x86 Docker 环境变量

`Dockerfile.x86` 中设置：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `INSTALLPATH` | `/appfs/cosmo_wander/cwai_data` | 主安装目录（可按需覆盖） |
| `COSMO_PLATFORM_TYPE` | `x86_64` | 平台类型 |

`scripts/docker-entrypoint.x86.sh` 会保证运行目录存在，并执行：

```bash
${INSTALLPATH}/scripts/run_start.sh start ${DATADIR}/log/logs/INTE_RUN_container.log
```

## Sophon 构建变量

`docker-compose.sophon.yml` 支持以下构建参数：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `SOPHON_BASE_IMAGE` | `stream_dev:0.2` | Sophon 构建基础镜像 |
| `SOPHON_APT_MIRROR` | `https://mirrors.aliyun.com/debian` | apt 镜像 |
| `SOPHON_NODE_DIST_BASE_URL` | `https://npmmirror.com/mirrors/node` | Node 下载镜像 |
| `SOPHON_RUSTUP_INIT_URL` | `https://rsproxy.cn/rustup-init.sh` | rustup-init 下载地址 |
| `SOPHON_RUSTUP_DIST_SERVER` | `https://rsproxy.cn` | Rust dist server |
| `SOPHON_RUSTUP_UPDATE_ROOT` | `https://rsproxy.cn/rustup` | Rust update root |

辅助脚本还支持：

| 变量 | 说明 |
| --- | --- |
| `SOPHON_STREAM_DEV_TAR` | 使用已下载的 `stream_dev_22.04.tar` |
| `SOPHON_STREAM_DEV_DFSS_URL` | Sophon stream dev 镜像 dfss 地址 |
| `SOPHON_DOCKER_CACHE_DIR` | Sophon 镜像缓存目录 |
| `SOPHON_PIP_INDEX_URL` | 安装 `dfss` 时使用的 pip 镜像 |

## 资源目录

| 构建路径 | 资源目录 |
| --- | --- |
| x86 Docker | `data/resource/aiboxresource_x86` |
| Sophon package | `data/resource/aiboxresource` |

CMake 通过 `RESOURCE_DIR` 安装资源。

## 运行目录

| 路径 | 说明 |
| --- | --- |
| `<INSTALLPATH>` | 主安装目录，由 Dockerfile 中的 `INSTALLPATH` 环境变量设定 |
| `<DATADIR>` | 用户数据，默认位于持久化卷上 |
| `<DATADIR>/log/logs` | 日志 |
| `<DATADIR>/upgrade` | 升级包 |
| `<DATADIR>/tmp/*` | nginx 临时目录 |

## 端口

| 端口 | 说明 |
| --- | --- |
| `8080` | x86 Docker 主机访问 Web 控制台 |
| `80` | 容器内 nginx |
| `8000` | 后端 HTTP |
| `9000` | 后端 WebSocket |
| `1936` | SRS RTMP |
| `1985` | SRS API |
| `18088` | SRS HTTP stream |

## 流媒体变量

`scripts/run_start.sh` 设置：

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## CMake 关键选项

用户可设置的 cache 选项（`option()` 声明）：

| 选项 | 说明 |
| --- | --- |
| `COSMO_TARGET_ARCH` | `aarch64` 或 `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | 启用 Sophon 后端 |
| `COSMO_NN_USE_CPU_BACKEND` | 启用 CPU/ONNX Runtime 后端（与 Sophon 互斥） |
| `COSMO_DEV_MODE` | 开发模式 |
| `BUILD_TESTS` | 构建测试 |

以下为**派生变量**（由后端选择自动推导，非 `option()` 声明，不可直接 `-D` 设置，列出仅供了解）：

| 派生变量 | 说明 |
| --- | --- |
| `COSMO_ENABLE_OPENH264` | CPU 后端时自动 `ON`，Sophon 后端时 `OFF` |
| `COSMO_OPENH264_USE_ASM` | 始终为 `OFF` |
| `COSMO_MODEL_GUARD` | Sophon 后端时 `ON`（启用加密模型校验），CPU 后端时 `OFF` |


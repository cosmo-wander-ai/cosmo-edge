---
title: 部署指南
description: 当前运行目录、服务进程、端口、升级包和 systemd 行为。
prev:
  text: 构建指南
  link: /guide/build
next:
  text: 架构概览
  link: /guide/architecture
---

# 部署指南

本文根据当前运行脚本整理，主要涉及：

- `scripts/docker-entrypoint.x86.sh`
- `scripts/start.sh`
- `scripts/run_start.sh`
- `scripts/install.sh`

## x86 Docker 运行环境

启动：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

停止：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml down
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml down
  ```

查看日志：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml logs -f
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml logs -f
  ```

## 运行目录

| 路径 | 说明 |
| --- | --- |
| `<INSTALLPATH>` | 主安装目录，由 Dockerfile 或部署脚本设定 |
| `<INSTALLPATH>/resource` | 运行资源目录 |
| `<DATADIR>` | 用户持久化数据目录，默认位于持久化卷上 |
| `<DATADIR>/log/logs` | 日志目录 |
| `<DATADIR>/upgrade` | 升级包目录 |

## 运行进程

启动脚本会拉起：

- `nginx` (system, `/usr/sbin/nginx`)
- `srs`
- `cosmo-engine`

对应路径：

`${INSTALLPATH}` 由 Dockerfile 中的 `INSTALLPATH` 环境变量设置（默认见运行配置）。
具体路径：
```text
/usr/sbin/nginx  (system nginx)
${INSTALLPATH}/bin/srs
${INSTALLPATH}/bin/cosmo-engine
```

## 默认端口

| 端口 | 来源 | 用途 |
| --- | --- | --- |
| `8080 -> 80` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | x86 Docker Web 控制台 |
| `1936` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | RTMP |
| `1985` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | SRS API |
| `18088` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | HTTP stream |
| `8000` | `src/app/AppConstants.h`（`kDefaultHttpPort`，TCP） | 后端 HTTP 常量（容器内监听 TCP） |
| `9000` | `src/app/AppConstants.h`（`kDefaultWebSocketPort`，TCP） | 后端 WebSocket（容器内监听 TCP） |

> 端口暴露说明：`8080 -> 80`、`1936`、`1985`、`18088` 是 x86 Docker 对**主机暴露**的端口。`8000`、`9000` 是容器内进程端口；其中 `8000` 在 `docker-compose.x86.yml` 中以 `8000:8000/udp` 形式映射到主机（用于设备发现等 UDP 场景），与后端 HTTP 的 TCP 监听不同。主机侧访问后端 HTTP/WebSocket API 通常经由 nginx（容器内 `80`，映射到主机 `8080`）反向代理，而不是直接访问主机的 `8000`。

运行脚本设置的流媒体环境变量：

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## 发布包结构

安装/升级脚本期望发布包中包含：

- `bin`
- `files`
- `font`
- `scripts`
- `web`

可选或按存在处理：

- `lib`
- `resource`

升级包文件名匹配：

```text
cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz
```

## systemd 服务

`scripts/install.sh` 会创建：

```text
/etc/systemd/system/cosmo.service
```

服务启动命令：

```text
ExecStart=${INSTALLPATH}/scripts/inte_run_start.sh
```

## 接口文档静态链接

打包接口文件：

- `data/Interface/ai-box-interface_v1.0.html`
- `data/Interface/mqtt_v1.0.html`

运行时会链接到：

- `web/staticfile/httpInterface.html`
- `web/staticfile/mqttInterface.html`


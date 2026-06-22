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
| `/appfs/cosmo_wander/cwai_data` | 主安装目录 |
| `/data/cwaiuserdata` | 用户持久化数据目录 |
| `/data/cwaiuserdata/log/logs` | 日志目录 |
| `/data/cwaiuserdata/upgrade` | 升级包目录 |
| `/appfs/cosmo_wander/cwai_data/resource` | 运行资源目录 |

## 运行进程

启动脚本会拉起：

- `nginx` (system, `/usr/sbin/nginx`)
- `srs`
- `cosmo-engine`

对应路径：

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
| `8000` | `src/app/AppConstants.h` | 后端 HTTP |
| `9000` | `src/app/AppConstants.h` | 后端 WebSocket |

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
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

## 接口文档静态链接

打包接口文件：

- `data/Interface/ai-box-interface_v1.0.html`
- `data/Interface/mqtt_v1.0.html`

运行时会链接到：

- `web/staticfile/httpInterface.html`
- `web/staticfile/mqttInterface.html`

## 生产授权说明

非开发模式构建中包含设备 SN 校验。开发模式 `COSMO_DEV_MODE` 会跳过该校验。生产授权、公开设备策略和受限模式的对外表述，需要在正式发布前由维护者确认。

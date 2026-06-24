---
title: 故障排查
description: 构建、运行、端口、Sophon 镜像、日志和文档站常见问题。
prev:
  text: 运行配置
  link: /guide/configuration
next:
  text: 架构概览
  link: /guide/architecture
---

# 故障排查

本文收集当前项目最常见的构建和运行问题。

## Web 控制台打不开

确认使用的是主机端口 `8080`：

```text
http://127.0.0.1:8080
```

检查容器状态：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml ps
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml ps
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

## 端口冲突

x86 Compose 会发布：

- `8080`
- `1936`
- `1985`
- `18088`
- `8000/udp`

如果端口被占用，可以修改 `docker-compose.x86.yml` (或 Windows 上的 `docker-compose.x86.windows.yml`) 的主机端口，或停止占用端口的服务。

## `build_output/` 没有发布包

使用完整运行命令：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

Sophon 路径使用：

```bash
docker compose -f docker-compose.sophon.yml up --build
```

注意：`docker compose build` 只构建镜像，不一定执行导出发布包的容器命令。

## Sophon 基础镜像不存在

优先使用辅助脚本：

```bash
bash scripts/build_sophon_package.sh
```

如果已经手动下载镜像 tar：

```bash
SOPHON_STREAM_DEV_TAR=/path/to/stream_dev_22.04.tar bash scripts/build_sophon_package.sh
```

如果 `dfss` 下载失败，请检查网络，或手动执行：

```bash
docker load -i /path/to/stream_dev_22.04.tar
```

然后重新运行构建脚本。

## nginx / SRS / cosmo-engine 未启动

运行脚本：

```text
${INSTALLPATH}/scripts/run_start.sh
```

启动顺序包括：

1. 停止已有进程。
2. 启动 nginx。
3. 启动 SRS。
4. 启动 `cosmo-engine`。

检查日志：

```text
/data/cwaiuserdata/log/logs
```

## 文档站构建失败

先安装依赖：

```bash
npm ci
```

再构建：

```bash
npm run docs:build
```

在 Windows PowerShell 中如果遇到 `npm.ps1` 执行策略问题，可以使用：

```powershell
npm.cmd run docs:build
```

## `vitepress` 未找到

说明还没有安装文档站依赖：

```bash
npm ci
```

## npm audit 提示漏洞

当前文档站依赖可能会出现 npm audit 提示。不要盲目升级依赖；升级前应确认 VitePress、主题配置和 GitHub Pages workflow 仍能构建通过。

## Windows 本机 CPU 构建

当前仓库没有确认可用的 Windows 本机 CPU 构建脚本。不要把旧脚本或旧命令写成公开支持路径。

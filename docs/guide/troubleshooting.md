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

- **Apple Silicon macOS (Preview)**:

  ```bash
  ./scripts/macos-docker-preview.sh status
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

- **Apple Silicon macOS (Preview)**:

  ```bash
  ./scripts/macos-docker-preview.sh logs --follow
  ```
## Windows x86 Docker 问题排查

如果您在 Windows 环境下使用 x86 Docker 配置设置 `cosmo-edge`，请检查以下内容：

1. **Docker Desktop:** 确保 Docker Desktop 正在运行，且 Docker 引擎已完成启动。如果命令超时或报告守护进程错误，请在继续前检查 Docker Desktop 是否正在运行。

2. **Docker Compose V2:** 使用 Compose V2 命令格式 `docker compose`，而不是旧的 `docker-compose` 命令。

3. **端口 8080:** Windows x86 设置使用宿主机端口 `8080` 作为 Web 控制台。如果 Docker 报告端口绑定错误，请参阅 [端口冲突](#端口冲突) 了解 Windows 特定的检查方法和修改宿主机端口的说明。

4. **Windows x86 配置:** 启动 Windows x86 设置时使用 `docker-compose.x86.windows.yml`：

   ```powershell
   docker compose -f docker-compose.x86.windows.yml up -d --build
   ```

5. **Docker 日志:** 要查看 x86 Docker 日志，请运行：

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

如果端口被占用，可以修改 `docker-compose.x86.yml` 的主机端口，或停止占用端口的服务。

Windows 的 Hyper-V / WSL 可能保留一段 TCP 端口，即使 `netstat` 没显示监听进程，Docker 仍会报告端口绑定失败。可以先查看系统保留范围：

```powershell
netsh interface ipv4 show excludedportrange protocol=tcp
```

`docker-compose.x86.windows.yml` 支持通过 `COSMO_X86_WEB_PORT` 改 Web 主机端口，无需修改受版本控制的文件。例如使用 `8280`：

```powershell
$env:COSMO_X86_WEB_PORT = "8280"
docker compose -f docker-compose.x86.windows.yml up -d --build
```

随后访问 `http://127.0.0.1:8280`。不设置该变量时仍默认使用 `8080`。

Mac Preview 使用相同的 Web 端口变量，但仍只绑定本机：

```bash
COSMO_X86_WEB_PORT=8280 ./scripts/macos-docker-preview.sh up
```

Mac 上若构建速度异常慢，请同时检查 Docker Desktop 的 VMM 与 Rosetta 设置；
完整边界见 [macOS Docker Preview](./macos-docker-preview.md)。

## Windows 构建脚本提示 `No such file or directory`

如果 Docker 构建在执行 `configure`、`config` 或 `Configure` 时报告文件存在但无法执行，通常是 Git for Windows 将无扩展名脚本检出为 CRLF，导致容器无法识别 shebang。

仓库根目录的 `.gitattributes` 会把自动识别出的文本文件（包括这些无扩展名脚本）固定为 LF。拉取最新规则后，请在没有未保存修改的全新 clone 或干净 worktree 中重试。可以用以下命令确认规则：

```powershell
git check-attr text eol -- 3rd/mp4v2-2.0.0/configure 3rd/openssl-3.5.3/config 3rd/srs-6.0-r0/trunk/configure
```

三个文件都应显示 `text: auto` 和 `eol: lf`。

## `build_output/` 没有构建产物

使用完整运行命令：

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

Sophon 的完整构建入口、profile 和输出约定以[构建指南](./build.md#sophon-构建产物)为准。
例如，默认 BM1688 Open 构建完成后可直接检查芯片目录：

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package
cat build_output/public-runtime/bm1688/TARGET_CHIP
(cd build_output/public-runtime/bm1688 && sha256sum -c SHA256SUMS)
```

Sophon 产物不会直接写在 `build_output/` 根目录。每个
`build_output/<profile>/<chip>/` 目录应包含 `TARGET_CHIP`、`SHA256SUMS` 和唯一的
`cosmo-V<version>-<32位md5>.tar.gz`。先确认检查的是本次选择的 profile 和芯片目录。

注意：不要使用 `docker compose build` 代替上述 `run` 入口；前者不会执行导出产物的
容器命令。

## Sophon 构建失败

`cosmo-sophon-package` 服务直接使用 `docker-compose.sophon.yml` 中配置的预构建 GHCR
镜像，仓库没有 `Dockerfile.sophon` 本地构建路径。镜像和构建链路的当前事实统一见
[构建指南](./build.md#sophon-构建产物)。

如果构建失败，请重新运行同一入口并检查末尾日志：

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x 2>&1 | tail -50
```

检查 BM1688 构建时把末尾型号改为 `bm1688`，或省略型号。

常见问题：

- 无法拉取预构建 GHCR 镜像或填充 npm 缓存——检查 Docker registry 网络、代理、DNS 和当前构建日志。
- 磁盘空间不足 — 构建过程需要约 3GB 空间。
- `COSMO_MODEL_GUARD_BUILD_PROFILE` 取值不受支持——只接受
  `public-runtime` 和 `production-release`。
- 芯片型号不受支持——只接受 `bm1688` 和 `cv186x`；省略时默认 `bm1688`。
- 在非受控发布环境选择 `production-release`——缺少正式 SDK、设备初始化、
  信任身份、签发者或发布引导输入时按设计拒绝构建。普通源码修改应使用
  默认 Open（内部配置 `public-runtime`），不要绕过正式发布检查。

## 受保护 preset 无法加载

设备只需要以下一个 Guard 状态文件：

```text
/data/cwaiuserdata/model-guard/device-certificate.bin
```

先检查证书状态和服务日志：

```bash
sudo test -f /data/cwaiuserdata/model-guard/device-certificate.bin
sudo journalctl -u cosmo.service -b --no-pager -n 200
```

如果受控 provisioner 仍在设备的临时目录，还可以运行
`sudo /临时目录/cosmo-model-provision status` 直接校验证书和本机绑定；Open
包本身不提供该工具。

- `-2001`（`CMG_V2_CERTIFICATE_UNAVAILABLE`）：证书文件不存在或无法读取。
- `-2002`（`CMG_V2_CERTIFICATE_REJECTED`）：证书损坏、签名无效，或证书不是
  为本机签发。

不要生成逐模型 license，也不要复制另一台设备的证书。使用本机生成的新请求在
受控离线环境重新签发证书，再执行
`cosmo-model-provision install --certificate <证书绝对路径>`。Open 安装器
不会创建、删除或修复该证书。

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

## 软件升级后页面一直等待

升级期间设备会离线，页面会等待新的 Linux `bootId`，最长显示 15 分钟。如果重启清空登录会话，页面会在“已观察到离线”且新服务返回鉴权响应后进入登录页。这个交互超时不会取消设备端升级；重新登录后仍需核对软件版本。

在 Sophon 设备上检查：

```bash
systemctl status cosmo --no-pager -l
journalctl -u cosmo -b --no-pager -n 200
stat -c '%F %a %U:%G %n' /data/cwaiuserdata/upload/sessions
```

正常情况下 `cosmo.service` 应为 `active (running)`，暂存根目录应是真实目录并保持 `0700`。如果启动日志出现致命初始化异常，进程会返回非零状态并由 `Restart=on-failure` 重试。不要通过递归放宽整个 `/data/cwaiuserdata` 的权限来规避检查。

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

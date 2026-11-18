---
title: 构建指南
description: x86 Docker、Sophon、Rockchip 和 CPU 测试构建路径。
prev:
  text: 文档首页
  link: /
next:
  text: RK3576 / RKNN 集成
  link: /guide/rk3576-rknn-development
---

# 构建指南

先确认**宿主环境**（在哪台电脑执行命令），再选择**目标平台**（产物在哪里运行）。
本文记录当前仓库的构建入口；只执行与你的宿主和目标相符的一组命令。

## 构建路径总览

| 使用目的 | 宿主环境 | 目标平台 | 入口与结果 |
| --- | --- | --- | --- |
| 本地体验 | Linux x86_64 | x86 CPU | [Linux Compose](#linux-x86-64)，启动服务；`build_output/` |
| 本地体验 | Windows x86_64 / Docker Desktop | x86 CPU | [Windows Compose](#windows-x86-64)，启动服务；`build_output/` |
| 本地体验 | Apple Silicon macOS | amd64 仿真 | [Mac Preview](#apple-silicon-macos-preview)，启动服务；`build_output/macos-x86/` |
| 构建安装包 | Linux x86_64 或 Windows x86_64 / PowerShell | BM1688 | [Sophon BM1688](#bm1688)；`build_output/public-runtime/bm1688/` |
| 构建安装包 | Linux x86_64 或 Windows x86_64 / PowerShell | CV186X | [Sophon CV186X](#cv186x)；`build_output/public-runtime/cv186x/` |
| 构建安装包 | Linux x86_64 / Bash | RK3576 | [Rockchip RK3576](#rk3576)；`build_output/rk3576/` |
| 构建安装包 | Linux x86_64 / Bash | RV1126B | [Rockchip RV1126B](#rv1126b)；`build_output/rv1126b/` |
| C++ 单元测试 | Linux x86_64 / 构建容器 | x86 CPU | [CPU 测试](#cpu-测试构建)；`build_cpu/cosmo-tests` |

## 公共准备

宿主机需要 Git、可用的 Docker 和对应 Compose 入口；Windows 使用 Docker Desktop
的 Linux 容器模式。Mac 先阅读 [Preview 的环境要求](./macos-docker-preview.md)。
首次构建需要访问镜像仓库和依赖源。交叉编译在宿主机的构建容器中进行，不在设备上编译。

首次获取源码（Bash 或 PowerShell）：

```sh
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
```

已有工作副本时直接进入其根目录，不必再次克隆。下文所有宿主机命令均从仓库根目录执行。
默认构建当前 `main`；需要正式版时，在构建前单独选择对应标签，例如：

```sh
git checkout v1.2.0
```

标签构建使用该标签的文档和脚本。包名中的版本由源码和 Git 状态决定，安装时使用构建
日志及 `SHA256SUMS` 中的实际文件名，不要套用教程里的旧版本号。

Linux 的 Compose 命令统一通过 `./scripts/docker-compose.sh` 执行，它自动选择 V2/V1，
并在账号无权访问 Docker daemon 时请求 sudo。Windows 命令使用 PowerShell；Sophon
PowerShell 脚本和 Mac Preview 需要 Compose V2。各节普通构建均按默认
Open（`public-runtime`）、`COSMO_PACKAGE_MODELS=include` 说明；若曾设置覆盖变量，
先恢复这些默认值。高级配置见[文末](#高级构建选项)。

## x86 Docker 开发运行环境

用于本地体验和开发，使用 ONNX Runtime CPU 后端。以下三个入口按宿主环境任选一个。

### Linux x86_64

在 Linux 宿主机的 Bash 中运行：

```bash
./scripts/docker-compose.sh -f docker-compose.x86.yml up -d --build
./scripts/docker-compose.sh -f docker-compose.x86.yml ps
```

### Windows x86_64

在已启动 Docker Desktop 的宿主机 PowerShell 中运行：

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
docker compose -f docker-compose.x86.windows.yml ps
```

### Apple Silicon macOS Preview

在 Mac 终端执行准入检查，通过后再启动：

```bash
./scripts/macos-docker-preview.sh doctor && ./scripts/macos-docker-preview.sh up
./scripts/macos-docker-preview.sh status
```

Mac 使用 `linux/amd64` 仿真和独立卷，只绑定回环地址；源码变化后需要显式运行
`./scripts/macos-docker-preview.sh up --build`。完整限制见 [macOS Docker Preview](./macos-docker-preview.md)。

### 启动结果与数据

确认容器运行且浏览器可打开 `http://127.0.0.1:8080`。Linux/Windows 的发布包导出到
`build_output/`，Mac 导出到 `build_output/macos-x86/`。容器运行数据和模型资源分别保存
在命名卷中；停止、升级与数据保留见[部署指南](./deployment.md#x86-docker-运行环境)。

`Dockerfile.x86` 调用 `scripts/build_cpu.sh`，主要参数为：

| 参数 | 值 |
| --- | --- |
| `COSMO_TARGET_ARCH` | `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | `OFF` |
| `COSMO_NN_USE_CPU_BACKEND` | `ON` |
| `COSMO_ENABLE_OPENH264` | `ON` |
| `COSMO_DEV_MODE` | `ON` |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` |

这条路径不启用 Model Guard；本地 CPU 或 Mac 仿真结果不代表目标 NPU 的性能。

## Sophon 构建产物

为已准备好 Sophon Linux 基础系统和运行依赖的设备生成 Open 安装包。先选芯片，再选
宿主环境。用户无需传入模型路径，脚本会选择对应资源目录。以下命令只构建和导出，不启动设备服务。

### BM1688

**Linux x86_64 / Bash：**

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml \
  run --rm cosmo-sophon-package --chip bm1688
```

**Windows x86_64 / PowerShell：**

```powershell
.\scripts\build_sophon_package.ps1 -Chip bm1688
```

产物目录：`build_output/public-runtime/bm1688/`。

### CV186X

**Linux x86_64 / Bash：**

```bash
./scripts/docker-compose.sh -f docker-compose.sophon.yml \
  run --rm cosmo-sophon-package --chip cv186x
```

**Windows x86_64 / PowerShell：**

```powershell
.\scripts\build_sophon_package.ps1 -Chip cv186x
```

产物目录：`build_output/public-runtime/cv186x/`。设备身份和首次检测见
[CV186X 快速开始](./cv186x-quick-start.md)。省略芯片参数时脚本默认 BM1688；上面的示例显式选型。

### 将 Sophon 构建包交给部署流程

成功时目标目录包含唯一的 `cosmo-V<版本号>-<32位md5>.tar.gz`、`TARGET_CHIP` 和
`SHA256SUMS`，包内还有 `share/cosmo/target-chip.txt`。必须核对实际设备与芯片标记；
即使部分模型字节相同，也不能跨芯片复用完整包。

**Linux / Bash 校验**（本例选择 BM1688；CV186X 将 `chip` 改为 `cv186x`）：

```bash
chip=bm1688
(cat "build_output/public-runtime/${chip}/TARGET_CHIP" &&
 cd "build_output/public-runtime/${chip}" && sha256sum -c SHA256SUMS)
```

**Windows / PowerShell 校验**（同样只选择一个芯片）：

```powershell
$chip = 'bm1688'
$outputDir = Join-Path 'build_output/public-runtime' $chip
if ((Get-Content -LiteralPath (Join-Path $outputDir 'TARGET_CHIP') -Raw).Trim() -ne $chip) {
    throw 'Target chip mismatch'
}
$checksumLines = @(Get-Content -LiteralPath (Join-Path $outputDir 'SHA256SUMS'))
if ($checksumLines.Count -ne 1 -or $checksumLines[0] -notmatch '^([0-9a-fA-F]{64})\s+\*?(cosmo-V[0-9]+\.[0-9]+\.[0-9]+-[0-9a-fA-F]{32}\.tar\.gz)$') {
    throw 'Expected one package checksum'
}
$expectedHash = $Matches[1]
$packageName = $Matches[2]
$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $outputDir $packageName)).Hash
if ($actualHash -ne $expectedHash) { throw 'SHA-256 mismatch' }
Write-Output "Verified $chip package: $packageName"
```

校验通过后，按[部署指南的 SSH 安装路径](./deployment.md#ssh-安装路径)或 Web 升级流程
使用这个实际包名。构建同时生成 `build/cosmo-tests`（aarch64），但不执行它；测试通过和
设备业务验收必须分别取得执行证据。

## Rockchip 构建产物

以下路径在 **Linux x86_64 宿主机的 Bash、仓库根目录**执行，为 RK3576 或 RV1126B
生成 aarch64 Open 安装包。本节不提供 Windows 或 macOS 交叉编译入口。

先拉取仓库固定 digest 的共享构建镜像，此步骤只需对本次使用的镜像执行一次：

```bash
./scripts/docker-compose.sh -f docker-compose.rockchip.yml pull cosmo-rockchip-package
```

再选择以下一个芯片构建。

### RK3576

```bash
COSMO_TARGET_CHIP=rk3576 ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
```

成功后在宿主机校验，确认标记为 `rk3576` 且 SHA-256 检查通过：

```bash
cat build_output/rk3576/TARGET_CHIP
(cd build_output/rk3576 && sha256sum -c SHA256SUMS)
```

产物目录：`build_output/rk3576/`。需要 Protected 时见[RK3576 Protected 构建](#rk3576-protected-构建)。RKLLM Runtime v1.3.0 及许可证为此包的强制依赖；
Qwen3.5 模型不随 Open 包分发，模型与运行时约定见 [RK3576 / RKNN 集成指南](./rk3576-rknn-development.md)。

### RV1126B

```bash
COSMO_TARGET_CHIP=rv1126b ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
```

成功后在宿主机校验，确认标记为 `rv1126b` 且 SHA-256 检查通过：

```bash
cat build_output/rv1126b/TARGET_CHIP
(cd build_output/rv1126b && sha256sum -c SHA256SUMS)
```

产物目录：`build_output/rv1126b/`。默认 `include` 从平台清单自动生成并验证模型 overlay，
无需手动准备。默认清单中的两个 AGPL-3.0 社区示例模型仅用于公开示例和 CI，不属于商业
模型交付；包内保留 `resource/model-bundle.json` 和许可证。此包不包含 RKLLM。

### 产物与安装

每个芯片目录包含唯一安装包、`TARGET_CHIP`、`MEDIA_RUNTIME_PROFILE` 和 `SHA256SUMS`。
校验后使用[统一 SSH 安装入口](./deployment.md#ssh-安装路径)，从所选芯片目录取包。
Rockchip 的默认持久化数据根为 `/userdata/cwaiuserdata`；基础系统和运行依赖须事先就绪。

构建器在 `linux/amd64` 容器中清理 `build_rknn/`，调用 `scripts/build_rknn.sh -c <chip> -T`。
同时生成 `build_rknn/cosmo-tests`、`cosmo-rknn-backend-smoke` 和 `cosmo-rknn-fastpath-qualify`
三个 aarch64 程序，但不执行它们，也不启动应用。构建和包审计不能替代设备验收。

## CPU 测试构建

适用于 **Linux x86_64 宿主机**。在仓库根目录使用与 CPU CI 相同的构建镜像；
它提供 CMake、编译器、Rust、pkg-config 和 OpenH264 等依赖。与 aarch64 的 `build/`
保持隔离，CPU 产物位于 `build_cpu/`。

### 构建测试程序

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/workspace" -w /workspace \
  -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/workspace \
  -e RUSTUP_HOME=/root/.rustup -e CARGO_HOME=/root/.cargo \
  ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_x86:v1 \
  bash -c 'bash scripts/build_cpu_test.sh'
```

成功结果是 `build_cpu/cosmo-tests`；脚本启用 CPU 后端和 `BUILD_TESTS=ON`，不运行测试、不生成安装包。

### 执行测试

构建成功后，仍在同一仓库根目录执行。以下运行库路径与 CPU CI 一致：

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/workspace" -w /workspace \
  ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_x86:v1 \
  bash -c '
    export LD_LIBRARY_PATH="\
/workspace/prebuild/ffmpeg/x86_64/lib:\
/workspace/3rd/onnxruntime-linux-x64-1.26.0/lib:\
/workspace/build_cpu/thirdparty_install/openssl/lib:\
/workspace/build_cpu/thirdparty_install/curl/lib:\
/workspace/build_cpu/thirdparty_install/event/lib:\
/workspace/build_cpu/thirdparty_install/glog/lib:\
/workspace/build_cpu/thirdparty_install/mp4v2/lib:\
/workspace/build_cpu/thirdparty_install/uuid/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    ./build_cpu/cosmo-tests
  '
```

以测试汇总和退出码判断结果。CPU 测试通过不代表 Sophon 或 Rockchip 真机行为通过。

## 高级构建选项

### 各平台 Open 与 Protected

| 目标平台 | Open（`public-runtime`） | Protected（`production-release`） |
| --- | --- | --- |
| BM1688 | 支持 | 支持，使用 Sophon Guard SDK |
| CV186X | 支持 | 支持，使用 Sophon Guard SDK |
| RK3576 | 支持 | 支持，使用 RKNN Guard SDK |
| RV1126B | 支持 | 不支持，配置与包审计均拒绝 |
| x86 / macOS Preview | CPU 开发模式 | 不支持 Model Guard |

Open 使用明文模型，无需设备授权，也不包含授权工具。Protected 使用对应平台的
Guard 运行时和 `cosmo-model-provision`，受保护模型需要设备授权。三个受支持目标的
Protected 产物均位于 `build_output/production-release/<chip>/`；Open 目录见上方各平台小节。

两种配置均使用 `cosmo-V<版本号>-<32位md5>.tar.gz` 和同一升级流程；应用包不签名。
Protected 的 CPack 包可直接用于管理页面升级，不需要离线应用签名步骤。
包中模型必须符合所选配置；SDK 不会把默认明文模型自动转换成加密模型。

### Sophon Protected 构建

当前授权 fork 在 `prebuild/model-guard-v2/` 中提供完整 Sophon Guard SDK，包含 runtime、
头文件、配套授权工具和 `SDK-MANIFEST.json`。两种配置默认使用它，只有显式设置
`COSMO_MODEL_GUARD_SDK_ROOT` 才会覆盖；该路径必须在容器内可访问。
`build_output/model-guard-sdk-production/` 不再自动优先。

在 Linux Bash 中构建 **CV186X Protected 应用更新包**的示例；不携带模型权重：

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
COSMO_PACKAGE_MODELS=preserve \
  ./scripts/docker-compose.sh -f docker-compose.sophon.yml \
  run --rm cosmo-sophon-package --chip cv186x
```

目标为 BM1688 时把 `--chip` 值改为 `bm1688`。Protected 在编译前校验完整 SDK 清单和
runtime、授权工具配套哈希，整包审计再次检查。默认 `include` 要求预设模型已经加密，
完整 SDK 不会自动加密模型。上述 `preserve` 示例保留已安装模型，不能用于空白设备初始化
或模型交付验收。需要随包交付已准备好的加密模型时，将策略改为 `COSMO_PACKAGE_MODELS=include`。

完整 SDK 的入库授权仅适用于当前 fork，不构成向其他位置发布或广泛重授权。
Guard 设备证书和模型加密秘密仍是受控输入，不得写入公开仓库。

### RK3576 Protected 构建

宿主环境为 Linux x86_64 / Bash，使用同一 Rockchip Compose 入口。默认 SDK 为
`prebuild/model-guard-v2-rknn-abi1/`（Model Guard v2.4 RKNN ABI 1），包含对应头文件、
`libcosmo_model_guard_rknn.so.1.0.0`、授权工具和清单。构建前校验 SDK，打包后检查运行时、
授权工具和模型策略；不能用 Sophon SDK 替代。

下面是**保留设备已有模型、仅更新应用**的构建命令。该包部署后依赖设备已有的授权和模型；编译时无需连接设备：

```bash
COSMO_TARGET_CHIP=rk3576 \
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
COSMO_PACKAGE_MODELS=preserve \
  ./scripts/docker-compose.sh -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
```

构建成功后校验，并从此目录取包：

```bash
cat build_output/production-release/rk3576/TARGET_CHIP
(cd build_output/production-release/rk3576 && sha256sum -c SHA256SUMS)
```

该示例不携带模型，不用于空白设备初始化或模型交付验收。需要随包交付模型时使用
`COSMO_PACKAGE_MODELS=include`，并通过 `COSMO_RKNN_MODELS_DIR` 和
`COSMO_RKNN_RESOURCE_OVERLAY_DIR` 提供匹配 RK3576 的加密模型及资源目录；这些路径必须
在容器内可访问。仓库默认明文 `.rknn` 模型不能直接用于 Protected 包，包审计会拒绝。

`COSMO_MODEL_GUARD_SDK_ROOT` 可覆盖 RKNN SDK 路径，但仍必须通过同样的配套校验。
此 SDK 的 fork 授权和发布边界以其随附说明为准；构建支持不代表本次已完成设备验收。
RV1126B 当前不能通过改芯片参数得到 Protected 包。

### 构建镜像、缓存与自定义模型

Sophon 使用 `docker-compose.sophon.yml` 中的
`ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_sophon:v1`。入口脚本调用
`scripts/build.sh -T -c <chip>`；Windows PowerShell 入口先同步到 Linux 卷并恢复 `.so` 软链接。

Sophon 与 Rockchip 在同一工作目录中共享 Compose npm 缓存。首次按锁文件串行填充，
随后离线安装；删除缓存卷会重新下载。构建服务使用宿主机网络解析依赖，不发布应用端口。

Rockchip 镜像的 builder lock 必须与源码 checkout 一致。RK3576 与 RV1126B 使用隔离的
MPP/RGA 根目录；RV1126B sysroot 由源码版本、ELF 属性和文件哈希封印，构建路径映射避免
临时目录进入二进制。包审计检查固定上游 MPP Apache-2.0/MIT 与 RGA `COPYING` 许可证。
旧 `docker-compose.rk3576.yml` 仅保留为薄兼容入口。

RV1126B 商业或专有模型通过 `COSMO_RKNN_ARTIFACT_MANIFEST` 指定独立清单，并接受相同的
芯片、大小和哈希审计；清单及材料存放在任务私有目录，必须挂载为容器可访问路径。
`preserve` 不携带模型权重，不能初始化空白设备，也不能凭构建结果替代带模型的设备验收。

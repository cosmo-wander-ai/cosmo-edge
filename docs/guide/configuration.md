---
title: 运行配置
description: 当前 Compose、运行脚本和 CMake 中可确认的环境变量、目录、端口及构建选项。
prev:
  text: 部署指南
  link: /guide/deployment
next:
  text: 故障排查
  link: /guide/troubleshooting
---

# 运行配置

本文只记录当前仓库可以从 Docker Compose、Dockerfile、CMake 和启动脚本确认的配置。
平台构建入口和完整命令见[构建指南](./build.md)，安装与升级见[部署指南](./deployment.md)。

## Docker Compose 入口

| 用途 | 文件 / 入口 | 服务 |
| --- | --- | --- |
| x86 Linux | `docker-compose.x86.yml` | `cosmo-x86` |
| x86 Windows | `docker-compose.x86.windows.yml` | `cosmo-x86` |
| Apple Silicon macOS Preview | `scripts/macos-docker-preview.sh` → `docker-compose.x86.macos.yml` | `cosmo-x86-macos` |
| Sophon 发布包 | `docker-compose.sophon.yml` | `cosmo-sophon-package` |
| Rockchip 发布包 | `docker-compose.rockchip.yml` | `cosmo-rockchip-package` |

优先使用 `scripts/docker-compose.sh` 调用一次性构建服务；它会选择当前可用的 Compose V2/V1。

## x86 Docker 配置

`Dockerfile.x86` 的主要构建参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` | 安装到运行镜像的资源目录 |
| `COSMO_BUILD_JOBS` | 未设置时由构建脚本使用 `nproc` | CPU 构建并行度；Mac Preview 默认覆盖为 `1` |

运行镜像设置：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `INSTALLPATH` | `/appfs/cosmo_wander/cwai_data` | 主安装目录 |
| `COSMO_PLATFORM_TYPE` | `x86_64` | 运行平台类型 |

Windows 和 macOS Preview 支持以下宿主机覆盖：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `COSMO_X86_WEB_PORT` | `8080` | Web 主机端口；Mac 仍只绑定 `127.0.0.1` |
| `COSMO_X86_BUILD_JOBS` | `1`（仅 Mac Preview） | amd64 仿真构建并行度 |

`scripts/docker-entrypoint.x86.sh` 创建数据和日志目录后执行：

```bash
${INSTALLPATH}/scripts/run_start.sh start /data/cwaiuserdata/log/logs/INTE_RUN_container.log
```

## 管理平台签名凭据

向管理平台发送签名请求时，运行环境必须同时配置以下变量；变量值是凭据文件的绝对路径：

| 变量 | 说明 |
| --- | --- |
| `COSMO_APP_KEY_FILE` | App Key 文件 |
| `COSMO_APP_SECRET_FILE` | App Secret 文件 |

两个文件都必须是普通文件，大小不超过 4096 字节，并且只包含一行非空内容。建议以只读
方式挂载并限制权限，不要把实际凭据写入镜像、Compose 文件或仓库。两个变量均未设置时，
签名管理平台请求保持禁用；只配置一个变量、使用相对路径或无效文件时，请求会被拒绝。

## Sophon 构建配置

芯片型号通过 Compose 服务后的 `--chip <型号>` 传入：

| 参数 | 支持值 | 默认值 |
| --- | --- | --- |
| `--chip` | `bm1688`、`cv186x` | `bm1688` |

`docker-compose.sophon.yml` 向构建容器传入：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `COSMO_MODEL_GUARD_BUILD_PROFILE` | `public-runtime` | `public-runtime`（Open）或 `production-release`（Protected） |
| `COSMO_PACKAGE_MODELS` | `include` | `include` 或 `preserve`；公开可部署包使用 `include` |
| `NPM_CONFIG_MAXSOCKETS` | `1` | npm 最大并发连接数 |
| `NPM_CONFIG_PROGRESS` | `false` | 关闭 npm 进度输出 |
| `NPM_CONFIG_FETCH_RETRIES` | `3` | npm 拉取重试次数 |
| `NPM_CONFIG_FETCH_TIMEOUT` | `120000` | npm 拉取超时，单位毫秒 |
| `NPM_CONFIG_PREFER_OFFLINE` | `true` | 优先使用 Compose npm 缓存 |
| `NPM_CONFIG_UPDATE_NOTIFIER` | `false` | 关闭 npm 更新提示 |

该 Compose 服务直接使用预构建 GHCR 镜像，不再提供旧文档中的 apt、Node 或 Rustup
镜像下载变量。网络排查应关注 GHCR 镜像拉取、npm 缓存填充和当前构建日志。

## Rockchip 构建配置

| 变量 / 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `COSMO_TARGET_CHIP` | `rk3576` | `rk3576` 或 `rv1126b`；Compose 将它传给 `--chip` |
| `COSMO_PACKAGE_MODELS` | `include` | `include` 或仅用于代码/结构验证的 `preserve` |
| `COSMO_BUILD_JOBS` | `4` | 交叉构建并行度 |
| `COSMO_ROCKCHIP_BUILDER_IMAGE` | 仓库固定 digest 的 GHCR 镜像 | 受控覆盖构建镜像 |
| `COSMO_RKNN_ARTIFACT_MANIFEST` | 平台 profile 中的清单 | 选择带来源、哈希、用途和许可证的模型 bundle；商业/专有输入应指向 ignored 任务目录中的独立清单 |

构建器还使用与 Sophon 相同的 npm 缓存/重试变量。RV1126B 的 `include` 构建会从
选定 artifact manifest 生成并校验 `output/platform-artifacts/rv1126b/resource-overlay`；
`preserve` 只验证代码和包结构，不能替代设备验收。仓库默认清单是 AGPL-3.0
社区示例，不属于商业模型交付；商业/专有模型必须使用独立清单和许可记录。

## 资源目录

| 构建路径 | 资源目录 |
| --- | --- |
| x86 Docker | `data/resource/aiboxresource_x86` |
| Sophon BM1688 | `data/resource/aiboxresource_bm1688` |
| Sophon CV186X | `data/resource/aiboxresource_cv186x` |
| Rockchip RK3576 | `data/resource/aiboxresource_rknn` |
| Rockchip RV1126B | `data/resource/aiboxresource_rknn` 共享模板 + `model-artifacts/rv1126b` 社区示例清单；目标 overlay 自动生成 |

构建脚本把选定目录作为 `RESOURCE_DIR` 交给安装规则。不同芯片的二进制产物仍必须
独立生成和验证，但算法、配置模板及 staging 代码保持共享。模型包会携带
`resource/model-bundle.json` 及对应许可证，包审计同时校验模型清单、大小和 SHA-256。

## 运行目录

| 路径 | 说明 |
| --- | --- |
| `/appfs/cosmo_wander/cwai_data` | 默认应用安装目录 |
| `/data/cwaiuserdata` | 默认用户数据根目录 |
| `/data/cwaiuserdata/log/logs` | 应用日志 |
| `/data/cwaiuserdata/upgrade` | 升级暂存目录 |
| `/data/cwaiuserdata/tmp/*` | nginx 临时目录 |

设备部署可以通过 `COSMO_APP_DATA_DIR` 和 `COSMO_DATA_DIR` 覆盖应用/数据根目录；覆盖值必须是
受控的绝对路径，并与服务配置和持久化策略一致。

## 端口

| 端口 | 说明 |
| --- | --- |
| `8080` | x86 Docker 默认 Web 主机端口 |
| `80` | 容器内 nginx |
| `8000` | 后端 HTTP；x86 Compose 对主机只发布同号 UDP 设备发现端口 |
| `9000` | 后端 WebSocket；通常经 nginx 访问 |
| `1936` | SRS RTMP |
| `1985` | SRS API |
| `18088` | SRS HTTP stream |

## 流媒体变量

`scripts/run_start.sh` 设置以下默认值：

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

macOS Preview 覆盖 `COSMO_STREAM_PLAY_MODE=httpflv-srs`，并继续使用 `18088` 播放 HTTP-FLV。

## CMake Cache 设置

以下值可通过 `-D<名称>=<值>` 配置，但推荐通过仓库构建脚本选择兼容组合：

| 名称 | 类型 / 默认值 | 说明 |
| --- | --- | --- |
| `COSMO_TARGET_ARCH` | `STRING` / `aarch64` | `aarch64` 或 `x86_64` |
| `COSMO_TARGET_CHIP` | `STRING` / 空 | 记录目标芯片；后端脚本传入受支持值 |
| `BUILD_TESTS` | `BOOL` / `OFF` | 构建 `cosmo-tests` |
| `COSMO_ENABLE_COVERAGE` | `BOOL` / `OFF` | 为测试构建启用 gcov |
| `COSMO_DEV_MODE` | `BOOL` / `OFF` | 关闭看门狗等生产行为，并启用开发日志输出 |
| `COSMO_NN_USE_SOPHON_BACKEND` | `BOOL` / `ON` | Sophon 推理后端 |
| `COSMO_NN_USE_CPU_BACKEND` | `BOOL` / `OFF` | ONNX Runtime CPU 后端 |
| `COSMO_NN_USE_RKNN_BACKEND` | `BOOL` / `OFF` | Rockchip RKNN 后端 |
| `COSMO_MEDIA_USE_SOPHON_BACKEND` | `BOOL` / 派生默认 | Sophon 媒体后端 |
| `COSMO_MEDIA_USE_CPU_BACKEND` | `BOOL` / 派生默认 | FFmpeg 软件媒体后端 |
| `COSMO_MEDIA_USE_ROCKCHIP_BACKEND` | `BOOL` / 派生默认 | Rockchip MPP/RGA 媒体后端 |
| `COSMO_ENABLE_OPENH264` | `BOOL` / 派生默认 | x86 CPU 媒体路径默认开启，可显式配置 |
| `COSMO_MODEL_GUARD_BUILD_PROFILE` | `STRING` / `public-runtime` | `public-runtime` 或 `production-release` |
| `COSMO_PACKAGE_MODELS` | `STRING` / `include` | `include` 或 `preserve` |

三个 NN 后端必须且只能启用一个；三个媒体后端也必须且只能启用一个。Sophon 媒体后端要求
Sophon 推理运行时，Rockchip 媒体后端要求 aarch64 目标。

以下为自动派生或固定内部值，不应由调用者覆盖：

| 名称 | 当前行为 |
| --- | --- |
| `COSMO_OPENH264_USE_ASM` | 固定为 `OFF` |
| `COSMO_MODEL_GUARD` | Sophon 推理后端为 `ON`，其他推理后端为 `OFF` |

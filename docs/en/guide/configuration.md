---
title: Runtime Configuration
description: Environment variables, directories, ports, and build settings confirmed by the current Compose, runtime scripts, and CMake files.
prev:
  text: Deployment Guide
  link: /en/guide/deployment
next:
  text: Troubleshooting
  link: /en/guide/troubleshooting
---

# Runtime Configuration

This page includes only settings confirmed by the current Docker Compose, Dockerfile, CMake, and startup scripts.
See the [Build Guide](./build.md) for complete build entry points and the [Deployment Guide](./deployment.md)
for installation and upgrade.

## Docker Compose Entry Points

| Purpose | File / entry point | Service |
| --- | --- | --- |
| x86 Linux | `docker-compose.x86.yml` | `cosmo-x86` |
| x86 Windows | `docker-compose.x86.windows.yml` | `cosmo-x86` |
| Apple Silicon macOS Preview | `scripts/macos-docker-preview.sh` → `docker-compose.x86.macos.yml` | `cosmo-x86-macos` |
| Sophon package | `docker-compose.sophon.yml` | `cosmo-sophon-package` |
| Rockchip package | `docker-compose.rockchip.yml` | `cosmo-rockchip-package` |

Prefer `scripts/docker-compose.sh` for one-shot build services. It selects an available Compose V2/V1 implementation.

## x86 Docker Settings

Main `Dockerfile.x86` build arguments:

| Argument | Default | Description |
| --- | --- | --- |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` | Resource directory installed into the runtime image |
| `COSMO_BUILD_JOBS` | The build script uses `nproc` when unset | CPU build parallelism; Mac Preview overrides it to `1` by default |

The runtime image sets:

| Variable | Default | Description |
| --- | --- | --- |
| `INSTALLPATH` | `/appfs/cosmo_wander/cwai_data` | Main installation directory |
| `COSMO_PLATFORM_TYPE` | `x86_64` | Runtime platform type |

Windows and macOS Preview support these host overrides:

| Variable | Default | Description |
| --- | --- | --- |
| `COSMO_X86_WEB_PORT` | `8080` | Host web port; Mac remains bound to `127.0.0.1` |
| `COSMO_X86_BUILD_JOBS` | `1` (Mac Preview only) | Build parallelism under amd64 emulation |

`scripts/docker-entrypoint.x86.sh` creates data and log directories, then executes:

```bash
${INSTALLPATH}/scripts/run_start.sh start /data/cwaiuserdata/log/logs/INTE_RUN_container.log
```

## Management-Platform Signing Credentials

Signed requests to the management platform require both variables below. Each value is an absolute credential-file path:

| Variable | Description |
| --- | --- |
| `COSMO_APP_KEY_FILE` | App Key file |
| `COSMO_APP_SECRET_FILE` | App Secret file |

Both files must be regular files, no larger than 4096 bytes, with one non-empty line. Mount them read-only with
restricted permissions; never place credentials in images, Compose files, or the repository. When both variables
are absent, signed management-platform requests remain disabled. A partial setting, relative path, or invalid file is rejected.

## Sophon Build Settings

Pass the chip after the Compose service as `--chip <model>`:

| Argument | Supported values | Default |
| --- | --- | --- |
| `--chip` | `bm1688`, `cv186x` | `bm1688` |

`docker-compose.sophon.yml` passes these values into the build container:

| Variable | Default | Description |
| --- | --- | --- |
| `COSMO_MODEL_GUARD_BUILD_PROFILE` | `public-runtime` | `public-runtime` (Open) or `production-release` (Protected) |
| `COSMO_PACKAGE_MODELS` | `include` | `include` or `preserve`; public deployable packages use `include` |
| `NPM_CONFIG_MAXSOCKETS` | `1` | Maximum npm connections |
| `NPM_CONFIG_PROGRESS` | `false` | Disable npm progress output |
| `NPM_CONFIG_FETCH_RETRIES` | `3` | npm fetch retries |
| `NPM_CONFIG_FETCH_TIMEOUT` | `120000` | npm fetch timeout in milliseconds |
| `NPM_CONFIG_PREFER_OFFLINE` | `true` | Prefer the Compose npm cache |
| `NPM_CONFIG_UPDATE_NOTIFIER` | `false` | Disable the npm update notifier |

This Compose service uses a prebuilt GHCR image and no longer exposes the apt, Node, or Rustup mirror
variables described by older guides. Diagnose GHCR pulls, npm-cache population, and the current build log instead.

## Rockchip Build Settings

| Variable / argument | Default | Description |
| --- | --- | --- |
| `COSMO_TARGET_CHIP` | `rk3576` | `rk3576` or `rv1126b`; Compose passes it to `--chip` |
| `COSMO_PACKAGE_MODELS` | `include` | `include` or `preserve`, which is only for code/structure validation |
| `COSMO_BUILD_JOBS` | `4` | Cross-build parallelism |
| `COSMO_ROCKCHIP_BUILDER_IMAGE` | Repository-pinned GHCR digest | Controlled builder-image override |
| `COSMO_RKNN_ARTIFACT_MANIFEST` | Manifest from the platform profile | Selects a model bundle with source, hash, usage, and license identity; commercial or proprietary inputs use a separate manifest in an ignored task directory |

The builder also uses the npm cache/retry variables listed for Sophon. An RV1126B `include` build generates
and verifies `output/platform-artifacts/rv1126b/resource-overlay` from the selected artifact manifest;
`preserve` validates only code and package structure and is not device acceptance. The repository default is
an AGPL-3.0 community example bundle, not a commercial model deliverable. Commercial or proprietary models
require an independent manifest and license record.

## Resource Directories

| Build path | Resource directory |
| --- | --- |
| x86 Docker | `data/resource/aiboxresource_x86` |
| Sophon BM1688 | `data/resource/aiboxresource_bm1688` |
| Sophon CV186X | `data/resource/aiboxresource_cv186x` |
| Rockchip RK3576 | `data/resource/aiboxresource_rknn` |
| Rockchip RV1126B | Shared `data/resource/aiboxresource_rknn` templates plus the `model-artifacts/rv1126b` community example manifest; the target overlay is generated |

The build scripts pass the selected path as `RESOURCE_DIR` to the install rules. Binary models for different
chips still require separate conversion and validation, while algorithm/config templates and staging code remain
shared. Packages retain `resource/model-bundle.json` and the applicable license, and package audit verifies model
inventory, size, and SHA-256.

## Runtime Directories

| Path | Description |
| --- | --- |
| `/appfs/cosmo_wander/cwai_data` | Default application installation directory |
| `/data/cwaiuserdata` | Default user-data root |
| `/data/cwaiuserdata/log/logs` | Application logs |
| `/data/cwaiuserdata/upgrade` | Upgrade staging directory |
| `/data/cwaiuserdata/tmp/*` | nginx temporary directories |

Device deployments may override the application and data roots with `COSMO_APP_DATA_DIR` and `COSMO_DATA_DIR`.
Overrides must be controlled absolute paths consistent with the service and persistence policy.

## Ports

| Port | Description |
| --- | --- |
| `8080` | Default x86 Docker host web port |
| `80` | nginx inside the container |
| `8000` | Backend HTTP; x86 Compose publishes only same-number UDP device discovery directly to the host |
| `9000` | Backend WebSocket; normally accessed through nginx |
| `1936` | SRS RTMP |
| `1985` | SRS API |
| `18088` | SRS HTTP stream |

## Stream Variables

`scripts/run_start.sh` sets these defaults:

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

macOS Preview overrides `COSMO_STREAM_PLAY_MODE=httpflv-srs` and continues to use `18088` for HTTP-FLV playback.

## CMake Cache Settings

These values can be configured with `-D<name>=<value>`, but repository build scripts should select compatible combinations:

| Name | Type / default | Description |
| --- | --- | --- |
| `COSMO_TARGET_ARCH` | `STRING` / `aarch64` | `aarch64` or `x86_64` |
| `COSMO_TARGET_CHIP` | `STRING` / empty | Records the target chip; backend scripts pass supported values |
| `BUILD_TESTS` | `BOOL` / `OFF` | Build `cosmo-tests` |
| `COSMO_ENABLE_COVERAGE` | `BOOL` / `OFF` | Enable gcov for the test build |
| `COSMO_DEV_MODE` | `BOOL` / `OFF` | Disable watchdog and related production behavior and enable development log output |
| `COSMO_NN_USE_SOPHON_BACKEND` | `BOOL` / `ON` | Sophon inference backend |
| `COSMO_NN_USE_CPU_BACKEND` | `BOOL` / `OFF` | ONNX Runtime CPU backend |
| `COSMO_NN_USE_RKNN_BACKEND` | `BOOL` / `OFF` | Rockchip RKNN backend |
| `COSMO_MEDIA_USE_SOPHON_BACKEND` | `BOOL` / derived default | Sophon media backend |
| `COSMO_MEDIA_USE_CPU_BACKEND` | `BOOL` / derived default | FFmpeg software media backend |
| `COSMO_MEDIA_USE_ROCKCHIP_BACKEND` | `BOOL` / derived default | Rockchip MPP/RGA media backend |
| `COSMO_ENABLE_OPENH264` | `BOOL` / derived default | Enabled by default for x86 CPU media and explicitly configurable |
| `COSMO_MODEL_GUARD_BUILD_PROFILE` | `STRING` / `public-runtime` | `public-runtime` or `production-release` |
| `COSMO_PACKAGE_MODELS` | `STRING` / `include` | `include` or `preserve` |

Exactly one NN backend and exactly one media backend must be enabled. Sophon media requires the Sophon runtime;
Rockchip media requires an aarch64 target.

These values are derived or fixed internally and should not be overridden by callers:

| Name | Current behavior |
| --- | --- |
| `COSMO_OPENH264_USE_ASM` | Always `OFF` |
| `COSMO_MODEL_GUARD` | `ON` for the Sophon inference backend, `OFF` for other inference backends |

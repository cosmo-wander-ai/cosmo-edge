---
title: Runtime Configuration
description: Environment variables, resource directories, ports, and data paths that can be confirmed in the current repository.
prev:
  text: Deployment Guide
  link: /en/guide/deployment
next:
  text: Troubleshooting
  link: /en/guide/troubleshooting
---

# Runtime Configuration

This page consolidates the runtime configuration entries that can be confirmed in the current repository. Configuration sources include Docker Compose, Dockerfile, CMake, startup scripts, and application constants.

## Docker Compose Configuration

x86 development runtime:

- Linux: `docker-compose.x86.yml`
- Windows: `docker-compose.x86.windows.yml`

Sophon release package build:

```text
docker-compose.sophon.yml
```

## x86 Docker Environment Variables

Set in `Dockerfile.x86`:

| Variable | Default | Description |
| --- | --- | --- |
| `INSTALLPATH` | `/appfs/cosmo_wander/cwai_data` | Main installation directory (can be overridden as needed) |
| `COSMO_PLATFORM_TYPE` | `x86_64` | Platform type |

`scripts/docker-entrypoint.x86.sh` ensures the runtime directory exists and executes:

```bash
${INSTALLPATH}/scripts/run_start.sh start ${DATADIR}/log/logs/INTE_RUN_container.log
```

## Manager Signing Credentials

Signed requests to the manager no longer use built-in application credentials. Configure both variables below with absolute paths to credential files:

| Variable | Description |
| --- | --- |
| `COSMO_APP_KEY_FILE` | App Key file |
| `COSMO_APP_SECRET_FILE` | App Secret file |

Each path must reference a regular file no larger than 4096 bytes containing exactly one non-empty line. Mount the files read-only with restricted permissions; do not store the credential values in an image, Compose file, or repository.

When both variables are absent, signed manager requests remain disabled. A partial configuration, relative path, or invalid file also rejects the request. Local web and device APIs are unaffected.

## Sophon Build Variables

`docker-compose.sophon.yml` supports the following build arguments:

| Variable | Default | Description |
| --- | --- | --- |
| `SOPHON_APT_MIRROR` | `https://mirrors.aliyun.com/ubuntu` | apt mirror |
| `SOPHON_NODE_DIST_BASE_URL` | `https://npmmirror.com/mirrors/node` | Node download mirror |
| `SOPHON_RUSTUP_INIT_URL` | `https://rsproxy.cn/rustup-init.sh` | rustup-init download URL |
| `SOPHON_RUSTUP_DIST_SERVER` | `https://rsproxy.cn` | Rust dist server |
| `SOPHON_RUSTUP_UPDATE_ROOT` | `https://rsproxy.cn/rustup` | Rust update root |

## Resource Directories

| Build Path | Resource Directory |
| --- | --- |
| x86 Docker | `data/resource/aiboxresource_x86` |
| Sophon package | `data/resource/aiboxresource` |

CMake installs resources via `RESOURCE_DIR`.

## Runtime Directories

| Path | Description |
| --- | --- |
| `<INSTALLPATH>` | Main installation directory, set by the `INSTALLPATH` environment variable in the Dockerfile |
| `<DATADIR>` | User data, by default located on a persistent volume |
| `<DATADIR>/log/logs` | Logs |
| `<DATADIR>/upgrade` | Upgrade packages |
| `<DATADIR>/tmp/*` | nginx temporary directories |

## Ports

| Port | Description |
| --- | --- |
| `8080` | x86 Docker host access to the web console |
| `80` | nginx inside the container |
| `8000` | Backend HTTP |
| `9000` | Backend WebSocket |
| `1936` | SRS RTMP |
| `1985` | SRS API |
| `18088` | SRS HTTP stream |

## Stream Variables

Set by `scripts/run_start.sh`:

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## CMake Key Options

User-configurable cache options (set with `-D<option>=<value>`):

| Option | Description |
| --- | --- |
| `COSMO_TARGET_ARCH` | `aarch64` or `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | Enable the Sophon backend (mutually exclusive with `COSMO_NN_USE_CPU_BACKEND`) |
| `COSMO_NN_USE_CPU_BACKEND` | Enable the CPU/ONNX Runtime backend (mutually exclusive with `COSMO_NN_USE_SOPHON_BACKEND`) |
| `COSMO_DEV_MODE` | Development mode |
| `BUILD_TESTS` | Build the test suite |

Derived variables (set automatically by the build system, not to be set directly):

| Variable | Description |
| --- | --- |
| `COSMO_ENABLE_OPENH264` | Enable OpenH264 under the CPU backend |
| `COSMO_OPENH264_USE_ASM` | Enable ASM for OpenH264 under the CPU backend |
| `COSMO_MODEL_GUARD` | Model guard integration |

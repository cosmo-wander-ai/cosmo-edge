---
title: Build Guide
description: Confirmed build paths for x86 Docker, Sophon, Rockchip, CPU tests, and docs.
prev:
  text: Documentation Home
  link: /en/
next:
  text: RK3576 / RKNN Integration
  link: /en/guide/rk3576-rknn-development
---

# Build Guide

This page documents build paths that are confirmed and available in the repository.

> **💡 Docker Compose Version Note**
> This documentation uses the latest Docker Compose V2 command format (`docker compose`). If you are using an older Docker environment, please replace `docker compose` with the hyphenated `docker-compose` in all commands.
> On Linux, `./scripts/docker-compose.sh` detects Compose V2/V1 and requests
> `sudo` once when the current account cannot access the Docker daemon.

## Build Path Overview

| Target | Entry Point | Notes |
| --- | --- | --- |
| x86 Docker runtime | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | Starts the containerized development/runtime environment. |
| macOS Docker Preview | `scripts/macos-docker-preview.sh` | Runs the one-video x86 workflow under amd64 emulation on Apple Silicon. |
| Sophon Open package | `./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package` | Cross-compiles the installable public source-build package. |
| Rockchip package | `docker compose -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package` | Cross-compiles RK3576 or RV1126B with one locked RKNN builder. |
| CPU test build | `scripts/build_cpu_test.sh` | Builds `cosmo-tests` for x86 CPU validation. |

## x86 Docker Development Runtime

These entry points are from:

- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
- `docker-compose.x86.macos.yml` (Apple Silicon macOS Preview)
- `Dockerfile.x86`
- `scripts/build_cpu.sh`

Confirmed CMake parameters:

| Parameter | Value |
| --- | --- |
| `COSMO_TARGET_ARCH` | `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | `OFF` |
| `COSMO_NN_USE_CPU_BACKEND` | `ON` |
| `COSMO_ENABLE_OPENH264` | `ON` |
| `COSMO_DEV_MODE` | `ON` |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` |

Linux:

```bash
docker compose -f docker-compose.x86.yml up -d --build
docker compose -f docker-compose.x86.yml ps
```

Windows (PowerShell/CMD):

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
docker compose -f docker-compose.x86.windows.yml ps
```

Apple Silicon macOS (Preview):

```bash
./scripts/macos-docker-preview.sh doctor
./scripts/macos-docker-preview.sh up
```

The Mac path explicitly runs `linux/amd64`, uses isolated volumes, and publishes
only on loopback. It does not enable Model Guard and is not native arm64 or NPU
performance evidence. See [macOS Docker Preview](./macos-docker-preview.md) for
the complete setup and acceptance boundary.

After build:

- Web console available at `http://127.0.0.1:8080`.
- Release packages and build artifacts exported to `build_output/`.
- Runtime data stored in Docker volume `cosmo-x86-data`.
- Resource directory mounted to Docker volume `cosmo-x86-app-resource`.

## Sophon Artifacts

The public entry point defaults to
`COSMO_MODEL_GUARD_BUILD_PROFILE=public-runtime`:

```bash
# Defaults to bm1688 when the chip model is omitted
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package

# Select a chip model explicitly
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip bm1688
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

Windows PowerShell:

```powershell
# Defaults to bm1688 when the chip model is omitted
.\scripts\build_sophon_package.ps1

# Select a chip model explicitly
.\scripts\build_sophon_package.ps1 -Chip bm1688
.\scripts\build_sophon_package.ps1 -Chip cv186x
```

The two supported profiles are deliberately isolated:

| Profile | Intended use | Output directory | Deployment status |
| --- | --- | --- | --- |
| Open (`public-runtime`, default) | Public aarch64 compile, link, package, and test validation using the tracked runtime SDK | `build_output/public-runtime/<chip>/` | Plain models; no device authorization required |
| Protected (`production-release`) | Controlled build with the complete production SDK and provisioning tool | `build_output/production-release/<chip>/` | Encrypted models; device authorization required |

Every chip directory also contains `TARGET_CHIP` and `SHA256SUMS`, while the
archive contains `share/cosmo/target-chip.txt`. Even when the selected public
model bytes match, complete packages for different chips must have different
hashes. Always take the archive from its chip-scoped directory.

On the first build, Compose fills the npm cache serially from `package-lock.json`
and then installs fully offline. BM1688, CV186X, and RK3576 builds in the same
working directory share that cache. This avoids an npm 10.2 failure mode where
many CDN sockets remain open indefinitely. Removing the Compose volume refills it.

Both profiles produce `cosmo-V<version>-<32-char-md5>.tar.gz`. The same format
can be uploaded through the management page on a main-branch installation and
on every later version. Application archives are not signed. The profiles differ
only in model protection and availability of `cosmo-model-provision`.

### Hand a Sophon Build to the Deployment Workflow

After the build, verify the target marker and SHA-256 in the chip-scoped output
directory:

```bash
chip=bm1688 # or cv186x
cat "build_output/public-runtime/${chip}/TARGET_CHIP"
(cd "build_output/public-runtime/${chip}" && sha256sum -c SHA256SUMS)
```

Use the [Deployment Guide](./deployment.md#ssh-installation-path) as the single
reference for SSH installation, web upgrade, recovery boundaries, and
post-reboot version acceptance. This guide does not duplicate device installation
commands, so the build entry point and deployment workflow cannot drift apart.

Maintainers use one command in a controlled environment containing the complete
Guard SDK and provisioning tool:

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
  ./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

This example builds a CV186X Protected package. Use `bm1688`, or omit the chip
model, for BM1688.

The Protected build fails immediately if the controlled SDK does not contain
`cosmo-model-provision`.
Stage the controlled production SDK under the host path
`build_output/model-guard-sdk-production/`. The existing Compose volume exposes
that ignored directory to the container, and Protected builds select it
automatically. Open builds remain unchanged.

The Protected CPack artifact is itself the upgrade archive accepted by the web
management page. No offline application-signing step is required. Guard device
certificates and model-encryption secrets remain controlled inputs and must never
be placed in the public repository.

This path is from:

- `scripts/docker-compose.sh` (Linux/macOS: selects Compose V2 or V1 and handles Docker access)
- `docker-compose.sophon.yml`
- `scripts/build_sophon_package.sh`
- `scripts/build_sophon_package.ps1` (Windows: restores `.so` symlinks before building)
- `scripts/build.sh`

Confirmed behavior:

- Base image uses the pre-built GHCR image: `ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_sophon:v1` (unified build environment, speeding up local start time).
- Docker Compose accepts a chip model argument: `cosmo-sophon-package --chip bm1688`
  or `cosmo-sophon-package --chip cv186x`. Omitting `--chip` defaults to `bm1688`.
- `scripts/build_sophon_package.sh` passes the chip model to
  `scripts/build.sh -T -c <model>`. `build.sh` then selects the matching resource
  directory; users do not provide a model path.
- Exports build artifacts only (does not start services).
- The chip model does not change CPack or MD5 renaming. Profile outputs remain
  under `build_output/<profile>/<chip>/`, with package names in the existing
  `cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz` format.

## Rockchip Artifacts

The Rockchip entry uses one digest-pinned GHCR image. It keeps one aarch64
toolchain and RKNN Runtime while selecting isolated MPP/RGA roots for RK3576
and RV1126B. RKLLM Runtime v1.3.0 is pinned to an official commit, but it is
required and packaged only for RK3576:

```bash
./scripts/docker-compose.sh -f docker-compose.rockchip.yml pull cosmo-rockchip-package

COSMO_TARGET_CHIP=rk3576 ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
sha256sum build_output/rk3576/cosmo-*.tar.gz

COSMO_TARGET_CHIP=rv1126b ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
sha256sum build_output/rv1126b/cosmo-*.tar.gz
```

Confirmed behavior:

- Runs the aarch64 cross-build in a `linux/amd64` build container.
- Removes `build_rknn/` before routing both chips through
  `scripts/build_rknn.sh -c <chip> -T`.
- Rejects a builder whose embedded lock does not exactly match the checkout.
- Seals the RV1126B MPP/RGA sysroot to its source revisions, ELF properties,
  and hashes. Reproducible path mapping keeps temporary workspaces out of MPP.
- Carries the MPP Apache-2.0/MIT and RGA `COPYING` texts from the pinned
  upstream commits in RV1126B packages and enforces them through target policy.
- Requires RKLLM and its license in RK3576 packages and forbids them in
  RV1126B packages.
- Exports the package, `TARGET_CHIP`, `MEDIA_RUNTIME_PROFILE`, and `SHA256SUMS`
  under `build_output/<chip>/` without starting application services.
- Also builds the aarch64 `build_rknn/cosmo-tests`,
  `cosmo-rknn-backend-smoke`, and `cosmo-rknn-fastpath-qualify` programs.
- Uses host networking to resolve build dependencies but publishes no
  application ports.

An RV1126B `include` build generates and verifies its overlay from the artifact
manifest selected by the platform profile. The repository default archives two
AGPL-3.0 community example models for public examples and CI only; it is not a
commercial model deliverable. The package retains `resource/model-bundle.json`
and the model license. Commercial or proprietary models set
`COSMO_RKNN_ARTIFACT_MANIFEST` to an independent manifest in an ignored task
directory and receive the same chip, size, and hash audit. `preserve` still
validates code, toolchain, and package structure only and is not model/device
acceptance. `docker-compose.rk3576.yml` remains a thin compatibility entry.

See [RK3576 / RKNN Integration](./rk3576-rknn-development.md) for the supported
release profile, runtime selection, model contract, and device-evidence boundary.

## CPU Test Build

```bash
bash scripts/build_cpu_test.sh
```

This script configures CMake with the CPU backend and `BUILD_TESTS=ON`, producing:

```sh
build_cpu/cosmo-tests
```

Useful for smoke testing C++ compilation and packaging logic without a target edge device.

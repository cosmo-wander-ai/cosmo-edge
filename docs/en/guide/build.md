---
title: Build Guide
description: Confirmed build paths for x86 Docker, Sophon release packages, CPU test builds, and docs.
prev:
  text: Documentation Home
  link: /en/
next:
  text: Deployment Guide
  link: /en/guide/deployment
---

# Build Guide

This page documents build paths that are confirmed and available in the repository.

## Build Path Overview

| Target | Entry Point | Notes |
| --- | --- | --- |
| x86 Docker runtime | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | Starts the containerized development/runtime environment. |
| Sophon release package | `scripts/build_sophon_package.sh` | Creates the target-device release package. |
| CPU test build | `scripts/build_cpu_test.sh` | Builds `cosmo-tests` for x86 CPU validation. |
| Documentation site | `npm ci` and `npm run docs:build` | Builds this VitePress site. |

## x86 Docker Development Runtime

These entry points are from:
- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
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

After build:
- Web console available at `http://127.0.0.1:8080`.
- Release packages and build artifacts exported to `build_output/`.
- Runtime data stored in Docker volume `cosmo-x86-data`.
- Resource directory mounted to Docker volume `cosmo-x86-app-resource`.

## Sophon Release Package

```bash
bash scripts/build_sophon_package.sh
```

Windows PowerShell:

```powershell
.\scripts\build_sophon_package.ps1
```

This path is from:
- `docker-compose.sophon.yml`
- `Dockerfile.sophon`
- `scripts/build_sophon_package.sh`
- `scripts/build_sophon_package.ps1`
- `scripts/build.sh`

Confirmed behavior:
- Default base image: `stream_dev:0.2`.
- Builds with `scripts/build.sh -m data/resource/aiboxresource`.
- Exports the release package only (does not start services).
- Package output under `build_output/`.

Common override variables:

```bash
SOPHON_STREAM_DEV_TAR=/path/to/stream_dev_22.04.tar bash scripts/build_sophon_package.sh
SOPHON_BASE_IMAGE=stream_dev:0.2 bash scripts/build_sophon_package.sh
```

If `stream_dev:0.2` is not available locally, the helper script attempts to download and load `stream_dev_22.04.tar` via `dfss`.

## CPU Test Build

```bash
bash scripts/build_cpu_test.sh
```

This script configures CMake with the CPU backend and `BUILD_TESTS=ON`, producing:

```
build_cpu/cosmo-tests
```

Useful for smoke testing C++ compilation and packaging logic without a target edge device.

## Documentation Build

```bash
npm ci
npm run docs:build
```

The build output is generated under `docs/.vitepress/dist` and should not be committed.

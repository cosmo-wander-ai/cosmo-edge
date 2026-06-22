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

This page documents build paths that are visible in the repository. Commands marked TODO require maintainer confirmation before the first public release.

## Build Path Overview

| Target | Entry Point | Notes |
| --- | --- | --- |
| x86 Docker runtime | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | Starts the containerized development/runtime environment. |
| Sophon release package | `scripts/build_sophon_package.sh` | Creates the target-device release package. |
| Windows CPU build | `scripts/build_cpu_windows.ps1` | Local build path used during validation. |
| Documentation site | `npm ci` and `npm run docs:build` | Builds this VitePress site. |

## x86 Docker Development Runtime

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

If ports or mounted paths differ in your environment, update `docker-compose.x86.yml` (or `docker-compose.x86.windows.yml` on Windows) and document the final values in the deployment guide.

## Sophon Release Package

```bash
bash scripts/build_sophon_package.sh
```

The package output location and target-device installation steps must be verified by maintainers before publication.

## CPU Test Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_cpu_windows.ps1
```

This path is useful for smoke testing C++ compilation and packaging logic without a target edge device.

## Documentation Build

```bash
npm ci
npm run docs:build
```

The build output is generated under `docs/.vitepress/dist` and should not be committed.

## Unconfirmed Paths

- Public binary release naming.
- Final target-device installation command.
- Whether every bundled model/resource is distributable in the first open-source release.

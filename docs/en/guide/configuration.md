---
title: Runtime Configuration
description: Docker, Sophon, resource, port, stream, and CMake configuration notes.
prev:
  text: Deployment Guide
  link: /en/guide/deployment
next:
  text: Troubleshooting
  link: /en/guide/troubleshooting
---

# Runtime Configuration

This page summarizes configuration surfaces visible in the repository.

## Docker Compose

`docker-compose.x86.yml` (Linux) and `docker-compose.x86.windows.yml` (Windows) define the x86 runtime container setup. Check mounted paths, exposed ports, image names, and persistent data directories against your environment.

## Sophon Build Variables

Sophon packaging depends on target SDK, runtime libraries, resource files, and packaging scripts. Confirm the final SDK version and compatible device models before publication.

## Resource Directories

| Path | Purpose |
| --- | --- |
| `data/resource/aiboxresource` | Target-device resources and templates. |
| `data/resource/aiboxresource_x86` | x86-oriented resources and templates. |
| `prebuild/` | Prebuilt components that need license review. |

## Ports

Keep port values consistent across deployment scripts, nginx config, SRS config, and user-facing documentation.

## Stream Settings

Verify RTSP/RTMP/WebRTC examples before publishing. Replace private camera URLs with safe examples such as `rtsp://example.com/stream`.

## CMake Options

Important options include CPU/Sophon backend selection, ONNX Runtime integration, and model guard integration. Document release defaults once they are stable.


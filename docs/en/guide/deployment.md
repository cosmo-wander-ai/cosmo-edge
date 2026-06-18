---
title: Deployment Guide
description: Runtime directories, processes, ports, package layout, and production deployment notes.
prev:
  text: Build Guide
  link: /en/guide/build
next:
  text: Runtime Configuration
  link: /en/guide/configuration
---

# Deployment Guide

CosmoEdge is deployed as a runtime package that includes the native backend, web assets, media services, scripts, and resource files.

## x86 Docker Runtime

```bash
docker compose up -d
docker compose ps
docker compose logs --tail=100
```

Use this path for local validation and integration work before preparing target-device packages.

## Runtime Directory

| Path | Purpose |
| --- | --- |
| `bin/` | Native executables. |
| `scripts/` | Start/stop and deployment helper scripts. |
| `web/` | Frontend assets served by nginx or the package web server. |
| `data/` | Runtime resources, templates, and model metadata. |
| `logs/` | Runtime logs. |

## Runtime Processes

| Process | Role |
| --- | --- |
| `cosmo-engine` | Main CosmoEdge backend process. |
| `nginx` | Serves the web console and static assets. |
| `srs` | Streaming media service. |

## Default Ports

| Port | Purpose |
| --- | --- |
| `80` or configured HTTP port | Web console. |
| `1936` | RTMP/SRS related service, depending on deployment. |
| `1985` | SRS HTTP API, depending on deployment. |
| `18088` | Backend or management API, depending on deployment. |

Verify the final port list against the runtime configuration used in your release package.

## Production Notes

- Do not publish default credentials.
- Review every sample stream URL and device serial number.
- Confirm model/resource distribution permission.
- Document the final service manager, whether systemd, Docker Compose, or target-device scripts.

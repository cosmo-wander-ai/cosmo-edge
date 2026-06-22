---
title: Architecture Overview
description: High-level architecture of the backend, frontend, services, inference, and resource system.
prev:
  text: Troubleshooting
  link: /en/guide/troubleshooting
next:
  text: API Overview
  link: /en/reference/api
---

# Architecture Overview

CosmoEdge combines a native C++ backend, a Vue-based web console, media services, resource templates, and model/runtime integrations into one edge AI application stack.

## Overall Structure

| Area | Main Paths | Purpose |
| --- | --- | --- |
| Backend | `src/` | Native services, APIs, media pipeline, inference, task logic. |
| Frontend | `src/web/` | Web console for configuration, monitoring, and operations. |
| Scripts | `scripts/` | Build, package, start/stop, validation, and scan helpers. |
| Runtime data | `data/` | Resource templates, API HTML, model/scenario metadata. |
| Third-party code | `3rd/` | Vendored dependencies that require license review. |
| Documentation | `docs/` | VitePress documentation site. |

## Backend Entry

The backend registers services, initializes runtime resources, starts HTTP/WebSocket/MQTT/media capabilities, and connects scenario tasks to inference and event output.

## Frontend

The frontend provides model management, scenario task configuration, live preview, alarm/event views, and system settings.

## Inference and Models

The runtime supports target-device inference paths and CPU/x86 validation paths. Model metadata and UI templates live under resource directories and must be reviewed before open-source publication.

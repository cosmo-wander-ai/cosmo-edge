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

```text
+---------------------------------------------------------------+
| Web Console                                                   |
| Vue 3 | Vite | Element Plus | Vue Flow | ECharts | i18n        |
+-------------------------------+-------------------------------+
                                | HTTP / WebSocket
                                v
+---------------------------------------------------------------+
| C++ Backend Runtime                                           |
| API Router | Services | Flow | Media | Inference | Database    |
+-------------------------------+-------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Runtime Package                                               |
| cosmo-engine | nginx | SRS | scripts | resources | data | fonts   |
+---------------------------------------------------------------+
```

## Backend Entry

| Entry | Purpose |
| --- | --- |
| `src/app/main.cc` | Creates `cosmo::app::Application`. |
| `src/app/application.cc` | Application startup shell. |
| `src/app/app_init.cc` | Service registration, initialization, network service startup. |
| `src/app/AppConstants.h` | Default HTTP / WebSocket ports. |

Primary executable target:

```text
cosmo-engine
```

## Service Registry

The backend assembles services through `cosmo::service::ServiceRegistry`. The startup sequence:

1. Register infrastructure services.
2. Register business services.
3. Initialize services.
4. Start MQTT, HTTP, WebSocket, device discovery, storage cleanup, watchdog, and other runtime services.

## Main Source Tree

| Directory | Purpose |
| --- | --- |
| `src/api` | API routing and message handlers |
| `src/app` | Application entry point and startup |
| `src/db` | DAO and database support |
| `src/flow` | Tasks, algorithms, action chains |
| `src/infer` | Model inference wrappers |
| `src/linkage` | Alarm linkage |
| `src/media` | Video decode, encode, frame processing, OSD |
| `src/mem` | Memory pool and device memory abstraction |
| `src/network` | HTTP, MQTT, network messages |
| `src/nn` | Inference backend abstraction |
| `src/platform` | Platform-specific capabilities |
| `src/service` | Service interfaces and implementations |
| `src/util` | General utilities |
| `src/web` | Vue 3 frontend |

## Frontend

Frontend source path:

```text
src/web
```

Confirmed stack:
- Vue 3
- Vite 6
- Vue Router 4
- Element Plus
- Axios
- ECharts
- Vue I18n
- Vue Flow

The frontend build output is installed into the release package's `web` directory.

## Inference and Models

Two inference backend paths exist:

- x86 CPU backend using ONNX Runtime.
- Sophon backend for aarch64/Sophon release packages.

Resource directories:
- `data/resource/aiboxresource`
- `data/resource/aiboxresource_x86`

Templates include YOLO, DINO, SAM2, Qwen3/Qwen3VL, keypoints, feature, and classification types. Model weights and resources may require separate distribution confirmation.


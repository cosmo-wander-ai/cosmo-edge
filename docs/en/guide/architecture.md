---
title: Architecture Overview
description: Overview of the current C++ backend, Vue frontend, service registry, API, media, and inference modules.
prev:
  text: Troubleshooting
  link: /en/guide/troubleshooting
next:
  text: API Overview
  link: /en/reference/api
---

# Architecture Overview

The current CosmoEdge repository is the complete product runtime, not just a single inference library or backend program.

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
| `src/app/main.cc` | Creates `cosmo::app::Application` |
| `src/app/application.cc` | Application startup shell |
| `src/app/app_init.cc` | Service registration, initialization, network service startup |
| `src/app/AppConstants.h` | Default HTTP / WebSocket ports |

Primary executable target:

```text
cosmo-engine
```

## Service Registry

The backend assembles services through `cosmo::service::ServiceRegistry`.

The startup sequence is split into:

1. Register infrastructure services.
2. Register business services.
3. Initialize services.
4. Start runtime services such as MQTT, HTTP, WebSocket, device discovery, storage cleanup, and the watchdog.

## Main Source Tree

| Directory | Purpose |
| --- | --- |
| `src/api` | API routing and message handlers |
| `src/app` | Application entry point and startup flow |
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

Frontend path:

```text
src/web
```

Confirmed technology stack:

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

Two inference backend paths currently exist in the project:

- x86 CPU backend, using ONNX Runtime.
- Sophon backend, used for aarch64/Sophon release packages.

Resource directories:

- `data/resource/aiboxresource`
- `data/resource/aiboxresource_x86`

The current templates cover detection (YOLO v5/v8/v9/v11/v12/v26), classification, keypoints, feature, segmentation (SAM2), object localization (DINO), and vision-language models (Qwen3VL, Qwen3.5). The complete list is subject to the actual files under `data/resource/aiboxresource/model_template/` and `data/resource/aiboxresource_x86/model_template/`.

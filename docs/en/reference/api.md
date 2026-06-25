---
title: API Overview
description: API categories, route entry points, and the packaged API document entry, verified from the current HTTP and MQTT-facing source.
prev:
  text: Architecture Overview
  link: /en/guide/architecture
next:
  text: API Fields
  link: /en/reference/api-fields
---

# API Overview

This page only documents the API categories and entry points that can be verified from the current source tree. For field-level interface details, continue with [API Fields](api-fields.md), [MQTT Reference](mqtt.md), and [HTTP Webhook Reference](webhook.md).

## Route Entry Points

The backend API routes are centralized in:

```text
src/api/ApiRouter.cc
src/api/ApiRouterRoutes.cc
```

The main management APIs are located under:

```text
/gtw/cwai/...
```

The core AI Host APIs are located under:

```text
/v1/cwai/aihost/...
/gtw/cwai/aihost/...
```

This group of routes is marked as `kNoAuth` (no authentication) and is registered in `RegisterCoreRoutes()` in `src/api/ApiRouter.cc`. There are 19 endpoints under `/v1/cwai/aihost/`:

```text
InterfaceTest             TaskCreate                TaskCancle
PTaskCreate               PTaskCancle               PTaskDetectPic
OperateNode               Info                      Probe
ViewRoutes                GraphicsMemory            OverviewStructrueRecord
LoadLocalAlgorithmAction  LogicTest                 QueryTaskOverviewFile
QueryTaskStatus           QueryTaskInfo             QueryDeviceMemStatus
QueryLogs
```

In addition, 3 compatibility routes are provided under `/gtw/cwai/aihost/` for the unified frontend prefix: `PTaskCreate`, `PTaskCancle`, and `PTaskDetectPic` (also `kNoAuth`).

## API Categories

| Category | Route Prefix | Description |
| --- | --- | --- |
| Login | `/gtw/cwai/login/` | Login and password change |
| Network | `/gtw/cwai/network/` | Network adapters, DNS, network quality, and connectivity checks |
| Algorithm | `/gtw/cwai/Algorithm/` | Algorithm pagination, upload, add, update, delete, and passenger-flow algorithm list |
| Algorithm layout | `/gtw/cwai/algorithm/layout/` | Algorithm layout save, detail, list, export single algorithm (`exportSingleAlg`, zip), and export all (`export`, tar.gz) |
| Atomic action | `/gtw/cwai/atomic/action/list` | Pipeline action list |
| Model management | `/gtw/cwai/atomic/Model/` | Model list, upload, configuration, import, delete, and export |
| Schedule | `/gtw/cwai/schedule/` | Schedule add, update, pagination, delete, and query |
| Event | `/gtw/cwai/Event/` | Event pagination, alarm export, and passenger-flow statistics |
| Camera | `/gtw/cwai/Camera/` | Camera CRUD, image capture, and USB camera list |
| Task | `/gtw/cwai/Task/` | Parameters, regions, policies, switches, batch operations, and run details |
| System | `/gtw/cwai/System/` | Device, time, image quality, recording, upgrade, logo, debug, and HTTP/MQTT parameters |
| Face gallery | `/gtw/cwai/Library/` | Face gallery and person image management |
| Body gallery | `/gtw/cwai/BodyLibrary/` | Body feature gallery management |
| Things gallery | `/gtw/cwai/ThingsLibrary/` | Things gallery management |
| File import | `/gtw/cwai/File/` | Import files and import status |
| Audio | `/gtw/cwai/Audio/` | Audio files, audio column devices, and testing |
| Linkage / alarm policy | `/gtw/cwai/AlarmStrage/` | Policy storage, CRUD, and switches |
| Live stream | `/gtw/cwai/LiveStream/` | Request live stream, keep-alive, and stop |

## Authentication

There are two kinds of markers in route registration: `kAuth` and `kNoAuth`. HTTP requests validate the `mtk` token; MQTT-dispatched requests go through the `ApiRouter` with an empty `mtk` and do not go through the HTTP token validation.

The public API documentation still needs to be supplemented with:

- Request and response fields of the login interface.
- Where the token is passed.
- The default account policy.
- Token expiration and error code descriptions.

## Response Header Fields

Most management responses inherit `MsgSendHead`:

| Field | Type | Description |
| --- | --- | --- |
| `resCode` | number | CWAI response code; `1` indicates success, `0` indicates failure |
| `resMsg` | object[] | Error or info message list |
| `resultCode` | string | ChinaMobile-compatible response code |
| `resultMsg` | string | ChinaMobile-compatible response message |

`MsgSendHead` itself does not carry business data; each concrete response message (each `*Send` subclass) additionally carries a `resData` business data container on top of `MsgSendHead`, whose structure varies by interface.

## WebSocket

The default WebSocket port:

```text
9000
```

The entry point is initialized by the event notifier:

```text
InitializeWebSocket("0.0.0.0", kDefaultWebSocketPort)
```

## Packaged API Documents

The repository still keeps the runtime-accessible HTML API documents:

```text
data/Interface/ai-box-interface_v1.0.html
data/Interface/mqtt_v1.0.html
```

After installation, static entries are generated:

```text
web/staticfile/httpInterface.html
web/staticfile/mqttInterface.html
```

The system interface also provides a document URL query:

| Type | Returned Path |
| --- | --- |
| `type = 0` | `/staticfile/httpInterface.html` |
| `type = 1` | `/staticfile/mqttInterface.html` |

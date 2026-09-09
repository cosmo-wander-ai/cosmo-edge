---
title: API Overview
description: API categories, route entry points, and the packaged API document entry, verified from the current HTTP and MQTT-facing source.
prev:
  text: Architecture Overview
  link: /en/guide/architecture
next:
  text: Image Detection API Integration
  link: /en/reference/image-detection-api
---

# API Overview

This page only documents the API categories and entry points that can be verified from the current source tree. For the complete upload and synchronous inference flow, continue with [Image Detection API Integration](image-detection-api.md). For other field-level details, see [API Fields](api-fields.md), [MQTT Reference](mqtt.md), and [HTTP Webhook Reference](webhook.md).

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

These routes are registered by `RegisterCoreRoutes()` in `src/api/ApiRouter.cc`. The minimal liveness endpoint `Probe` is `kNoAuth`; every other endpoint is `kAuth` and requires a valid `mtk` for HTTP calls. There are 19 endpoints under `/v1/cwai/aihost/`:

```text
InterfaceTest             TaskCreate                TaskCancle
PTaskCreate               PTaskCancle               PTaskDetectPic
OperateNode               Info                      Probe
ViewRoutes                GraphicsMemory            OverviewStructrueRecord
LoadLocalAlgorithmAction  LogicTest                 QueryTaskOverviewFile
QueryTaskStatus           QueryTaskInfo             QueryDeviceMemStatus
QueryLogs
```

In addition, 3 authenticated compatibility routes are provided under `/gtw/cwai/aihost/` for the unified frontend prefix: `PTaskCreate`, `PTaskCancle`, and `PTaskDetectPic`.

## API Categories

| Category | Route Prefix | Description |
| --- | --- | --- |
| Login | `/gtw/cwai/login/` | Login is anonymous; password changes require the header `mtk` and revoke every session for that user |
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

There are two kinds of markers in route registration: `kAuth` and `kNoAuth`. HTTP requests validate the `mtk` token. MQTT requests enter the same router with a trusted internal transport context only after the configured client connection and device registration have completed; HTTP `mtk` validation is not repeated for that transport.

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

## Resource-Aware Transfers

Model components, model archives, local videos, algorithm packages, upgrade packages, audio, face imports, and images use the same authenticated chunk protocol:

| Endpoint | Purpose |
| --- | --- |
| `POST /gtw/cwai/atomic/model/uploadCapabilities` | Query current device capabilities and safely usable storage |
| `POST /gtw/cwai/atomic/model/uploadTemp` | Upload one `multipart/form-data` chunk |
| `POST /gtw/cwai/atomic/model/cancelUpload` | Cancel a session and release its reservation immediately |

Byte counts in `uploadCapabilities` are returned as decimal strings. Important fields:

| Field | Meaning |
| --- | --- |
| `maxTotalSize` | Optional deployment-policy limit for a complete file; `0` means no product quota |
| `maxChunkSize` | Per-request chunk limit; 8 MB (8 × 1024 × 1024 bytes) by default |
| `maxChunks` | Optional deployment-policy limit for chunk count; `0` means no product quota |
| `availableBytes` | Currently available bytes on the staging filesystem before the safety reserve |
| `reserveBytes` | Effective disk safety reserve |
| `availableForNewUploadsBytes` | Bytes currently admissible after the safety reserve and in-flight reservations |
| `reservedBySessionsBytes` | Bytes reserved by in-flight upload sessions |
| `activeSessions` | Open or completed-but-not-yet-consumed upload sessions |
| `idleTimeoutMs` | Inactive session expiry; each accepted chunk refreshes it |
| `absoluteTimeoutMs` | Absolute lifetime; `0` means no absolute timeout |
| `resumable` | Whether idempotent resume is supported |
| `persistentAcrossRestart` | Whether sessions survive an engine restart |
| `maxEncodedImageBytes` | Encoded-image byte capability of the current media pipeline |
| `maxImagePixels` | Maximum decodable pixel count of the current media pipeline |

The default policy does not impose arbitrary total quotas by model, video, or image type. Admission is based on the target filesystem's live resources with a fixed `512 MB` disk safety reserve by default; the percentage reserve defaults to `0%`. Clients must query capabilities before an upload and must not turn a previously observed storage or image value into a product constant.

Current production defaults are centralized instead of defining separate fine-grained limits for each business type:

| Parameter | Default | Purpose |
| --- | --- | --- |
| Complete-file total / chunk count | `0` / `0` | No product quota; live resource admission still applies |
| Per-user / global concurrent sessions | `0` / `0` | No fixed session quota |
| Global in-flight reservation total | `0` | No fixed reservation quota |
| Upload chunk | `8 MB` | Bounds per-request memory and parsing work |
| Per-session metadata budget | `64 MB` | Prevents a malformed session from consuming unbounded memory |
| Idle / absolute timeout | `30 minutes` / `0` | Progress renews the session; no absolute lifetime |
| Restart persistence | enabled | Persists manifests and supports idempotent resume |
| Disk safety reserve | `512 MB` (percentage reserve defaults to `0%`) | Prevents uploads from exhausting the target filesystem; use the capability response's `reserveBytes` value |

The first chunk should carry a stable `clientRequestId`. The server returns an opaque `uploadId` and `nextChunkIndex`. After a disconnect or engine restart, resend chunk 0 for the same file with the same `clientRequestId`; the server returns the existing session and its next required chunk. Delete the local resume identity after completion or cancellation.

Control-plane JSON requests are limited to 1 MB by default. A regular single multipart request is limited to 10 MB, and the recommended upload chunk is 8 MB. MB values here use 1024 × 1024 bytes. These are per-request parsing and memory boundaries, not business-file size limits. A request beyond the boundary returns HTTP 413 with `HTTP_BODY_TOO_LARGE` and recommends either `USE_CHUNKED_UPLOAD` or `REDUCE_REQUEST_BODY`.

### Upgrade Recovery Status

The upgrade request accepts an `uploadId` whose original filename matches
`cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz`. Before reboot, the backend
validates the filename, MD5, archive safety, and package layout. After reboot,
the common startup script revalidates the MD5 and installs the package. Open and
Protected packages use the same application-upgrade protocol; model authorization
is independent.

`POST /gtw/cwai/System/QueryDeviceStatus` returns these fields on success:

| Field | Meaning |
| --- | --- |
| `resData.bootId` | Current Linux boot identity; the software-upgrade page uses it to confirm that a reboot actually completed |
| `resData.softwareVersion` | Version of the currently running CosmoEdge process |

These are backward-compatible additive fields. Older clients may ignore them; newer clients must not declare an upgrade successful only because the endpoint returns HTTP 200 again.

libevent also has a bounded 12 MB emergency receive backstop. It only protects requests that were not rejected earlier by the application boundary and is not a usable business-upload allowance. A low-level rejection may provide only a generic HTTP 413; the Web console converts that response into the same actionable guidance based on request type (use chunks for multipart, reduce the body for other requests). Third-party clients should still observe the 8 MB chunk, 1 MB JSON, and 10 MB regular multipart boundaries.

Image URL downloads derive their budget from current memory and media-frame capability instead of a fixed 16 MB threshold. HTTP video and other large-file retrieval streams directly to a file. Static media paths return the standard extension-derived MIME type (for example, `image/jpeg` for JPEG and `video/mp4` for MP4). Model and other managed-file exports are also file-streamed and support a single `Range`, `206 Partial Content`, and `416` for an unsatisfiable range.

`/gtw/cwai/atomic/model/exportConfig` returns a direct attachment for user-managed models that are marked exportable. Preset, encrypted, or device-bound models return `DefaultCantBeExport`; this is a model portability and security-policy boundary, not a file-size quota.

## Actionable Errors

In addition to the compatible `msgCode` and `msgText`, an item in `resMsg` can contain:

| Field | Meaning |
| --- | --- |
| `messageKey` | Frontend localization key |
| `details` | Machine-readable context such as `actualBytes`, `limitBytes`, `requiredBytes`, `availableBytes`, and `reserveBytes` |
| `retryable` | Whether the same operation can succeed after external conditions change |
| `retryAfterSeconds` | Suggested delay |
| `recommendedAction` | Next step the frontend should present or perform |

Primary transfer and media errors include `STORAGE_RESERVE_REACHED`, `TRANSFER_BUSY`, `UPLOAD_METADATA_BUDGET`, `HTTP_BODY_TOO_LARGE`, `IMAGE_INPUT_TOO_LARGE`, and `IMAGE_RESOLUTION_TOO_LARGE`. Frontends should display the returned actual value, limit, and recommended action instead of reducing every condition to a generic “upload failed” message.

The current console maps `recommendedAction` to visible guidance. Stable action codes include `FREE_DISK_SPACE`, `USE_CHUNKED_UPLOAD`, `REDUCE_REQUEST_BODY`, `USE_LARGER_CHUNKS`, `RETRY`/`RETRY_LATER`, `RESIZE_IMAGE`, `RESIZE_OR_RECOMPRESS_IMAGE`, `CHECK_UPLOAD_PARAMETERS`, `CHANGE_DEPLOYMENT_POLICY`, and `USE_LARGER_CHUNKS_OR_CHANGE_POLICY`. When `retryAfterSeconds` is also present, the console shows the suggested wait.

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

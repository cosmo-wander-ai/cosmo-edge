---
title: Backend Development
description: Backend architecture, build system, service layer and DI, API routing, testing, code style, and key module references.
prev:
  text: Frontend Development
  link: /en/development/frontend
next:
  text: CI and Quality Checks
  link: /en/development/ci
---

# Backend Development

The backend is a native C++17 application built with CMake. It provides runtime services, media processing, NN inference, API routing, event output, and scenario task orchestration — all linked into a single executable: `cosmo-engine`.

## Architecture Overview

The CMake build produces **object libraries** organized in layers. The final binary links them together:

```
Layer 0 (Foundation)    cosmo_util  cosmo_db  cosmo_platform  cosmo_mem
Layer 1 (Infrastructure)  cosmo_media  cosmo_infer  cosmo_network  cosmo_nn
Layer 2 (Business)      cosmo_flow  cosmo_service  cosmo_linkage
Layer 3 (Interface)     cosmo_api
```

Each object library maps to a source directory under `src/`. The layer order ensures that higher layers can depend on lower ones, but not vice versa.

## Build System

### Entry Points

| Script / File                     | Purpose                            |
| --------------------------------- | ---------------------------------- |
| `scripts/build_cpu.sh`            | x86 CPU backend build              |
| `scripts/build_cpu_test.sh`       | CPU test build (`cosmo-tests`)     |
| `scripts/build.sh`                | Sophon / aarch64 build             |
| `scripts/build_sophon_package.sh` | Sophon release package             |
| `CMakeLists.txt`                  | Root build and packaging           |

### CMake Options

| Option                          | Default    | Description                                    |
| ------------------------------- | :--------: | ---------------------------------------------- |
| `COSMO_TARGET_ARCH`             | `aarch64`  | Target architecture: `aarch64` or `x86_64`     |
| `COSMO_NN_USE_SOPHON_BACKEND`   | `ON`       | Enable Sophon TPU backend                      |
| `COSMO_NN_USE_CPU_BACKEND`      | `OFF`      | Enable CPU / ONNX Runtime backend              |
| `COSMO_ENABLE_OPENH264`         | auto       | Enable OpenH264 (ON when CPU backend selected) |
| `COSMO_DEV_MODE`                | `OFF`      | Disable watchdog and other production guards   |
| `COSMO_MODEL_GUARD`             | auto       | Link `libcosmo_model_guard.so` (Sophon default)|
| `BUILD_TESTS`                   | `OFF`      | Build `cosmo-tests` with Catch2 + gcov         |

`COSMO_NN_USE_SOPHON_BACKEND` and `COSMO_NN_USE_CPU_BACKEND` are mutually exclusive. Selecting CPU auto-enables `COSMO_ENABLE_OPENH264` and disables `COSMO_MODEL_GUARD`.

## Service Layer & Dependency Injection

### ServiceRegistry

All backend services are wired through `cosmo::service::ServiceRegistry`, a thread-safe DI container keyed by `std::type_index`.

```cpp
// Production registration (owning — destroyed in reverse order on ShutdownAll)
ServiceRegistry::Instance().Register<IFooService>(std::make_unique<FooServiceImpl>());

// Test / mock injection (non-owning)
ServiceRegistry::Instance().Set<IFooService>(&mockFoo);

// Resolution
auto& foo = ServiceRegistry::Instance().Get<IFooService>();
```

### Startup Sequence

Service initialization happens in `src/app/app_init.cc`, called from `SwDeviceInit()`, in four phases:

**Phase 1 — `RegisterInfrastructureServices()`**
Registers foundational services: `IFileService`, `IEventNotifier` (WebSocket), `IMemoryPoolService`, `IStorageCleanService`, `IWatchDogService`, `IDbService` (SQLite), `IOsdTextRenderer`, `IVideoFrameService`, `ITaskService`, `IInferPoolService`, `ILlmInferService`, `INetworkService`, `IDeviceDiscoveryService`, `IHttpClient`.

**Phase 2 — `RegisterBusinessServices()`**
Registers domain services: audio, linkage, camera, picture tasks, algorithms, device info, time (NTP), system config, model management, alarm records/push, auth, schedules, face/body/item libraries, live stream, actions, client messages, app info, timer restart.

**Phase 3 — `InitializeServices()`**
Calls `Init()` on services in dependency order — OSD fonts, algorithm service, network service, database, alarms, models, face data, camera entities, app info, MQTT start.

**Phase 4 — `InitializeExternalComponents()`**
Starts the HTTP server, WebSocket server, device discovery multicast, storage cleanup timer, and hardware watchdog (skipped in `COSMO_DEV_MODE`).

### Adding a New Service

1. Define an interface in the appropriate `src/service/<domain>/` subdirectory, inheriting from a base interface if one exists for the domain.
2. Implement the service class in the same domain directory.
3. Register it in the correct phase in `src/app/app_init.cc`:
   ```cpp
   ServiceRegistry::Instance().Register<IMyService>(std::make_unique<MyServiceImpl>());
   ```
   If the service exposes multiple narrow interfaces, use `Set<>()` for additional aliases:
   ```cpp
   ServiceRegistry::Instance().Set<IMyQuery>(&ServiceRegistry::Instance().Get<IMyService>());
   ```
4. Add the implementation file to `src/service/CMakeLists.txt`.

Tear-down is automatic — `SwDeviceDestroy()` calls `ServiceRegistry::ShutdownAll()`, which destroys owned services in reverse registration order.

## API Routing

### Route Registration

API routes are defined in `src/api/ApiRouter.cc` and `src/api/ApiRouterRoutes.cc`. Two `ApiRouter` instances are created: one for HTTP requests (`MessageFromHttp`) and one for MQTT-dispatched requests (`MessageFromMqtt`). Both share the same route table.

Routes use a macro pattern (defined in `ApiRouterInternal.h`):

```cpp
ROUTE("/gtw/cwai/System/QueryDeviceInfo", Mtk, system_handler_, System, QueryDeviceInfo)
```

This expands to a dispatch that:
1. Matches the URL (case-insensitive via `util::ToLower`).
2. Validates the `mtk` token if the route is marked `Mtk` (vs `None` for public routes).
3. Deserializes the request JSON into `NS::MsgQueryDeviceInfoSend`.
4. Calls the handler.
5. Serializes the response (`NS::MsgQueryDeviceInfoRecv`) back to JSON.

### Standard Response Envelope

Most management responses inherit from `MsgSendHead`:

| Field        | Type     | Notes                            |
| ------------ | -------- | -------------------------------- |
| `resCode`    | number   | `1` = success, `0` = failure     |
| `resMsg`     | object[] | Error / info message list        |
| `resData`    | object   | Business payload (varies by API) |
| `resultCode` | string   | Compatibility response code      |
| `resultMsg`  | string   | Compatibility response text      |

### Adding a New Route Group

1. Create a message handler class under `src/api/` (e.g., `MessageMyHandler.h/.cc`).
2. Define the Send/Recv structs and the handler method.
3. Add a `RegisterMyRoutes()` function that calls `ROUTE` or `ROUTE_CORE` for each endpoint.
4. Call `RegisterMyRoutes()` from the `ApiRouter` constructor.

### Route Groups

| Registration Function       | URL Prefix                            | Domain              |
| --------------------------- | ------------------------------------- | ------------------- |
| `RegisterCoreRoutes()`      | (login, interface list)               | Core / auth         |
| `RegisterNetworkRoutes()`   | `/gtw/cwai/Network/`                  | Network, DNS, NTP   |
| `RegisterAlgorithmRoutes()` | `/gtw/cwai/Algorithm/`                | Algorithms          |
| `RegisterModelRoutes()`     | `/gtw/cwai/atomic/Model/`             | Model repository    |
| `RegisterScheduleRoutes()`  | `/gtw/cwai/schedule/`                 | Time templates      |
| `RegisterEventRoutes()`     | `/gtw/cwai/Event/`                    | Events, alarms      |
| `RegisterCameraRoutes()`    | `/gtw/cwai/Camera/`                   | Cameras, USB        |
| `RegisterTaskRoutes()`      | `/gtw/cwai/Task/`                     | Scenario tasks      |
| `RegisterSystemRoutes()`    | `/gtw/cwai/System/`                   | Device, upgrade, diag |
| `RegisterLibraryRoutes()`   | `/gtw/cwai/Library/`, `/BodyLibrary/`, `/ThingsLibrary/` | Face, body, item libraries |
| `RegisterFileRoutes()`      | `/gtw/cwai/File/`                     | File import         |
| `RegisterAudioRoutes()`     | `/gtw/cwai/Audio/`                    | Audio files, devices |
| `RegisterLinkageRoutes()`   | `/gtw/cwai/AlarmStrage/`              | Alarm linkage       |
| `RegisterLiveStreamRoutes()`| `/gtw/cwai/LiveStream/`               | Live stream lifecycle |

## Testing

### Framework

Tests use [Catch2](https://github.com/catchorg/Catch2) with [Trompeloeil](https://github.com/rollbear/trompeloeil) for mocking. The test files live under `test/` and map one-to-one to service or component implementations.

### Build and Run

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests
```

### Mocking

The `ServiceRegistry::Set<T>(ptr)` non-owning injection is the primary mechanism for replacing real services with mocks in tests. A central mock header (`test/test_mock_services.h`) provides reusable mock implementations for common interfaces.

### Test File Naming

Test files follow the pattern `test_<component>.cc`, e.g.:
- `test_service_registry.cc` → tests `ServiceRegistry`
- `test_video_frame_service_impl.cc` → tests `VideoFrameServiceImpl`
- `test_api_router.cc` → tests `ApiRouter`

When adding a new service, add a corresponding test file with a Trompeloeil mock for each dependency.

## Code Style

The project follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) adapted for C++17. Full details are in `CODING_STYLE.md` at the repository root (Chinese). Key conventions:

| Category          | Convention                              | Example                      |
| ----------------- | --------------------------------------- | ---------------------------- |
| File naming       | PascalCase, `.h` / `.cc`                | `VideoFrameService.h`        |
| Header guard      | `#pragma once`                          |                              |
| Namespace         | `cosmo::` top-level; per-module sub-ns  | `cosmo::media`, `cosmo::flow` |
| Classes           | PascalCase                              | `VideoFrameServiceImpl`      |
| Member variables  | `snake_case_` (trailing underscore)     | `video_width_`               |
| Struct members    | `snake_case` (no underscore)            | `max_retries`                |
| Functions         | PascalCase                              | `GetVideoFrame()`            |
| Constants         | `k` + PascalCase                        | `kDefaultHttpPort`           |
| Enums             | `enum class` with `k` + PascalCase      | `kAlarm`, `kInfo`            |
| Booleans          | `is_` / `has_` / `should_` prefix       | `is_running_`                |

Strict rules:
- No `using namespace` in headers.
- No bare `new` / `delete` — use RAII and smart pointers.
- No C-style casts — use `static_cast`, `const_cast`, etc.
- No `#define` for constants — use `constexpr`.
- No magic numbers.
- `const` correctness is required everywhere.
- Thread safety: use `std::atomic` for shared flags, lock guards for shared data.

### Formatting and Static Analysis

```bash
# Format check
bash scripts/format_check.sh --check         # all src/ + test/
bash scripts/format_check.sh --staged --check # staged only
bash scripts/format_check.sh --fix            # auto-format

# Static analysis
bash scripts/static_analysis.sh --cppcheck
bash scripts/static_analysis.sh --clang-tidy  # needs compile_commands.json
```

Configuration files at the repository root: `.clang-format`, `.clang-tidy`, `.cppcheck-suppressions`.

## Key Modules Reference

### Flow (`src/flow/`) — Algorithm Pipeline Nodes

The flow layer implements the composable nodes that make up scenario task pipelines:

| Subdirectory    | Purpose                                          |
| --------------- | ------------------------------------------------ |
| `action/`       | Action instances, branches, the action manager   |
| `alarm/`        | Area alarms, task alarms, alarm suppression      |
| `channel/`      | Algorithm channel decode, demux, MP4 record, buffer pool |
| `classify/`     | AI classifiers (attribute, group, area)          |
| `detect/`       | AI detectors (YOLO), DINO open-vocabulary detector, SAM2 segmenter |
| `logical/`      | Logical judgment, face logic, sensitivity calculation |
| `qwen3vl/`      | Qwen3 VL inference worker and manager            |
| `recognizer/`   | AI recognizers (face/body re-id)                 |
| `stream/`       | RTMP streaming, encoder, overview renderer       |
| `common/`       | Shared types: `AlgDataUnit`, `AlgDataQueue`, `AlgCommonType` |

Each domain typically has:
- An **Ai\* class** (e.g., `AiDetector`) — the actual algorithm logic.
- A **P\* class** (e.g., `PDetector`) — the pipeline node wrapping it, forming part of an `AlgActionBase`-derived action.

### NN (`src/nn/`) — Backend Abstraction

The neural network layer uses an abstract device pattern:

- `src/nn/core/` — Device-agnostic graph, blob, and node base classes.
- `src/nn/device/sophon/` — Sophon BM1688 TPU backend (BMRT).
- `src/nn/device/cpu/` — x86 ONNX Runtime backend.
- `src/nn/device/naive/` — Fallback for in-memory compute.
- `src/nn/pipeline/` — High-level pipelines: `detection`, `classify`, `feature`, `keypoints`, `advanced`.

Adding a new NN op requires a node implementation for each active backend.

### Inference (`src/infer/`) — Backend-Agnostic Inference

Uses a **"Unify" pattern**: each task type has an interface + a unified implementation that delegates to the active NN backend:

| Class                  | Task                       |
| ---------------------- | -------------------------- |
| `AiDetectorUnify`      | Object detection           |
| `AiClassifierUnify`    | Image classification       |
| `AiRecognizerUnify`    | Re-identification (embeddings) |
| `AiLandmarkerUnify`    | Keypoint / landmark        |
| `AiTrackerUnify`       | Object tracking (ByteTrack) |
| `DinoDetectorUnify`    | Open-vocabulary detection  |
| `Sam2SegmenterUnify`   | SAM2 segmentation          |
| `Qwen3VLUnify`         | Vision-language model      |

### Media (`src/media/`) — Video / Audio / OSD

Handles video decode/encode (FFmpeg), hardware codecs (Sophon VPP/VPU), OSD rendering (bitmap text + graphics), frame transform, and streaming output.

### Network (`src/network/`) — HTTP / MQTT

| Subdirectory | Library       | Purpose                     |
| ------------ | ------------- | --------------------------- |
| `http/`      | uWebSockets   | HTTP server, thread pool, multipart parsing |
| `mqtt/`      | Paho MQTT C   | MQTT client, topics, heartbeats |
| `msg/`       | —             | Internal message dispatch   |

### Database (`src/db/`) — SQLite

The `DaoBase` class wraps `SQLite::Database` and provides `SetCondition()`, `SetLimit()`, `Begin()`, `Commit()`, `Rollback()`. Concrete DAOs (e.g., `PersonDao`, `TaskEventDao`, `PassengerFlowDao`) extend it. `TransactionGuard` provides RAII transaction management.

### Third-Party Dependencies

Key third-party libraries (under `3rd/` and linked via CMake `ExternalProject`):

| Library            | Purpose                          | License      |
| ------------------ | -------------------------------- | ------------ |
| SQLiteCpp          | SQLite C++ wrapper               | MIT          |
| fmt                | String formatting                | MIT          |
| nlohmann-json      | JSON parsing                     | MIT          |
| uWebSockets        | HTTP / WebSocket server          | Apache 2.0   |
| Paho MQTT C        | MQTT client                      | EPL 2.0      |
| ONNX Runtime       | x86 CPU inference                | MIT          |
| Sophon BMRT        | aarch64 TPU inference            | Proprietary  |
| FFmpeg             | Video decode / encode            | LGPL 2.1+    |
| OpenH264           | H.264 encode (CPU backend)       | BSD 2-Clause |
| Catch2             | Test framework                   | Boost        |
| Trompeloeil        | Mocking framework                | Boost        |

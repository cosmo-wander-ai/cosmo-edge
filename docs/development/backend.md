---
title: 后端开发
description: 后端架构、构建系统、服务层与依赖注入、API 路由、测试、代码风格及核心模块参考。
prev:
  text: 前端工程
  link: /development/frontend
next:
  text: CI 与质量检查
  link: /development/ci
---

# 后端开发

后端基于 C++17 和 CMake 构建，提供运行时服务、媒体处理、NN 推理、API 路由、事件输出和场景任务编排，最终链接为单一可执行文件 `cosmo-engine`。

## 架构概览

CMake 构建以**对象库**形式按分层组织，各层之间单向依赖（上层依赖下层，反之不可）：

```
Layer 0 (基础层)         cosmo_util  cosmo_db  cosmo_platform  cosmo_mem
Layer 1 (基础设施层)     cosmo_media  cosmo_infer  cosmo_network  cosmo_nn
Layer 2 (业务层)         cosmo_flow  cosmo_service  cosmo_linkage
Layer 3 (接口层)         cosmo_api
```

每个对象库对应 `src/` 下的一个源码目录。

## 构建入口

| 文件 / 脚本                      | 用途                           |
| -------------------------------- | ------------------------------ |
| `scripts/build_cpu.sh`           | x86 CPU 后端构建               |
| `scripts/build_cpu_test.sh`      | CPU 测试构建（`cosmo-tests`）  |
| `scripts/build.sh`               | Sophon / aarch64 构建          |
| `scripts/build_sophon_package.sh`| Sophon 发布包构建              |
| `CMakeLists.txt`                 | 顶层构建和打包入口             |

## CMake 选项

| 选项                            | 默认值     | 说明                                  |
| ------------------------------- | :--------: | ------------------------------------- |
| `COSMO_TARGET_ARCH`             | `aarch64`  | 目标架构：`aarch64` 或 `x86_64`       |
| `COSMO_NN_USE_SOPHON_BACKEND`   | `ON`       | 启用 Sophon TPU 后端                  |
| `COSMO_NN_USE_CPU_BACKEND`      | `OFF`      | 启用 CPU / ONNX Runtime 后端          |
| `COSMO_ENABLE_OPENH264`         | 自动       | 启用 OpenH264（CPU 后端时自动开启）   |
| `COSMO_DEV_MODE`                | `OFF`      | 开发模式，跳过看门狗等生产校验        |
| `COSMO_MODEL_GUARD`             | 自动       | 链接 `libcosmo_model_guard.so`（Sophon 默认开启） |
| `BUILD_TESTS`                   | `OFF`      | 构建 `cosmo-tests`，含 Catch2 + gcov  |

`COSMO_NN_USE_SOPHON_BACKEND` 和 `COSMO_NN_USE_CPU_BACKEND` 互斥。选择 CPU 后端时会自动启用 `COSMO_ENABLE_OPENH264` 并禁用 `COSMO_MODEL_GUARD`。

## 服务层与依赖注入

### ServiceRegistry

所有后端服务通过 `cosmo::service::ServiceRegistry` 装配。它是一个以 `std::type_index` 为 key 的线程安全 DI 容器。

```cpp
// 生产环境注册（持有所有权，ShutdownAll 时按注册逆序销毁）
ServiceRegistry::Instance().Register<IFooService>(std::make_unique<FooServiceImpl>());

// 测试 / Mock 注入（不持有所有权）
ServiceRegistry::Instance().Set<IFooService>(&mockFoo);

// 获取服务
auto& foo = ServiceRegistry::Instance().Get<IFooService>();
```

### 启动流程

服务初始化在 `src/app/app_init.cc` 中完成，由 `SwDeviceInit()` 按四阶段调用：

**阶段一 — `RegisterInfrastructureServices()`**
注册基础设施服务：`IFileService`、`IEventNotifier`（WebSocket 推送）、`IMemoryPoolService`、`IStorageCleanService`、`IWatchDogService`、`IDbService`（SQLite）、`IOsdTextRenderer`、`IVideoFrameService`、`ITaskService`、`IInferPoolService`、`ILlmInferService`、`INetworkService`、`IDeviceDiscoveryService`、`IHttpClient`。

**阶段二 — `RegisterBusinessServices()`**
注册业务服务：音频、联动、摄像头、图片任务、算法、设备信息、时间（NTP）、系统配置、模型管理、告警记录/推送、认证、计划任务、人脸/人体/物品底库、直播流、动作、客户端消息、应用信息、定时重启。

**阶段三 — `InitializeServices()`**
按依赖顺序调用各服务的 `Init()`：OSD 字体加载 → 算法服务 → 网络服务 → 数据库 → 告警推送 → 模型 → 设备信息 → 定时重启 → 人脸数据加载 → 摄像头实体初始化 → 应用信息 → MQTT 启动。

**阶段四 — `InitializeExternalComponents()`**
启动 HTTP 服务器、WebSocket 服务器、设备发现多播、存储清理定时器、硬件看门狗（`COSMO_DEV_MODE` 下跳过）。

### 新增服务

1. 在 `src/service/<domain>/` 下定义接口（如有对应基础接口则继承）。
2. 在同一目录下编写实现类。
3. 在 `src/app/app_init.cc` 的对应阶段中注册：
   ```cpp
   ServiceRegistry::Instance().Register<IMyService>(std::make_unique<MyServiceImpl>());
   ```
   如果一个实现类同时对外暴露多个窄接口，通过 `Set<>()` 添加别名：
   ```cpp
   ServiceRegistry::Instance().Set<IMyQuery>(&ServiceRegistry::Instance().Get<IMyService>());
   ```
4. 将实现文件加入 `src/service/CMakeLists.txt`。

销毁由 `SwDeviceDestroy()` 自动处理：调用 `ServiceRegistry::ShutdownAll()` 按注册逆序销毁所有持有所有权的服务。

## API 路由

### 路由注册

API 路由定义在 `src/api/ApiRouter.cc` 和 `src/api/ApiRouterRoutes.cc` 中。系统创建两个 `ApiRouter` 实例：一个处理 HTTP 请求（`MessageFromHttp`），一个处理 MQTT 下发请求（`MessageFromMqtt`），两者共享同一份路由表。

路由注册使用宏模式（定义在 `ApiRouterInternal.h`）：

```cpp
ROUTE("/gtw/cwai/System/QueryDeviceInfo", Mtk, system_handler_, System, QueryDeviceInfo)
```

展开后完成以下流程：
1. 匹配 URL（通过 `util::ToLower` 做大小写不敏感匹配）。
2. 若路由标记为 `Mtk`，校验 `mtk` token；标记为 `None` 则跳过校验。
3. 将请求 JSON 反序列化为 `NS::MsgQueryDeviceInfoSend`。
4. 调用对应的 handler 方法。
5. 将响应 `NS::MsgQueryDeviceInfoRecv` 序列化为 JSON 返回。

### 标准响应信封

大多数管理端响应继承 `MsgSendHead`：

| 字段        | 类型     | 说明                            |
| ----------- | -------- | ------------------------------- |
| `resCode`   | number   | `1` 成功，`0` 失败              |
| `resMsg`    | object[] | 错误或提示信息列表              |
| `resData`   | object   | 业务数据，按接口不同而变化      |
| `resultCode` | string  | ChinaMobile 兼容响应码          |
| `resultMsg` | string   | ChinaMobile 兼容响应文本        |

### 新增路由组

1. 在 `src/api/` 下创建 handler 类（如 `MessageMyHandler.h/.cc`）。
2. 定义 Send/Recv 结构体和 handler 方法。
3. 编写 `RegisterMyRoutes()` 函数，为每个端点调用 `ROUTE` 或 `ROUTE_CORE`。
4. 在 `ApiRouter` 构造函数中调用 `RegisterMyRoutes()`。

### 路由组一览

| 注册函数                    | URL 前缀                                | 领域             |
| --------------------------- | ---------------------------------------- | ----------------- |
| `RegisterCoreRoutes()`      |（登录、接口列表等）                      | 核心 / 认证       |
| `RegisterNetworkRoutes()`   | `/gtw/cwai/Network/`                     | 网络、DNS、NTP    |
| `RegisterAlgorithmRoutes()` | `/gtw/cwai/Algorithm/`                   | 算法管理          |
| `RegisterModelRoutes()`     | `/gtw/cwai/atomic/Model/`                | 模型仓库          |
| `RegisterScheduleRoutes()`  | `/gtw/cwai/schedule/`                    | 时间模板          |
| `RegisterEventRoutes()`     | `/gtw/cwai/Event/`                       | 事件、告警        |
| `RegisterCameraRoutes()`    | `/gtw/cwai/Camera/`                      | 摄像头、USB       |
| `RegisterTaskRoutes()`      | `/gtw/cwai/Task/`                        | 场景任务          |
| `RegisterSystemRoutes()`    | `/gtw/cwai/System/`                      | 设备、升级、调试  |
| `RegisterLibraryRoutes()`   | `/gtw/cwai/Library/`、`/BodyLibrary/`、`/ThingsLibrary/` | 人脸、人体、物品底库 |
| `RegisterFileRoutes()`      | `/gtw/cwai/File/`                        | 文件导入          |
| `RegisterAudioRoutes()`     | `/gtw/cwai/Audio/`                       | 音频文件、设备    |
| `RegisterLinkageRoutes()`   | `/gtw/cwai/AlarmStrage/`                 | 告警联动          |
| `RegisterLiveStreamRoutes()`| `/gtw/cwai/LiveStream/`                  | 直播流生命周期    |

## 测试

### 框架

测试使用 [Catch2](https://github.com/catchorg/Catch2) + [Trompeloeil](https://github.com/rollbear/trompeloeil) mock 框架。测试文件放在 `test/` 目录下，与服务或组件一对一对应。

### 构建和运行

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests
```

### Mock

`ServiceRegistry::Set<T>(ptr)` 不持有所有权的注入方式是测试中替换真实服务的主要手段。公共 mock 头文件（`test/test_mock_services.h`）为常用接口提供了可复用的 mock 实现。

### 测试文件命名

测试文件遵循 `test_<component>.cc` 的命名规范，如：
- `test_service_registry.cc` → 测试 `ServiceRegistry`
- `test_video_frame_service_impl.cc` → 测试 `VideoFrameServiceImpl`
- `test_api_router.cc` → 测试 `ApiRouter`

新增服务时，应同步新增对应的测试文件，为每个依赖项提供 Trompeloeil mock。

## 代码风格

项目遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 并适配 C++17。完整规范见仓库根目录的 `CODING_STYLE.md`。核心约定速查：

| 类别           | 约定                                    | 示例                          |
| -------------- | --------------------------------------- | ----------------------------- |
| 文件命名       | PascalCase，`.h` / `.cc`                | `VideoFrameService.h`         |
| 头文件保护     | `#pragma once`                          |                               |
| 命名空间       | `cosmo::` 顶层；子模块按领域划分        | `cosmo::media`、`cosmo::flow` |
| 类名           | PascalCase                              | `VideoFrameServiceImpl`       |
| 成员变量       | `snake_case_`（末尾下划线）             | `video_width_`                |
| 结构体成员     | `snake_case`（无末尾下划线）            | `max_retries`                 |
| 函数名         | PascalCase                              | `GetVideoFrame()`             |
| 常量/constexpr | `k` + PascalCase                        | `kDefaultHttpPort`            |
| 枚举值         | `enum class` + `k` + PascalCase         | `kAlarm`、`kInfo`             |
| 布尔值         | `is_` / `has_` / `should_` 前缀         | `is_running_`                 |

严格规则：
- 头文件中禁止 `using namespace`。
- 禁止裸 `new` / `delete`，统一使用 RAII 和智能指针。
- 禁止 C 风格类型转换，使用 `static_cast`、`const_cast` 等。
- 禁止 `#define` 定义常量，使用 `constexpr`。
- 禁止魔数。
- 全量要求 `const` 正确性。
- 线程安全：共享标志位用 `std::atomic`，共享数据用锁守卫。

### 格式化和静态分析

```bash
# 格式检查
bash scripts/format_check.sh --check          # 检查全部 src/ 和 test/
bash scripts/format_check.sh --staged --check # 仅检查暂存区
bash scripts/format_check.sh --fix            # 自动格式化

# 静态分析
bash scripts/static_analysis.sh --cppcheck
bash scripts/static_analysis.sh --clang-tidy  # 需要 compile_commands.json
```

配置文件位于仓库根目录：`.clang-format`、`.clang-tidy`、`.cppcheck-suppressions`。

## 核心模块参考

### Flow（`src/flow/`）— 算法流水线节点

flow 层实现场景任务流水线中的可组合节点：

| 子目录        | 用途                                                |
| ------------- | --------------------------------------------------- |
| `action/`     | 动作实例、分支、动作管理器                          |
| `alarm/`      | 区域告警、任务告警、告警抑制                        |
| `channel/`    | 算法通道解码、解复用、MP4 录像、缓冲池              |
| `classify/`   | AI 分类器（属性、分组、区域）                       |
| `detect/`     | AI 检测器（YOLO）、DINO 开放词表检测器、SAM2 分割器 |
| `logical/`    | 逻辑判断、人脸逻辑、灵敏度计算                      |
| `qwen3vl/`    | Qwen3 VL 推理 worker 和管理器                       |
| `recognizer/` | AI 识别器（人脸/人体 re-id）                        |
| `stream/`     | RTMP 推流、编码器、概览渲染器                       |
| `common/`     | 共享类型：`AlgDataUnit`、`AlgDataQueue`、`AlgCommonType` |

每个领域通常包含：
- **Ai\* 类**（如 `AiDetector`）—— 实现实际算法逻辑。
- **P\* 类**（如 `PDetector`）—— 封装为流水线算子节点，构成 `AlgActionBase` 派生 action 的一部分。

### NN（`src/nn/`）— 后端抽象

神经网络层使用抽象设备模式：

- `src/nn/core/` — 与设备无关的图、Blob 和 Node 基类。
- `src/nn/device/sophon/` — Sophon BM1688 TPU 后端（BMRT）。
- `src/nn/device/cpu/` — x86 ONNX Runtime 后端。
- `src/nn/device/naive/` — 内存计算兜底实现。
- `src/nn/pipeline/` — 高层流水线：`detection`、`classify`、`feature`、`keypoints`、`advanced`。

新增 NN 算子时需要为每个活跃后端提供对应的节点实现。

### Inference（`src/infer/`）— 后端无关推理

使用 **"Unify" 模式**：每种任务类型定义一个接口 + 一个统一实现，将调用委托给当前活跃的 NN 后端：

| 类                     | 任务                       |
| ---------------------- | -------------------------- |
| `AiDetectorUnify`      | 目标检测                   |
| `AiClassifierUnify`    | 图像分类                   |
| `AiRecognizerUnify`    | 特征提取 / re-id           |
| `AiLandmarkerUnify`    | 关键点 / 姿态估计          |
| `AiTrackerUnify`       | 目标跟踪（ByteTrack）      |
| `DinoDetectorUnify`    | 开放词表检测               |
| `Sam2SegmenterUnify`   | SAM2 分割                  |
| `Qwen3VLUnify`         | 视觉语言模型               |

### Media（`src/media/`）— 视频 / 音频 / OSD

负责视频编解码（FFmpeg）、硬件编解码（Sophon VPP/VPU）、OSD 渲染（位图文字 + 图形）、帧变换和流输出。

### Network（`src/network/`）— HTTP / MQTT

| 子目录   | 底层库         | 用途                                |
| -------- | -------------- | ----------------------------------- |
| `http/`  | uWebSockets    | HTTP 服务器、线程池、multipart 解析 |
| `mqtt/`  | Paho MQTT C    | MQTT 客户端、topic、心跳            |
| `msg/`   | —              | 内部消息分发                        |

### Database（`src/db/`）— SQLite

`DaoBase` 类封装 `SQLite::Database`，提供 `SetCondition()`、`SetLimit()`、`Begin()`、`Commit()`、`Rollback()`。具体 DAO（如 `PersonDao`、`TaskEventDao`、`PassengerFlowDao`）继承该类。`TransactionGuard` 提供 RAII 事务管理。

### 第三方依赖

主要第三方库（位于 `3rd/` 并通过 CMake `ExternalProject` 链接）：

| 库                | 用途                          | 许可证        |
| ----------------- | ----------------------------- | ------------- |
| SQLiteCpp         | SQLite C++ 封装               | MIT           |
| fmt               | 字符串格式化                  | MIT           |
| nlohmann-json     | JSON 解析                     | MIT           |
| uWebSockets       | HTTP / WebSocket 服务器       | Apache 2.0    |
| Paho MQTT C       | MQTT 客户端                   | EPL 2.0       |
| ONNX Runtime      | x86 CPU 推理                  | MIT           |
| Sophon BMRT       | aarch64 TPU 推理              | 专有          |
| FFmpeg            | 视频编解码                    | LGPL 2.1+     |
| OpenH264          | H.264 编码（CPU 后端）        | BSD 2-Clause  |
| Catch2            | 测试框架                      | Boost         |
| Trompeloeil       | Mock 框架                     | Boost         |

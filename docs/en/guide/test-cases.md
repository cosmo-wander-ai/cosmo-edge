---
title: Test Scope and Test Cases
description: Test scope planning and detailed test case design for the CosmoEdge edge AI engine, covering CV algorithms, VLM/DINO large models, visual orchestration, and system integration.
prev:
  text: Architecture Overview
  link: /en/guide/architecture
next: false
---

# CosmoEdge Test Scope and Test Cases

This document provides a testing guide for QA teams covering the **CosmoEdge edge AI engine**. It covers test scope definition, test environment preparation, detailed test case design for core functional modules, performance and stress testing, and exception/reliability testing plans.

---

## 1. Test Scope Planning (Test Scope)

Testing is divided into five dimensions: **core functional testing**, **specialized testing (performance and stress)**, **reliability and self-healing testing**, **platform compatibility testing**, and **integration testing**.

### 1.1 Core Functional Test Scope
*   **User authentication and security**: User login, password change, Token (`mtk`) authentication mechanism, verification of functional state in dev mode (skip SN validation) and restricted mode (failed SN validation starts only basic HTTP services).
*   **Video access and media processing**: Offline video upload and playback, RTSP network stream pulling, USB camera recognition; VPU hardware decoding, CPU software decoding; real-time frame capture verification.
*   **Model repository management**: ONNX (x86) and bmodel (Sophon) model upload, metadata configuration, version switching, model hot-loading and hot-swap workflows.
*   **Visual algorithm orchestration**: Web-based visual canvas (Vue Flow) node drag-and-drop, wiring (Decode -> Detection -> Tracking -> Classification -> Sensitivity Filter -> Alarm Reporting), property parameter editing, orchestration rule export and import.
*   **Scenario task configuration**: Detection area (ROI, up to 4 hexagonal zones) drawing, run schedule strategy configuration, dynamic activation of core algorithm key parameters (e.g., alarm interval, max alarm count, static-target deduplication, sensitivity threshold, minimum filter size).
*   **Real-time analysis and OSD rendering**: WebRTC live preview (SRS 6.0 cascade), algorithm-level OSD overlay (prediction boxes, label confidence, region boundaries, counters), timing statistics panel (Decode / Infer / OSD / Encode latency statistics).
*   **On-device large models (VLM & DINO)**:
    *   **VLM (Vision-Language Model)**: Prompt parsing, frame-rate configuration, ROI cropping, YES/NO/multi-class result inference stability, batch image inference analysis (PIC task).
    *   **DINO (Open-vocabulary detection)**: English dotted token parsing (e.g., `person.garbage`), zero-shot object detection and localization, real-time OSD detection box rendering.
*   **Event center and alarm management**: Alarm screenshot capture and retention, severity classification, historical event filtering by channel/algorithm/time, alarm record CSV export.
*   **Gallery management**: Face gallery (image upload, feature extraction), body feature gallery, object gallery CRUD operations.
*   **Voice and audio control**: Audio file upload, linked speaker broadcast, sound-and-light alarm testing.

### 1.2 Specialized Test Scope
*   **Concurrency performance testing**: Multi-channel (up to 16 channels) CV algorithm concurrent analysis on Sophon BM1688 edge devices, concurrent scheduling of multi-scenario tasks sharing a decoded stream.
*   **Stability and memory leak testing**: Continuous long-cycle video stress testing for 72+ hours (against 200+ distinct video samples), monitoring memory, CPU, NPU temperature, and utilization metrics.
*   **Inference latency and throughput**: Record E2E latency from video frame input to OSD overlay/event generation, covering different inference chains such as regular CV algorithms (YOLO), DINO, and VLM.

### 1.3 Reliability and Self-Healing Test Scope
*   **Watchdog self-healing**: Simulate the core inference process being killed and verify system auto-recovery.
*   **Storage cleanup protection**: Automatic circular cleanup and overwrite of historical images/videos/logs when disk space is full.
*   **Disconnection and recovery**: RTSP automatic reconnection on disconnect, retry and caching strategy on MQTT/Webhook send failure.

---

## 2. Test Environment and Verification Preparation

### 2.1 Test Platform Matrix
| Platform Type | Processor / Chip | OS / Runtime Environment | Target Use |
| :--- | :--- | :--- | :--- |
| **Edge Production** | Sophon BM1688 NPU/VPU | Linux (aarch64) SDK V23.10 | Production-grade deployment, NPU acceleration, high-concurrency multi-channel video analysis. |
| **Dev/Test** | Intel/AMD x86_64 | Ubuntu 22.04 LTS (Docker) | Open-source/integration-level development and debugging, ONNX Runtime (CPU). |
| **Evaluation/Test** | Intel/AMD x86_64 | Windows 10/11 (Docker Compose) | Local visual evaluation, lightweight debugging. |

### 2.2 Test Material Preparation
1.  **Test video sources**:
    *   1080P/720P H.264/H.265 RTSP simulated streams (you can use VLC or FFmpeg for loop streaming).
    *   Test MP4 files for scenarios such as construction-site workers with/without helmets, absence from post, and fire cabinet open/close.
2.  **Large model test samples**:
    *   Prepare at least 10 river images containing different floating objects (positive samples), and 10 clean river images (negative samples), for VLM accuracy regression.
3.  **Gallery images**:
    *   Standard JPG images used for face and body retrieval verification.

---

## 3. Detailed Test Case Design (Test Cases)

### 3.1 User Authentication and Restricted Mode (TC-AUTH)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-AUTH-001** | User Authentication | Default account successful login | System started normally, frontend console ready | 1. Visit `http://localhost:8080`<br>2. Enter default account `admin`/`admin`<br>3. Click Login | 1. Login succeeds, redirected to system dashboard<br>2. API returns `mtk` token, stored in Cookie/LocalStorage |
| **TC-AUTH-002** | User Authentication | Token authentication failure and redirect | User not logged in, or holds an expired Token | 1. Clear browser local `mtk` Token<br>2. Try to directly access `/gtw/cwai/System/info` or an internal page | 1. API returns HTTP 401 or a business error code<br>2. Frontend intercepts and redirects to the login page |
| **TC-AUTH-003** | Device Authorization | Device SN validation and restricted mode | System compiled without `COSMO_DEV_MODE`, and device SN validation fails | 1. Start the engine on an unauthorized device<br>2. Visit the system web pages | 1. System enters **restricted mode**<br>2. Only basic status and configuration endpoints start; all inference services and scenario tasks cannot be configured or are grayed out |
| **TC-AUTH-004** | Device Authorization | Dev mode exempt from SN validation | System compiled with `COSMO_DEV_MODE` | 1. Start the engine without an authorization file<br>2. Visit the system function pages | 1. System starts fully normally, SN validation exempted<br>2. Scenario tasks and algorithm orchestration run normally |

### 3.2 Video Access and Camera Management (TC-CAM)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-CAM-001** | Video Access | Add offline video channel | Test MP4 video prepared | 1. Go to the "Video Access" page, click "Add"<br>2. Select "Offline Video" as the access type, fill in the name, upload the MP4 file, and save | 1. Saved successfully, channel appears in the list<br>2. Status is "Online", capture thumbnail displayed |
| **TC-CAM-002** | Video Access | Add RTSP network camera | A usable RTSP stream address is ready | 1. Add a channel, select "RTSP" as the access type<br>2. Enter the RTSP address (e.g., `rtsp://admin:123@ip:port`) and save | 1. Saved successfully<br>2. System successfully establishes the RTSP connection and decodes, channel status is normal |
| **TC-CAM-003** | Video Access | RTSP video source reconnection on disconnect | Channel is decoding and analyzing normally | 1. Manually interrupt the RTSP network stream (or stop the simulated streaming source) for 30 seconds<br>2. Restart the streaming source and observe system behavior | 1. While disconnected, channel shows "Offline" status, task paused<br>2. After network recovery, the system automatically reconnects and decodes, the task resumes seamlessly, no crash |

### 3.3 Model Repository Management (TC-MOD)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-MOD-001** | Model Management | Model upload and format validation | ONNX/bmodel format file prepared | 1. Go to "Model Management" page, click "Upload Model"<br>2. Select the model file, configure metadata (framework type, input size), submit | 1. Upload succeeds, the backend parses the model header information correctly<br>2. The model appears in the model list with an available status |
| **TC-MOD-002** | Model Management | Model hot-swap and version management | A running scenario task is bound to the same channel | 1. Switch the model version bound to the running task from v1.0 to v2.0<br>2. Save the task configuration and observe the live view | 1. The inference engine dynamically unloads the old model and loads the new one, with no need to restart the cosmo-engine process<br>2. No frame drops or long screen freezes during the switch |

### 3.4 Visual Algorithm Orchestration (TC-FLOW)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-FLOW-001** | Algorithm Orchestration | Orchestration rule validity validation | Visual orchestration design page ready | 1. Drag in two conflicting nodes (e.g., connect "Face Gallery" and "Absence Detection" incorrectly)<br>2. Try clicking Save | 1. Frontend/backend rule validator intercepts and reports an error, indicating component type mismatch or wiring logic error |
| **TC-FLOW-002** | Algorithm Orchestration | Composite algorithm orchestration creation and export | Want to create a "no helmet" pipeline | 1. Drag components: Decode -> Object Detection -> Object Tracking -> Helmet Classification -> Alarm Reporting<br>2. Configure parameters for each atomic node and save<br>3. Click "Export Orchestration" | 1. Saved successfully, a custom algorithm is added to the algorithm service list<br>2. The exported JSON orchestration configuration file is complete and correct |

### 3.5 Scenario Task and Rule Configuration (TC-TASK)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-TASK-001** | Scenario Task | Detection area (ROI) drawing and activation | A "no helmet" algorithm is assigned to the construction-site entrance channel | 1. Go to scenario task configuration, click "Add Region", draw a local hexagonal ROI<br>2. Observe in the live view whether personnel without helmets in non-ROI regions trigger an alarm | 1. Personnel without helmets outside the ROI are ignored, no inference classification is performed<br>2. Only violations inside the ROI trigger an alarm |
| **TC-TASK-002** | Scenario Task | Alarm interval and deduplication control | Task parameter configuration page ready | 1. Change "Alarm Interval" to 10 seconds and save<br>2. Continuously observe the same unhelmeted person, record alarm frequency<br>3. Enable "Static Target Deduplication" and save, observe fixed objects such as posters | 1. The second alarm triggers after 10 seconds (once every 10 seconds)<br>2. After enabling deduplication, stationary interfering objects in the frame no longer repeatedly generate alarms |
| **TC-TASK-003** | Scenario Task | Time-series threshold logic (absence detection) | "Absence Detection" algorithm assigned to the channel | 1. Set "Absence Time" to 30 seconds and "Minimum On-Duty Count" to 1 in parameters<br>2. Simulate personnel leaving the post, time 40 seconds<br>3. Simulate personnel briefly returning at 20 seconds then leaving again | 1. An absence alarm triggers when 30 seconds have elapsed<br>2. After a brief return, the time-series counter resets, no alarm triggers until they leave again for a full 30 seconds |

### 3.6 Real-time Analysis and OSD Rendering (TC-LIVE)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-LIVE-001** | Live View | Real-time WebRTC stream pulling and preview | A scenario task is currently running | 1. Go to the console "Live View" page<br>2. Select the corresponding channel, enable WebRTC stream preview | 1. The video stream plays smoothly, end-to-end stream-pulling latency (including SRS/WebRTC transport) is below 500ms<br>2. Alarm pop-ups flash in real time in the upper-right corner of the console |
| **TC-LIVE-002** | Live View | Algorithm OSD overlay and debug info display | Algorithm is in analysis state | 1. In the view control bar, check the OSD overlay for the corresponding algorithm<br>2. Turn on the "Debug View" switch | 1. Violating targets in the video frame are correctly boxed with category labels and confidence<br>2. The debug view shows the original tracking ID, classification probability, and Pipeline orchestration timing statistics in the upper-left corner |

### 3.7 VLM and DINO Large Model Testing (TC-LLM)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-LLM-001** | VLM Analysis | VLM text prompt takes effect | "River floating object detection" algorithm uses a VLM node | 1. In the large model parameter configuration, set the Prompt to: "Are there leaves or garbage in the frame?"<br>2. Set "Frame Rate" to 0.2 (analyze one frame every 5 seconds)<br>3. Play the floating-object video stream | 1. The engine captures one frame every 5 seconds and calls VLM inference<br>2. When floating objects are present in the frame, the event panel outputs `YES`<br>3. Timing statistics show single-frame inference time (usually between 1.5 - 3.5 seconds) |
| **TC-LLM-002** | VLM Analysis | VLM batch image offline inference (PIC) | A VLM task of the "Image Analysis" data source type is created | 1. Go to the "Image Inference" page, upload multiple positive/negative sample images<br>2. Click "Start Analysis", wait for inference to finish | 1. Batch analysis executes smoothly, with no concurrency crashes or memory exhaustion<br>2. The presence/absence of the target state is correctly identified, confidence output is normal |
| **TC-LLM-003** | DINO Analysis | DINO open-vocabulary object localization | A DINO-class algorithm is assigned to the channel | 1. Configure `"garbage.person"` in the orchestration prompt<br>2. Play a video stream with pedestrians and garbage | 1. The system can localize the non-fixed categories "garbage" and "person"<br>2. OSD successfully overlays detection boxes, and corresponding structured target data is generated in the alarm center |

### 3.8 Alarm Event and Push Integration (TC-ALARM)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-ALARM-001** | Event Center | Alarm data query and CSV export | The system has produced a certain amount of historical alarms | 1. Visit the "Event Center -> Detection/Analysis" page<br>2. Combined filter: select a specified channel, helmet algorithm, today<br>3. Click "Data Export" | 1. The list accurately filters alarm data matching the criteria<br>2. The CSV file downloads normally, the content includes channel name, algorithm, alarm time, logical result, screenshot path, with no garbled text |
| **TC-ALARM-002** | Alarm Push | MQTT structured data push | The MQTT broker service is ready and configured in system settings | 1. Start a scenario task, trigger a "no helmet" alarm<br>2. Listen on the corresponding MQTT topic (e.g., `/d2p/aibox`, see [MQTT Reference](/en/reference/mqtt) for the device-to-platform report topic) | 1. The MQTT client receives a structured JSON message in real time<br>2. The message content conforms to the `MsgSendHead` and ChinaMobile-compatible format, including image Base64/path and event details |
| **TC-ALARM-003** | Alarm Push | HTTP Webhook event push | Webhook URL configured (the built-in test push service can be used) | 1. Trigger a system alarm<br>2. Check the HTTP POST request received by the Webhook receiver | 1. The receiver successfully receives the JSON data packet, responds with status code 200<br>2. The data packet contains the alarm event ID, screenshot URL, alarm determination status, etc. |

### 3.9 System Management and Exception Self-Healing (TC-SYS)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-SYS-001** | System Management | Network and connectivity configuration | Network configuration page ready | 1. Change the NIC IP address or DNS configuration<br>2. Click "Network Connectivity Test", enter an external address or proxy | 1. Configuration is successfully modified and applied to the underlying system files<br>2. The connectivity test accurately reports network quality and Ping latency |
| **TC-SYS-002** | Fault Self-Healing | Core inference process crash recovery (Watchdog) | A task is running | 1. In the backend terminal, forcibly kill the main inference process `cosmo-engine` | 1. The Watchdog service detects the process is gone<br>2. It automatically restarts the engine within the configured time (e.g., 10 seconds) and restores the previously running tasks |
| **TC-SYS-003** | Storage Protection | Disk-full automatic cleanup | A disk storage upper limit is configured (e.g., 90%) | 1. Simulate disk space reaching the limit (fill with large files or lower the threshold)<br>2. Trigger a new alarm to produce a screenshot | 1. The system successfully triggers the cleanup mechanism, automatically deleting the oldest batch of alarm snapshots<br>2. The new alarm screenshot is saved normally, the system does not report a "disk space insufficient" error |

### 3.10 Performance Stress Test Cases (TC-PERF)

| Case ID | Module | Title | Preconditions | Steps | Expected Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-PERF-001** | Performance Test | 16-channel concurrent CV inference stress test | Sophon BM1688 device, multi-channel video streams ready | 1. Enable 16 channels of H.264 video stream access<br>2. Bind regular YOLOv8n detection and tracking algorithms to all 16 channels<br>3. Enable OSD rendering and WebRTC streaming | 1. 16 channels run stably, FPS meets expectations (e.g., 3 FPS/channel)<br>2. The single-frame Decode->OSD->Encode pipeline latency (excluding network transport) is 32-68ms<br>3. CPU, memory, and NPU utilization and temperature are within safe range, no hangs |
| **TC-PERF-002** | Performance Test | 72-hour stability and leak monitoring | 16 channels fully loaded | 1. Keep the system running under high load for 72 hours<br>2. Write a script to record the virtual and physical memory consumption (Resident Set Size) of the `cosmo-engine` process every half hour | 1. No system crash or restart over 72 hours of running<br>2. The memory consumption curve is flat, with no signs of a continuously diverging memory leak |

---

## 4. Test Case Execution and Test Report Output Requirements

### 4.1 Quality Gate Requirements (Regression Strategy)
Before each test submission or code merge (Pull Request), the following quality gates must pass:
1.  **Unit tests**:
    *   Run `./build_cpu/cosmo-tests` in the build directory, ensuring all Catch2 unit test cases pass (100% Pass).
2.  **Code style and static analysis**:
    *   Run `bash scripts/format_check.sh --check`; the code format must conform to the `.clang-format` specification.
    *   Run `bash scripts/static_analysis.sh --cppcheck`, with no high-risk Warnings.
3.  **Smoke testing**:
    *   Use `docker compose -f docker-compose.x86.windows.yml up -d --build` to launch in one click, ensuring the first-experience path (Login -> Video Access -> Assign Helmet Detection -> Live View OSD success) works smoothly.

### 4.2 Test Result Output Specification
After testing is complete, the test team must submit a "Test Report" that includes:
*   **Case execution summary**: Total cases, passed, failed, waived, pass rate (target: 100% pass rate for functional testing).
*   **Outstanding bug list**: Classified by severity (Blocker, Critical, Major, Minor).
*   **Performance benchmark data table**: Records of CPU usage, memory usage, average E2E latency, and FPS jitter rate under different channel counts on BM1688/x86.
*   **Stability monitoring charts**: 72-hour memory consumption curve (proving no Memory Leak).

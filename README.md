<!--
Repository metadata suggestion:

Description:
Production-grade C++ edge AI engine for video analytics, with visual pipeline orchestration and on-device VLM support.

Topics:
cpp, c-plus-plus, computer-vision, video-analytics, edge-ai, edge-computing,
object-detection, video-processing, rtsp, webrtc, mqtt, inference,
visual-programming, workflow-orchestration, industrial-ai,
vision-language-model, vlm, sophon, bm1688, real-time
-->

<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**Production-grade C++ edge AI engine for video analytics, with visual pipeline orchestration and on-device VLM support**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#c-native-runtime)
[![Platform](https://img.shields.io/badge/platform-Sophon%20BM1688%20%2F%20x86%20Linux%20%2F%20Windows-purple?style=flat-square)](#supported-platforms)
[![Release](https://img.shields.io/badge/release-v0.1.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Stress Test](https://img.shields.io/badge/stress%20test-200%20video%20samples-brightgreen?style=flat-square)](#validation)
[![Pipelines](https://img.shields.io/badge/pipelines-18%2F18%20validated-brightgreen?style=flat-square)](#validation)

[Quick Start](#quick-start) | [Features](#key-features) | [Validation](#validation) | [Docs](#documentation) | [Hardware](#cosmoedge-ready-devices)

[English](README.md) | [简体中文](README.zh-CN.md)

</div>

---

<!--
TODO before public launch:
- Replace all placeholder image paths with real assets.
- Confirm Quick Start commands.
- Confirm public benchmark values.
- Confirm final public release assets and URLs.
- Confirm certified hardware URL.
-->

<div align="center">

https://github.com/user-attachments/assets/23a014a5-d753-432f-8de5-c750bc82d8e2

</div>

*Multiple AI pipelines, real-time OSD, and live event output on one edge device.*

CosmoEdge is a C++ native edge AI engine for building production-oriented video analytics systems on edge devices. It turns models into visual, manageable applications: import a model, compose a pipeline, connect video sources, watch AI overlays in the browser, and push structured events to MQTT or HTTP.

The runtime is built in C++ for efficient multi-channel video processing, hardware decoding, OSD rendering, and low-overhead edge deployment. Python remains excellent for research, experimentation, and model tooling; CosmoEdge is optimized for the part where integrators need long-running edge applications with predictable resource usage.

Instead of stopping at an inference API or demo script, CosmoEdge focuses on the next step: helping integrators turn AI models into complete edge applications that can be deployed, monitored, debugged, and maintained.

## What You Can Build

- Multi-camera safety monitoring with real-time OSD and event snapshots.
- People counting, line crossing, zone intrusion, and traffic statistics.
- Visual inspection workflows powered by on-device VLM prompts.
- Long-tail object detection with text-prompted GroundingDINO.
- End-to-end edge AI systems with model management, scenario tasks, alarms, and data push.

## Key Features

### C++ Native Runtime

CosmoEdge is built around a C++17 runtime rather than a Python service loop. This matters for edge video systems where decode, inference scheduling, OSD rendering, event generation, and stream output all run continuously on resource-constrained devices.

- Lower runtime overhead for long-running multi-channel video workloads.
- Direct integration with hardware decode, NPU runtime, memory pools, and streaming components.
- Better fit for appliance-style edge devices where predictable CPU, memory, and thread behavior matter.
- Same engine core for x86 developer mode and Sophon production deployment.

### Visual Pipeline Orchestration

Build video AI workflows in a browser. Connect video sources, AI models, post-processing nodes, OSD rendering, alarm rules, and output channels with a visual pipeline editor.

<div align="center">

https://github.com/user-attachments/assets/94b9418b-36c8-47b6-a730-ad8f508a6709

</div>

### Complete Application Loop

CosmoEdge links the runtime, web console, and integration layer into one field workflow:

```text
Model Repository -> Scenario Task -> Real-time Analysis -> Alarm Management -> Data Push
       |                  |                 |                   |              |
  Upload/manage       Configure        Multi-channel        Rule engine   MQTT / HTTP
  ONNX/bmodel         pipelines        AI + OSD overlay     snapshots     webhook
  model versions      per scene        WebRTC streaming     event log     integration
```

<details>
<summary><b>Full capability list</b></summary>

| Module             | Capabilities                                                             |
| ------------------ | ------------------------------------------------------------------------ |
| Model Repository   | Model upload, metadata management, version management, hot-swap workflow |
| Scenario Tasks     | Pipeline binding, camera binding, scheduling, scene-level configuration  |
| Real-time Analysis | RTSP, video files, USB cameras, WebRTC live view, HTTP-FLV fallback      |
| Image Analysis     | Batch image upload, VLM analysis, structured results                     |
| Alarm Management   | Rule-based alarms, severity levels, snapshots, filtering, event history  |
| Data Integration   | MQTT push, HTTP webhook, structured JSON event format                    |
| System Management  | Dashboard, device status, user auth, i18n, configuration management      |

</details>

### Real-time Visual Debugging

CosmoEdge includes a production-oriented OSD system designed for operators and developers:

- Business labels instead of raw model class names.
- Semantic colors for normal, warning, violation, and uncertain states.
- Zone overlays, line-crossing indicators, counters, and event panels.
- Debug view for raw detections, confidence scores, track IDs, and model output.

### Prompt-driven AI: GroundingDINO + VLM

CosmoEdge supports prompt-driven visual intelligence on edge devices. GroundingDINO and VLM are part of the same capability family, but they solve different problems:

| Capability         | How it works                                    | Typical use                                           |
| ------------------ | ----------------------------------------------- | ----------------------------------------------------- |
| GroundingDINO      | Text prompt -> open-vocabulary object detection | Find long-tail objects without task-specific training |
| Edge VLM           | Closed question -> YES/NO state judgment        | "Is the cabinet door open?" -> alarm on YES           |
| VLM Image Analysis | Image upload -> structured visual check         | Quality inspection, compliance review                 |

<div align="center">

https://github.com/user-attachments/assets/212a33a8-e662-4678-9945-02c78d808e4d

</div>

GroundingDINO finds what and where. VLM judges whether a visual state is true. Both can be used as asynchronous pipeline nodes alongside traditional CV pipelines.

CosmoEdge supports compatible Qwen3 VLM series models and Qwen3.5 multimodal models as edge VLM nodes. Certified device packages can provide `CosmoEdge-VL-Judge-0.8B`, a 0.8B model package optimized for YES/NO visual state judgment.

### Model Sources

**Integrated end-to-end in CosmoEdge:**

The capabilities below are integrated end-to-end; different model types map to different runtime modes.

| Category                  | Supported Models / Architectures                                    | Pipeline Support    |
| :------------------------ | :------------------------------------------------------------------ | :------------------ |
| Object Detection          | YOLOv5, YOLOv8, YOLOv10, YOLOv11, YOLOv12, YOLO26                   | Full pipeline       |
| Object Tracking           | ByteTrack                                                           | Full pipeline       |
| Attribute Classification  | Safety helmet, vest, uniform classifiers                            | Full pipeline       |
| Counting & Statistics     | Line crossing, zone counting, directional flow                      | Full pipeline       |
| Open-vocabulary Detection | GroundingDINO                                                       | Async pipeline node |
| Visual State Judgment     | Qwen3 VLM models, Qwen3.5 multimodal models (text prompt -> YES/NO) | Async pipeline node |
| Image Analysis            | VLM batch analysis                                                  | Standalone task     |

**Model ecosystem compatibility:**

CosmoEdge uses ONNX as the model interchange format. Models from major CV training frameworks can be imported through a documented conversion path:

- **Ultralytics (YOLO)**: Export with `yolo export format=onnx`, then import via Model Repository or convert to bmodel for NPU deployment.
- **Roboflow**: Train on Roboflow, export ONNX, import into CosmoEdge.
- **Custom models**: Any ONNX-compatible detection or classification model can be integrated through the model porting guide.

**Broader Sophon model ecosystem:**

CosmoEdge runs on the Sophon BM1688 inference stack. Models from SOPHGO's official model zoo can be integrated through the model porting guide, which covers post-processing adaptation and pipeline node registration.

→ [SOPHGO Model Zoo (sophon-demo)](https://github.com/sophgo/sophon-demo)
→ [CosmoEdge Model Porting Guide](docs/en/tutorials/05-model-porting/model-porting.md)

## Quick Start

### Option A: x86 Developer Mode

No edge hardware is required for the first experience. The x86 developer mode uses the same UI and workflow as edge deployment, with lower throughput than Sophon NPU mode.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. Start in x86 mode
# Preferred release target (Linux):
sudo docker compose -f docker-compose.x86.yml up -d --build

# Windows (PowerShell/CMD):
docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. Open the web console
# http://localhost:8080
```

> **USB cameras**: If you have USB cameras attached, uncomment the `devices` block in `docker-compose.x86.yml` before starting.

Once running, follow the [Scenario Configuration tutorial](docs/en/tutorials/02-scenario-config/scenario-config.md) to set up your first AI detection scenario.

### Option B: Sophon Edge Device

Use this path for NPU-accelerated deployment.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. Build the Sophon/aarch64 package
sudo bash scripts/build_sophon_package.sh

# 3. View exported release packages
ls -lh build_output/
# The output package will be named like: cosmo-V<version>-<hash>.tar.gz

# 4. Copy the package to the Sophon edge device (replace <device_ip> with actual IP, default is 192.168.100.1)
scp build_output/cosmo-V*.tar.gz root@<device_ip>:/tmp/

# 5. SSH to the device, extract the package, and run the installation script
ssh root@<device_ip>
cd /tmp
tar -zxvf cosmo-V*.tar.gz
sudo bash scripts/install.sh

# 6. Reboot the device to start the services
sudo reboot
```

On Windows PowerShell to build the package:

```powershell
.\scripts\build_sophon_package.ps1
```

After installing the package and rebooting the device:

- **Default IP**: `192.168.100.1` (ensure your computer is configured with a static IP in the `192.168.100.x` subnet to connect directly)
- **Web Console URL**: `http://192.168.100.1`
- **Default Username**: `admin`
- **Default Password**: `admin` (it is highly recommended to change this password after your first login)

This path builds, exports, and installs release packages. Ready for production hardware? Certified CosmoEdge devices provide preconfigured Sophon acceleration, production model packages, and deployment support. See [CosmoEdge-ready devices](#cosmoedge-ready-devices).

## Validation

CosmoEdge is built from a commercial codebase and has gone through recent internal system validation before open-source release.

| Area                   | Current validation status                                                                                    |
| ---------------------- | ------------------------------------------------------------------------------------------------------------ |
| Video stress test      | 200 video samples used in continuous playback testing, with no memory leak or crash observed during the test |
| CV pipeline validation | 18/18 CV pipelines precision-aligned against internal industry baselines                                     |
| Concurrent CV workload | 16-channel CV inference verified on a single BM1688 device                                                   |
| Regression testing     | Multi-round system regression with dedicated QA                                                              |
| Pilot deployments      | Validated in de-identified pilot scenarios across education, smart campus, and industrial safety             |

### Performance Benchmarks

The numbers below are representative system-level combinations based on internal records. A video channel means one decoded input stream; multiple scenario tasks can share the same decoded stream. E2E latency means frame-to-OSD or frame-to-event latency under the listed workload, not single-model inference time.

| Workload                      | Video channels | Scenario task num |  FPS target |   E2E latency | Hardware                                   | Notes                                                                                                    |
| ----------------------------- | -------------: | ----------------: | ----------: | ------------: | ------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| Full-stream YOLOv8n detection |             16 |                16 |   3/channel |     32-68(ms) | BM1688                                     | Decode + inference + OSD enabled; stable upper-limit case                                                |
| Shared-codec dense CV tasks   |              4 |                20 |   3/channel |    84-141(ms) | BM1688                                     | Multiple scenario tasks share decoded streams; demonstrates task concurrency                             |
| Safety compliance pipeline    |             16 |                16 |   3/channel |   182-314(ms) | BM1688                                     | Detection + tracking + attribute/rule + alarm; representative business pipeline                          |
| Prompt-driven AI pipeline     |              8 |                 8 | 0.2/channel | 3154-4128(ms) | BM1688                                     | Validated `CosmoEdge-VL-Judge-0.8B`; VLM async nodes; event-driven slow path, not frame-synchronous OSD |
| x86 developer mode            |              1 |                 1 |   3/channel |     45-50(ms) | x86 CPU<br />(Intel(R) Core(TM) i9-13900F) | YOLOv8n development and evaluation workload                                                              |

## Architecture

```text
+---------------------------------------------------------------+
| Web Frontend                                                  |
| Pipeline Editor | Management Console | Real-time View          |
+-------------------------------+-------------------------------+
                                | REST / WebSocket / MQTT
                                v
+---------------------------------------------------------------+
| C++ Engine Core                                               |
| Flow Engine | Media Pipeline | Inference | Services            |
| Task/Action | Decode/Encode | CV/VLM/DINO | Alarm/Event/Model  |
+-------------------------------+-------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Hardware Abstraction                                          |
| Sophon BM1688 NPU/VPU/VPP | x86 CPU                           |
+---------------------------------------------------------------+
```

### Tech Stack

| Layer       | Technology                                  |
| ----------- | ------------------------------------------- |
| Engine      | C++17, CMake, FFmpeg, SQLiteCpp             |
| Inference   | Sophon BMRT/SAIL, ONNX Runtime for x86 mode |
| Frontend    | Vue.js, Vue Flow, Element Plus              |
| Streaming   | SRS 6.0, WebRTC, HTTP-FLV                   |
| Integration | REST API, WebSocket, MQTT, HTTP webhook     |

## Supported Platforms

| Platform       |  Status  | Intended use                                 |
| -------------- | :-------: | -------------------------------------------- |
| Sophon BM1688  |  Primary  | NPU-accelerated production deployment        |
| x86 Linux      | Supported | Development, evaluation, integration testing |
| x86 Windows    | Supported | Development and evaluation                   |
| Sophon BM1684X |  Planned  | NPU-accelerated deployment                   |

## CosmoEdge-ready Devices

CosmoEdge is open source. The repository provides the same engine, web UI, and workflow used by certified device packages. You can bring your own models, run on x86 for development, and deploy on compatible edge hardware.

Certified devices are for teams that want to skip hardware bring-up and model packaging. They add preconfigured NPU acceleration, production model packages, and dedicated support.

| Capability                   |               Open-source repository               |    Certified device package    |
| ---------------------------- | :------------------------------------------------: | :-----------------------------: |
| C++ engine                   |                      Included                      |            Included            |
| Visual pipeline orchestrator |                      Included                      |            Included            |
| Web management console       |                      Included                      |            Included            |
| x86 developer mode           |                      Included                      |            Included            |
| Sophon NPU runtime support   |         Source support, hardware required         |          Preconfigured          |
| CV model package             |               Bring your own models               |          Pre-installed          |
| `CosmoEdge-VL-Judge-0.8B`  | Bring your own/custom package; validation required | Pre-installed validated package |
| GroundingDINO package        |          Bring your own or custom package          |          Pre-installed          |
| Deployment support           |                     Community                     |            Dedicated            |

Certified devices add deployment readiness, not locked software features.

<!-- TODO: Replace with final hardware page URL. -->

[Get a certified device](https://cosmoedge.dev/hardware)

## Documentation

| Start here                                                                                   | For            | Description                               |
| -------------------------------------------------------------------------------------------- | -------------- | ----------------------------------------- |
| [Documentation Home](docs/en/index.md)                                                          | Everyone       | Full documentation index and reading path |
| [Quick Start Guide](docs/en/tutorials/01-quickstart/quickstart.md)                              | Everyone       | First working experience                  |
| [Scenario Configuration](docs/en/tutorials/02-scenario-config/scenario-config.md)               | Integrators    | Build scene-level AI workflows            |
| [VLM Guide](docs/en/tutorials/03-vlm-guide/vlm-guide.md)                                        | Developers     | Use visual state judgment with prompts    |
| [Pipeline Orchestration](docs/en/tutorials/04-pipeline-orchestration/pipeline-orchestration.md) | Advanced users | Compose custom pipelines visually         |
| [Model Porting Guide](docs/en/tutorials/05-model-porting/model-porting.md)                      | ML engineers   | Bring your own ONNX or target model       |
| [Build Guide](docs/en/guide/build.md)                                                           | Developers     | Build x86 Docker and Sophon packages      |
| [API Overview](docs/en/reference/api.md)                                                        | Developers     | REST/WebSocket/MQTT-facing API categories |

## Roadmap

- [X] C++17 edge inference engine
- [X] Visual pipeline orchestrator
- [X] Web management console
- [X] x86 developer mode for Linux and Windows
- [X] VLM and GroundingDINO integration
- [X] 18 CV pipelines internally validated
- [ ] Public x86 one-command startup
- [ ] Additional public performance summaries
- [ ] Release packaging for v1.0
- [ ] Community model and scenario examples
- [ ] GB28181 protocol support

## Contributing

CosmoEdge is in active development toward v1.0. Contributions are welcome in focused areas:

- Bug reports with logs and reproduction steps.
- Documentation fixes and tutorial improvements.
- Scenario examples and integration notes.
- Small, scoped pull requests discussed through issues first.

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

## FAQ

<details>
<summary><b>Do I need a Sophon device to try CosmoEdge?</b></summary>

No. Use x86 developer mode on Linux or Windows to try the UI, pipeline workflow, model management, and integration path. Sophon hardware is needed for production-level NPU throughput.

</details>

<details>
<summary><b>Does the open-source repository include model weights?</b></summary>

The open-source repository does not include production model weights by default. You can bring your own models, including compatible Qwen3 VLM series models and Qwen3.5 multimodal models. Certified device packages can provide pre-installed production CV models, `CosmoEdge-VL-Judge-0.8B`, and GroundingDINO. Community or custom models should be validated for the target scenario.

</details>

<details>
<summary><b>Can I use my own trained models?</b></summary>

Yes. CosmoEdge is designed around model import and model lifecycle management. The final public guide will document the recommended path from ONNX or target runtime formats into the model repository.

</details>

<details>
<summary><b>How is CosmoEdge different from inference servers or NVR projects?</b></summary>

CosmoEdge is an application runtime for complete edge AI workflows, not only a model-serving layer or video recorder.

</details>

<details>
<summary><b>Is CosmoEdge production-ready?</b></summary>

The codebase comes from production-oriented commercial development and has passed internal stress, pipeline, and regression validation. The first public release is still marked v0.1.0 while public APIs, packaging, and contributor workflows stabilize toward v1.0.

</details>

## Contact

- 💬 Community: [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions)
- 📧 Partnership & Enterprise: hello@cosmowander.ai
- 🔒 Security: see [SECURITY.md](SECURITY.md) for private vulnerability reporting

## License

CosmoEdge is licensed under the [Apache License 2.0](LICENSE).

```text
Copyright 2026 CosmoEdge Contributors

Licensed under the Apache License, Version 2.0
```

---

<div align="center">

Built by Cosmos Wanderer AI Technology

Industrial edge intelligence, from model to production.

</div>

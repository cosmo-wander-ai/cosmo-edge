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

<!-- TODO: Replace with final logo asset. -->

<!-- <img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge"> -->

# CosmoEdge

**Production-grade C++ edge AI engine for video analytics, with visual pipeline orchestration and on-device VLM support**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#cpp-native-runtime)
[![Platform](https://img.shields.io/badge/platform-Sophon%20BM1688%20%2F%20x86%20Linux%20%2F%20Windows-purple?style=flat-square)](#supported-platforms)
[![Release](https://img.shields.io/badge/release-v0.1.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Stress Test](https://img.shields.io/badge/stress%20test-200%20video%20samples-brightgreen?style=flat-square)](#validation)
[![Pipelines](https://img.shields.io/badge/pipelines-18%2F18%20validated-brightgreen?style=flat-square)](#validation)

[Quick Start](#quick-start) | [Features](#key-features) | [Showcases](#showcases) | [Validation](#validation) | [Docs](#documentation) | [Hardware](#cosmoedge-ready-devices)

[English](README.md) | [简体中文](README.zh-CN.md)

</div>

---

<!--
TODO before public launch:
- Replace all placeholder image paths with real assets.
- Confirm Quick Start commands.
- Confirm benchmark values and attach a reproducible test report.
- Confirm final public release assets and URLs.
- Confirm certified hardware URL.
-->

<div align="center">

![CosmoEdge running multiple edge AI pipelines](docs/assets/hero.gif)

*Multiple AI pipelines, real-time OSD, and live event output on one edge device.*

</div>

CosmoEdge is a C++ native edge AI engine for building production-oriented video analytics systems on edge devices. It turns models into visual, manageable applications: import a model, compose a pipeline, connect video sources, watch AI overlays in the browser, and push structured events to MQTT or HTTP.

The runtime is built in C++ for efficient multi-channel video processing, hardware decoding, OSD rendering, and low-overhead edge deployment. Python remains excellent for research, experimentation, and model tooling; CosmoEdge is optimized for the part where integrators need long-running edge applications with predictable resource usage.

Instead of stopping at an inference API or demo script, CosmoEdge focuses on the next step: helping integrators turn AI models into complete edge applications that can be deployed, monitored, debugged, and maintained.

## What You Can Build

- Multi-camera safety monitoring with real-time OSD and event snapshots.
- People counting, line crossing, zone intrusion, and traffic statistics.
- Visual inspection workflows powered by on-device VLM prompts.
- Long-tail object detection with text-prompted GroundingDINO.
- End-to-end edge AI systems with model management, scenario tasks, alarms, and data push.

## Screenshots

<!--
TODO: Replace placeholders with real screenshots.
Recommended set:
1. Visual Pipeline Orchestrator
2. Real-time AI Analytics
3. Web Management Console
4. VLM Visual Inspection
-->

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

![Pipeline editor workflow](docs/assets/pipeline-editor.gif)

</div>

### Complete Application Loop

CosmoEdge is not only an inference runtime. It includes the application layer needed to operate AI vision systems in the field.

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

### Multi-channel Edge Runtime

The C++ engine is designed for multi-channel video analytics on edge hardware. On Sophon BM1688, CosmoEdge has been internally verified with 16-channel CV inference workloads.

> TODO: Publish the exact benchmark setup before launch: device model, SDK version, resolution, codec, input source, model version, duration, and whether OSD/streaming are included.

### Prompt-driven AI: GroundingDINO + VLM

CosmoEdge supports prompt-driven visual intelligence on edge devices. GroundingDINO and VLM are part of the same capability family, but they solve different problems:

| Capability         | How it works                                    | Typical use                                           |
| ------------------ | ----------------------------------------------- | ----------------------------------------------------- |
| GroundingDINO      | Text prompt -> open-vocabulary object detection | Find long-tail objects without task-specific training |
| Edge VLM           | Closed question -> YES/NO/Enum state judgment   | "Is the cabinet door open?" -> alarm on YES           |
| VLM Image Analysis | Image upload -> structured visual check         | Quality inspection, compliance review                 |

<div align="center">

![Prompt-driven AI with GroundingDINO and VLM](docs/assets/prompt-driven-ai.gif)

</div>

GroundingDINO finds what and where. VLM judges whether a visual state is true. Both can be used as asynchronous pipeline nodes alongside traditional CV pipelines.

Certified device packages can include a fine-tuned 0.8B edge VLM and production-ready CV models. The open-source engine can also run user-provided models through the same workflow.

> TODO: Confirm the public model name, parameter count, quantization format, and measured latency before using the 0.8B VLM claim in a release.

### Zero-barrier Developer Mode

Try the full UI and workflow on standard x86 hardware:

- x86 Linux and Windows support for development and evaluation.
- Same UI and workflow as edge deployment.
- Lower throughput than Sophon NPU mode, but enough for onboarding, testing, and integration work.

### Model Sources

**Validated end-to-end in CosmoEdge:**

Models listed below have full pipeline support — detection, OSD rendering, tracking, alarm rules, and event output work out of the box.

| Category                  | Verified Architectures                         | Pipeline Support    |
| :------------------------ | :--------------------------------------------- | :------------------ |
| Object Detection          | YOLOv5, YOLOv8, YOLOv10, YOLOv11               | Full pipeline       |
| Object Tracking           | ByteTrack                                      | Full pipeline       |
| Attribute Classification  | Safety helmet, vest, uniform classifiers       | Full pipeline       |
| Counting & Statistics     | Line crossing, zone counting, directional flow | Full pipeline       |
| Open-vocabulary Detection | GroundingDINO                                  | Async pipeline node |
| Visual State Judgment     | Edge VLM (text prompt → YES/NO/Enum)          | Async pipeline node |
| Image Analysis            | VLM batch analysis                             | Standalone task     |

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

No edge hardware is required for the first experience.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. Start in x86 mode
# TODO: Confirm the final public launch command.
# Preferred release target (Linux):
sudo docker compose -f docker-compose.x86.yml up -d --build

# Windows (PowerShell/CMD):
docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. Open the web console
# http://localhost:8080
```

Expected first path:

```text
Open browser -> import or select model -> create scenario task -> connect video -> view AI results
```

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

## Showcases

Representative application pipelines built with the same engine, UI, and event system:

| Scenario                 | Pipeline                                                        | What it demonstrates                                    |
| ------------------------ | --------------------------------------------------------------- | ------------------------------------------------------- |
| Pedestrian Flow Analysis | Detection -> tracking -> line crossing -> counting -> MQTT      | Multi-stage CV pipeline with real-time statistics       |
| Construction Site Safety | Person/PPE detection -> zone rule -> alarm -> snapshot -> OSD   | Compliance monitoring with semantic overlays and alarms |
| Visual Inspection        | DINO object localization -> VLM state judgment -> event mapping | Prompt-driven long-tail inspection without retraining   |

Additional scene GIFs can be added here later. The three launch GIFs are intentionally used for the three strongest README proof points: live edge runtime, visual orchestration, and prompt-driven AI.

## Validation

CosmoEdge is built from a commercial codebase and has gone through internal system validation before open-source release.

| Area                   | Current validation status                                                                                    |
| ---------------------- | ------------------------------------------------------------------------------------------------------------ |
| Video stress test      | 200 video samples used in continuous playback testing, with no memory leak or crash observed during the test |
| CV pipeline validation | 18/18 CV pipelines precision-aligned against internal industry baselines                                     |
| Concurrent CV workload | 16-channel CV inference verified on a single BM1688 device                                                   |
| Regression testing     | Multi-round system regression with dedicated QA                                                              |
| Pilot deployments      | Validated in de-identified pilot scenarios across education, smart campus, and industrial safety             |

> TODO: Link to a public validation report before formal v1.0 release. The report should include test duration, device configuration, input resolution, model versions, and known limitations.

### Performance Benchmarks

The numbers below are representative system-level combinations based on internal records. A video channel means one decoded input stream; multiple scenario tasks can share the same decoded stream. E2E latency means frame-to-OSD or frame-to-event latency under the listed workload, not single-model inference time.

| Workload                      | Video channels | Scenario task num |  FPS target |   E2E latency | Hardware | Notes                                                                           |
| ----------------------------- | -------------: | ----------------: | ----------: | ------------: | -------- | ------------------------------------------------------------------------------- |
| Full-stream YOLOv8n detection |             16 |                16 |   3/channel |     32-68(ms) | BM1688   | Decode + inference + OSD enabled; stable upper-limit case                       |
| Shared-codec dense CV tasks   |              4 |                20 |   3/channel |    84-141(ms) | BM1688   | Multiple scenario tasks share decoded streams; demonstrates task concurrency    |
| Safety compliance pipeline    |             16 |                16 |   3/channel |   182-314(ms) | BM1688   | Detection + tracking + attribute/rule + alarm; representative business pipeline |
| Prompt-driven AI pipeline     |              8 |                 8 | 0.2/channel | 3154-4128(ms) | BM1688   | VLM async nodes; event-driven slow path, not frame-synchronous OSD（QW3.5 0.8b) |
| x86 developer mode            |              1 |                 1 |        TODO |          TODO | x86 CPU  | YOLOv8n development and evaluation workload                                     |

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
| Sophon BM1688 NPU/VPU/VPP | x86 CPU/OpenCV                    |
+---------------------------------------------------------------+
```

### Tech Stack

| Layer       | Technology                                  |
| ----------- | ------------------------------------------- |
| Engine      | C++17, CMake, FFmpeg, OpenCV, SQLiteCpp     |
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

| Capability                   |      Open-source repository      | Certified device package |
| ---------------------------- | :-------------------------------: | :----------------------: |
| C++ engine                   |             Included             |         Included         |
| Visual pipeline orchestrator |             Included             |         Included         |
| Web management console       |             Included             |         Included         |
| x86 developer mode           |             Included             |         Included         |
| Sophon NPU runtime support   | Source support, hardware required |      Preconfigured      |
| CV model package             |       Bring your own models       |      Pre-installed      |
| 0.8B edge VLM                | Bring your own or custom package |      Pre-installed      |
| GroundingDINO package        | Bring your own or custom package |      Pre-installed      |
| Deployment support           |             Community             |        Dedicated        |

Certified devices add deployment readiness, not locked software features.

<!-- TODO: Replace with final hardware page URL. -->

[Get a certified device](https://cosmoedge.dev/hardware)

## Documentation

| Document               | Audience       | Description                                 |
| ---------------------- | -------------- | ------------------------------------------- |
| [Quick Start Guide](docs/en/tutorials/01-quickstart/quickstart.md) | Everyone | First working experience in minutes |
| [Scenario Configuration](docs/en/tutorials/02-scenario-config/scenario-config.md) | Integrators | Build scene-level AI workflows |
| [VLM Guide](docs/en/tutorials/03-vlm-guide/vlm-guide.md) | Developers | Use visual state judgment with prompts |
| [Pipeline Orchestration](docs/en/tutorials/04-pipeline-orchestration/pipeline-orchestration.md) | Advanced users | Compose custom pipelines visually |
| [Model Porting Guide](docs/en/tutorials/05-model-porting/model-porting.md) | ML engineers | Bring your own ONNX or target model |
| [Build Guide](docs/en/guide/build.md) | Developers | Confirmed x86 Docker and Sophon package build paths |
| [Deployment Guide](docs/en/guide/deployment.md) | DevOps | Runtime paths, ports, processes, packages, and systemd |
| [Configuration](docs/en/guide/configuration.md) | Operators | Environment variables, resource paths, ports, logs, and runtime defaults |
| [Troubleshooting](docs/en/guide/troubleshooting.md) | Operators | Common build, runtime, port, Sophon image, and docs-site issues |
| [API Overview](docs/en/reference/api.md) | Developers | Current REST/WebSocket/MQTT-facing API categories |
| [API Fields](docs/en/reference/api-fields.md) | Integrators | Common response, event, HTTP push, MQTT, and IoT network fields |
| [MQTT Reference](docs/en/reference/mqtt.md) | Integrators | MQTT topics, envelope format, registration, heartbeat, requests, and responses |
| [HTTP Webhook Reference](docs/en/reference/webhook.md) | Integrators | Event push configuration, payload fields, and receiver guidance |
| [Architecture Overview](docs/en/guide/architecture.md) | Contributors | Engine internals and extension points |
| [Frontend Development](docs/en/development/frontend.md) | Frontend developers | Vue 3 frontend structure and scripts |
| [I18N Glossary](docs/i18n/GLOSSARY.md) | Frontend developers | UI terminology, default English labels, and short-label rules |
| [I18N Short Scope Rules](docs/i18n/SHORT-SCOPES.md) | Frontend developers | Controlled compact-label scope IDs |
| [Backend Development](docs/en/development/backend.md) | C++ developers | Backend modules, CMake options, and tests |
| [CI and Quality Checks](docs/en/development/ci.md) | Contributors | Documentation, frontend, C++ formatting, static analysis, and release checks |
| [Security Policy](SECURITY.md) | Maintainers | Vulnerability reporting and deployment security notes |
| [Notice](NOTICE) | Maintainers | Project notice and third-party attribution information |
| [Changelog](CHANGELOG.md) | Maintainers | Public change history |

## Roadmap

- [X] C++17 edge inference engine
- [X] Visual pipeline orchestrator
- [X] Web management console
- [X] x86 developer mode for Linux and Windows
- [X] VLM and GroundingDINO integration
- [X] 18 CV pipelines internally validated
- [ ] Public x86 one-command startup
- [ ] Public reproducible benchmark dataset and reports
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

The open-source repository focuses on the engine, UI, and workflow. You can bring your own models and follow the model porting guide. Certified device packages can include pre-installed production CV models, a fine-tuned edge VLM, and GroundingDINO.

</details>

<details>
<summary><b>Can I use my own trained models?</b></summary>

Yes. CosmoEdge is designed around model import and model lifecycle management. The final public guide will document the recommended path from ONNX or target runtime formats into the model repository.

</details>

<details>
<summary><b>How is CosmoEdge different from inference servers or NVR projects?</b></summary>

CosmoEdge combines three layers in one edge system: a C++ video AI runtime, a visual pipeline builder, and a web management console for scenarios, alarms, model lifecycle, OSD, and data push. The goal is not only to run models, but to help integrators operate complete edge AI applications.

</details>

<details>
<summary><b>Is CosmoEdge production-ready?</b></summary>

The codebase comes from production-oriented commercial development and has passed internal stress, pipeline, and regression validation. The first public release is still marked v0.1.0 while public APIs, packaging, and contributor workflows stabilize toward v1.0.

</details>

## Contact

- 💬 Community: [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions)
- 📧 Partnership & Enterprise: hello@cosmowander.ai

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

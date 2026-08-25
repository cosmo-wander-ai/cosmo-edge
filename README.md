<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**Turn video AI models into deployable edge applications — a C++ edge AI engine for Sophon, Rockchip, and x86.**

Build and operate video analytics, VLM, and event workflows through a consistent orchestration experience. Each platform uses its own runtime, build, and model artifacts.

[![Nightly Sophon Build and Test](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml)
[![Rockchip Cross Build](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/ci-build-rockchip.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/ci-build-rockchip.yml)

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#core-capabilities)
[![Release](https://img.shields.io/badge/release-v1.1.0-2ea44f?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.1.0)

[![Website](https://img.shields.io/badge/website-cosmowander.ai-3B82F6?style=flat-square)](https://www.cosmowander.ai/)
[![Docs](https://img.shields.io/badge/docs-online-2563EB?style=flat-square)](https://www.cosmowander.ai/docs/)
[![Gitee](https://img.shields.io/badge/Gitee-cosmo--edge-C71D23?style=flat-square&logo=gitee)](https://gitee.com/cosmo-wander-ai/cosmo-edge)

[Quick Start](#quick-start) · [Platforms](#choose-a-platform) · [Validation](#validation) · [Documentation](#documentation-devices-and-community) · [简体中文](README.zh-CN.md)

</div>

---

<div align="center">

<https://github.com/user-attachments/assets/23a014a5-d753-432f-8de5-c750bc82d8e2>

</div>

CosmoEdge goes beyond model serving with a complete application layer for model import, visual orchestration, alarms, and event delivery. The core engine and console in this repository are released under Apache-2.0; certified hardware, commercial preset models, and Model Guard distribution protection have separate boundaries.

## CosmoEdge 1.1

- **Multi-platform release:** BM1688, CV186X, RK3576, and RV1126B use the same video-ingest, orchestration, event, and observability workflow with target-specific model artifacts.
- **Public benchmark pack:** the [CosmoEdge 1.1 multi-platform report](docs/benchmarks/scenario-bench/v1.1/README.md) covers single workloads, concurrent mixed workloads, a controlled 72-hour dual-CV profile, and validated VLM performance with sanitized, traceable benchmark artifacts.
- **Rockchip release platforms:** RK3576 and RV1126B both include cross-build, board operation, RKNN inference, and MPP/RGA media paths in the same v1.1 release tier; their models and measured evidence remain bound separately.
- **Sophon model handling:** chip-aware validation supports target-specific `.nn` artifacts for BM1688 and CV186X. The benchmark records an exact Open-package and running-engine binding for both reference devices.
- **RKNN data path:** targeted DMA-BUF-to-RGA input, persistent bound-input, native quantized output, and direct YOLOv8 tensor decoding paths with explicit fallbacks.
- **Agent-assisted development:** a repository-guided path for handing model porting, integration, and UI tasks to the coding agent you already use and receiving verifiable deliverables.
- **Model Guard 2.3:** protects commercial preset-model distribution in Sophon Protected packages. Open and Protected expose the same application features, with no SKU-gated software functionality; they differ in model encryption and device-provisioning tooling.
- **macOS Docker Preview:** an isolated `linux/amd64` path for one local-video workflow on Apple Silicon, validated through multiple end-to-end lab rounds.
- **Benchmark scope:** VLM figures are exact short-run gate boundaries for the recorded protocol. The separate 72-hour test covers the listed fixed-channel profiles using controlled local-loop input; detailed conditions are documented in the benchmark report.

## Choose a Platform

CosmoEdge provides one engine architecture and orchestration experience, but each build selects one inference backend and uses models generated for that target platform.

| Platform | Status | Runtime / model artifact | Current scope and evidence |
| --- | --- | --- | --- |
| Sophon BM1688 | v1.1 supported / primary; VLM supported | BMRT / `.nn` | Validated VLM at 0.1 FPS/ch through 6 channels, plus [v1.1 workload evidence](docs/benchmarks/scenario-bench/v1.1/README.md) |
| Rockchip RK3576 / RV1126B | v1.1 supported; RK3576 VLM supported | RKNN / target-specific `.rknn`; RKLLM on RK3576 | RK3576 validates VLM at 0.1 FPS/ch through 4 channels; RV1126B VLM is outside this release claim. Other platform-specific results remain separate in the [v1.1 report](docs/benchmarks/scenario-bench/v1.1/README.md) |
| Sophon CV186X | v1.1 supported; VLM supported | BMRT / target-specific `.nn` | Validated VLM at 0.1 FPS/ch through 6 channels, plus model-import and workload evidence in the [v1.1 benchmark](docs/benchmarks/scenario-bench/v1.1/README.md) |
| x86 Linux / Windows; Apple Silicon macOS | Linux / Windows supported; macOS Preview | ONNX Runtime / `.onnx` | Mac uses amd64 emulation for one local-video developer workflow, not native performance evidence |
| Sophon BM1684X | Planned | — | Not part of the current release scope |

## Quick Start

### Try locally on x86

No edge hardware is required. The x86 mode uses the same UI and workflow with lower throughput than an NPU deployment.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. Start on Linux
sudo docker compose -f docker-compose.x86.yml up -d --build
# Windows: docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. Open http://localhost:8080
```

Apple Silicon macOS uses a separate amd64-emulation Preview path:

```bash
./scripts/macos-docker-preview.sh doctor
./scripts/macos-docker-preview.sh up
./scripts/macos-docker-preview.sh status
# Open http://127.0.0.1:8080
```

The Mac Preview provides a local, single-video workflow under `linux/amd64`
emulation on Apple Silicon. Read the full
[macOS Docker Preview](docs/en/guide/macos-docker-preview.md) scope first.

After startup, use the [Scenario Configuration tutorial](https://www.cosmowander.ai/docs/tutorials/02-scenario-config/scenario-config) to create your first AI detection task. Docker Compose V1 users can replace `docker compose` with `docker-compose`.

### Build for Sophon

```bash
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
# BM1688 (default when the chip model is omitted)
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip bm1688

# CV186X
./scripts/docker-compose.sh -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

The wrapper selects the available Compose implementation and requests elevated
Docker access only when required. The build selects the matching model resources
without a model-path argument and exports each target independently under
`build_output/public-runtime/<chip>/`, together with `TARGET_CHIP` and
`SHA256SUMS`.

The default Open package contains plaintext models and requires no device
authorization. Verify the chip marker and checksum before deployment. The
[Build Guide](https://www.cosmowander.ai/docs/guide/build) is the authoritative
build reference; follow the [Deployment Guide](https://www.cosmowander.ai/docs/guide/deployment)
for SSH installation, web upgrade, recovery, and post-reboot acceptance.

### Build for Rockchip

```bash
./scripts/docker-compose.sh -f docker-compose.rockchip.yml pull cosmo-rockchip-package

# RK3576 (uses the tracked RK3576 model resources)
COSMO_TARGET_CHIP=rk3576 ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
ls -lh build_output/rk3576/

# RV1126B (requires a prepared target model overlay)
COSMO_TARGET_CHIP=rv1126b ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
ls -lh build_output/rv1126b/
```

One digest-pinned Rockchip builder shares the compiler and RKNN SDK while
selecting an isolated MPP/RGA profile for each chip. RKLLM v1.3.0 is required
and packaged only for RK3576. Target markers, media-profile identities, and
checksums are exported under `build_output/<chip>/`. See the
[Build Guide](docs/en/guide/build.md#rockchip-artifacts) and
[RK3576 integration guide](docs/en/guide/rk3576-rknn-development.md) for model
and device-validation boundaries.

For CV186X, follow the [CV186X Quick Start](docs/en/guide/cv186x-quick-start.md) for package installation, model import, first-event verification, upgrade, and recovery boundaries.

## What You Can Build

- **Real-time video analytics:** detection, classification, tracking, zones, counters, OSD, and alarm snapshots.
- **Prompt-driven vision:** VLM state judgment and GroundingDINO open-vocabulary detection alongside conventional CV pipelines.
- **Visual application workflows:** connect models, rules, events, and output actions in the browser.
- **Edge integrations:** operate managed tasks and deliver structured events through REST, WebSocket, MQTT, or HTTP webhooks.

## Core Capabilities

| Capability | What it covers | Go deeper |
| --- | --- | --- |
| Native runtime | C++17 engine for multi-channel media, inference scheduling, OSD, tasks, and events | [Architecture](https://www.cosmowander.ai/docs/guide/architecture) |
| Visual orchestration | Browser-based pipeline composition, task binding, parameter validation, and live feedback | [Pipeline tutorial](https://www.cosmowander.ai/docs/tutorials/04-pipeline-orchestration/pipeline-orchestration) |
| Inference and media | Platform backends for Sophon, RKNN, and x86; platform-specific builds and model artifacts | [Build Guide](https://www.cosmowander.ai/docs/guide/build) |
| VLM and DINO | Prompt-based judgment, open-vocabulary detection, and optional VLM review before a detection alarm is reported | [VLM Guide](https://www.cosmowander.ai/docs/tutorials/03-vlm-guide/vlm-guide) |
| Operations and integration | Model management, alarms, event history, REST, WebSocket, MQTT, and webhooks | [API Overview](https://www.cosmowander.ai/docs/reference/api) |
| Model onboarding and protection | Model conversion, import, validation, and the Open/Protected distribution boundary | [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) |

<details>
<summary>▶ Watch: compose a complete visual pipeline</summary>

<https://github.com/user-attachments/assets/94b9418b-36c8-47b6-a730-ad8f508a6709>

</details>

<details>
<summary>▶ Watch: GroundingDINO and VLM visual workflows</summary>

<https://github.com/user-attachments/assets/212a33a8-e662-4678-9945-02c78d808e4d>

</details>

## Agent-Assisted Development

Already have a model-porting, integration, or UI task? Give your usual coding agent the business goal, available materials, target device or test environment, and acceptance criteria. The repository provides task entry points, examples, checks, and evidence boundaries so the result can include importable artifacts, scoped code changes, and a verifiable conclusion.

Start with [Agent-Assisted Development](docs/en/development/agent-assisted-development.md), then use the [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) or [Contributor Guide](docs/en/development/contributing.md) for the task at hand.

## Validation

### CosmoEdge 1.1 multi-platform performance report

The v1.1 report covers BM1688, CV186X, RK3576, and RV1126B. It includes 49 independent small-model case reports for person detection, no-safety-helmet analysis, and a concurrent mixed workload at 5 FPS per business task, a controlled 72-hour dual-CV configured profile, and validated VLM performance for BM1688, CV186X, and RK3576.

- [English benchmark index](docs/benchmarks/scenario-bench/v1.1/README.md)
- [中文基准报告索引](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)
- [English primary report (rendered documentation site)](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/report.html)
- [72-hour dual-CV report (rendered documentation site)](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/dual-cv-72h/report.html)
- [Methodology and reproduction](docs/benchmarks/scenario-bench/v1.1/methodology.md)

The short-run capacity refresh uses source commit `89c73a7464a81ef378686447d7c1eeb88b988455`, tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`, one fixed 1080p24 input, 30-second steps, and the gates recorded in the [release manifest](docs/benchmarks/scenario-bench/v1.1/release-manifest.json). The 72-hour observation uses source commit `44209759f450e96cda265acfa8bc6d17a1138888`, tree `5cbdefeaefaf642407356c22c271ccc7d57935b0`, and the same controlled input.

| Platform | 72-hour configured channels | Task bindings | Sample coverage | Min / avg FPS | Result |
| --- | ---: | ---: | ---: | --- | --- |
| BM1688 | 8 | 16 | 4316 / 4320 | 4.68 / 5.086 | PASS |
| CV186X | 8 | 16 | 4316 / 4320 | 4.54 / 5.085 | PASS |
| RK3576 | 8 | 16 | 4316 / 4320 | 5.00 / 5.098 | PASS |
| RV1126B | 4 | 8 | 4316 / 4320 | 4.85 / 5.230 | PASS |

These results describe the tested fixed-channel profiles under controlled local-loop input; they are not maximum-capacity or RTSP-resilience measurements. Execution details are documented in the [methodology](docs/benchmarks/scenario-bench/v1.1/methodology.md).

Each channel concurrently runs two business tasks across three model stages: person detection has one detector stage, while no-safety-helmet analysis has a detector followed by a classifier.

| Platform | Workload per channel | Model stages/ch | Target FPS/task | Passing channels | Business-task bindings |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | Person detection + no-safety-helmet analysis | 3 | 5 | ≥16 | 32/32 |
| CV186X | Person detection + no-safety-helmet analysis | 3 | 5 | ≥8 | 16/16 |
| RK3576 | Person detection + no-safety-helmet analysis | 3 | 5 | ≥8 | 16/16 |
| RV1126B | Person detection + no-safety-helmet analysis | 3 | 5 | ≥4 | 8/8 |

The three VLM platforms use one final protocol: each newly added route completes task-local readiness before a 60-second measurement window, and every active route must pass the executed 80% threshold against the 0.1 FPS-per-channel target. Each platform's fixed candidate package also passed model load, task creation, valid inference, event/alarm output, and task recovery after service restart; these claims do not extend to later rebuilt packages.

| Platform | Target FPS/ch | Last passing channels | First failure |
| --- | ---: | ---: | --- |
| BM1688 | 0.1 | 6 channels | 7-channel FPS ratio 69.5%, below the 80% gate |
| CV186X | 0.1 | 6 channels | 7-channel FPS ratio 69.2%, below the 80% gate |
| RK3576 | 0.1 | 4 channels | 5-channel FPS ratio 69.7%, below the 80% gate |
| RV1126B | — | — | Outside this VLM validation |

The capacity values are exact short-run gate boundaries under the recorded conditions—not maximum-capacity certification or production recommendations. The separate 72-hour table verifies only its configured profile. See the [v1.1 benchmark](docs/benchmarks/scenario-bench/v1.1/README.md) for the complete matrices. Previously published data remains available through one [v1.0 historical archive](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/) link.

## Architecture

```text
+------------------------------------------------------------------+
| Web Console | Visual Orchestration | REST / WebSocket / MQTT      |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| C++ Engine Core                                                   |
| Media | Inference | Tasks | Rules | Alarms | Events | Models      |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| Inference and Media Backend Interfaces                            |
+--------------------+----------------------+----------------------+
| Sophon BMRT/VPU    | RKNN + MPP/RGA       | ONNX Runtime/FFmpeg  |
| BM1688; CV186X          | RK3576          | x86 Linux / Windows  |
+--------------------+----------------------+----------------------+
```

One build selects one inference backend. Model artifacts are generated for the target platform, and feature/model coverage and capacity remain platform-specific. Model Guard Protected distribution currently belongs to the Sophon packaging path.

## Documentation, Devices, and Community

| Start here | Best for |
| --- | --- |
| [Documentation Home](https://www.cosmowander.ai/docs/) | Full documentation index and learning path |
| [Quick Start Guide](https://www.cosmowander.ai/docs/tutorials/01-quickstart/quickstart) | First setup and scenario run |
| [Scenario Configuration](https://www.cosmowander.ai/docs/tutorials/02-scenario-config/scenario-config) | Building scene-level workflows |
| [VLM Guide](https://www.cosmowander.ai/docs/tutorials/03-vlm-guide/vlm-guide) | Prompt-based visual judgment and events |
| [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) | Importing your own model |
| [Agent-Assisted Development](docs/en/development/agent-assisted-development.md) | Delegating an extension task with verifiable results |
| [Build Guide](https://www.cosmowander.ai/docs/guide/build) | x86, Sophon, and RK3576 build/package paths |
| [API Overview](https://www.cosmowander.ai/docs/reference/api) | REST, WebSocket, MQTT, and webhook integration |

Certified devices add preconfigured acceleration, validated commercial model packages, and dedicated deployment support; they do not unlock separate software features. Devices are available in mainland China from the [Taobao store](https://item.taobao.com/item.htm?id=1066672051450); contact <hello@cosmowander.ai> for other regions or project support.

Contributions are welcome through scoped bug reports, documentation improvements, scenarios, and integration notes. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

[GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions) is the official searchable English Q&A and community-support channel for v1.1. Send reproducible defects to [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues), use [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) for the mainland China mirror, and report vulnerabilities privately through [SECURITY.md](SECURITY.md). No Discord channel is operated for this release.

## FAQ

<details>
<summary><b>Can I try CosmoEdge without Sophon or Rockchip hardware?</b></summary>

Yes. Use x86 developer mode on Linux or Windows. Apple Silicon Macs can use the Docker Preview for the console, pipeline, model-management, and integration workflow with one local video. The Mac path uses amd64 emulation and does not enable Model Guard. Edge NPU hardware is still required for target-platform acceleration and capacity validation.

</details>

<details>
<summary><b>What is the boundary between the Open and Protected packages?</b></summary>

They expose the same application features and use the same MD5 upgrade lifecycle. Open uses plaintext models without device authorization; Sophon Protected packages can carry encrypted commercial preset models and provisioning tooling that require a device-bound certificate. Application archives themselves are not signed.

</details>

<details>
<summary><b>Can I use my own trained models?</b></summary>

Yes. Use the model-porting path to validate the tensor, preprocessing, post-processing, target runtime, and business accuracy contract. A model artifact must be generated for the platform where it will run.

</details>

<details>
<summary><b>How production-ready is CosmoEdge?</b></summary>

`v1.1.0` is the multi-platform release line for BM1688, CV186X, RK3576, RV1126B, and x86, with VLM support validated on BM1688, CV186X, and RK3576 plus a scoped macOS Docker Preview. The linked report records measured workload boundaries, validated VLM performance, and controlled 72-hour fixed-profile results. Production sizing still requires validation with your own models, streams, accuracy requirements, and deployment conditions.

</details>

### License

CosmoEdge is licensed under the [Apache License 2.0](LICENSE). Copyright 2026 CosmoEdge Contributors.

---

<div align="center">

An open-source project by Cosmo Wander AI and the CosmoEdge contributors.

Turn video AI models into deployable edge applications.

📦 This repository is mirrored read-only to [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) for mainland China access. See [MIRRORING.md](MIRRORING.md).

</div>

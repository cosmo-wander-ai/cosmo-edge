# Changelog

All notable public changes to CosmoEdge will be documented in this file.

This project follows a release-note style inspired by Keep a Changelog.

## [Unreleased]

These changes are on `main` after v1.1.0 and are not included in the published v1.1.0
binaries. Validation remains specific to the recorded candidate, platform, and workload;
the v1.1 benchmark results do not qualify later builds or newly added features.

### Added

- ONVIF camera discovery, manual registration, authentication, and stream selection, plus
  GB28181 camera registration, catalog discovery, and channel playback with independently
  configurable TCP/UDP signaling and video transport. Physical-camera compatibility and
  target-device acceptance remain deployment-specific.
  ([#181](https://github.com/cosmo-wander-ai/cosmo-edge/pull/181))
- Management v1 APIs for versioned model, scene, channel, schedule, and task delivery, with
  resumable uploads, content deduplication, operation recovery, explicit task activation,
  and status readback. See the [Management API guide](docs/en/reference/management.md).
  ([#182](https://github.com/cosmo-wander-ai/cosmo-edge/pull/182))
- Native BM1688 Laya-V integration through the model repository and local VLM service, with
  alarm review, image/video ROI questions, multiple questions per ROI, persisted results,
  and model-name preview overlays. The same integration adds YOLO26 raw-output and OBB
  decoding, geometry propagation, model templates, and conversion-toolchain admission.
  Recorded device checks cover a fixed candidate and short workloads; they do not establish
  business accuracy, long-run capacity, real-RTSP resilience, or standard OTA acceptance.
  ([#219](https://github.com/cosmo-wander-ai/cosmo-edge/pull/219))
- Video-sample event measurement with `cosmo-accuracy`, including reproducible suites,
  real-device trial orchestration, reports, and comparisons. This measures sample/event
  outcomes rather than frame-level Precision, Recall, or mAP.
  ([#141](https://github.com/cosmo-wander-ai/cosmo-edge/pull/141))
- Optional Sophon/RKLLM VLM evaluation tools with frozen input manifests, raw first outputs,
  failure-preserving scoring, paired comparisons, and timing/memory reports. Independent
  evaluation CLIs require the explicit `BUILD_VLM_EVAL` build option, which defaults to off.
  ([#174](https://github.com/cosmo-wander-ai/cosmo-edge/pull/174))
- Protected RK3576 preset-model loading through Model Guard, including multi-file RKNN
  packages, caller-selected certificate storage, versioned SDK admission, and package
  integrity checks. This extension does not change the v1.1.0 package scope or qualify
  protected models on RV1126B.
  ([#151](https://github.com/cosmo-wander-ai/cosmo-edge/pull/151))

### Changed

- Expanded RKNN device operations with reboot, factory reset, hardware watchdog support in
  production builds, and network configuration through NetworkManager.
- Clarified scene/channel ownership of visible task parameters and synchronized saved
  parameters before task start and after restart; simplified flow-editor state handling.
- Added documentation entry points for the independently maintained
  [CosmoEdge training-assistance Skill](https://github.com/cosmo-wander-ai/cosmoedge-training-skill)
  and [CosmoEdge Connect MCP project](https://github.com/cosmo-wander-ai/cosmoedge-connect).
  Their versions, changelogs, installation, and validation scope remain separate from the
  engine release; Connect is an Alpha source preview. See [Companion Projects](docs/en/guide/companion-projects.md).

### Fixed

- Corrected Sophon RGB normalization and YOLO NMS overlap calculations for center-format
  boxes, and enabled Guard-backed Qwen vision loading and host-backed image inputs.
- Preserved CPU fallback after failed Rockchip RGA operations, concealed recoverable
  decoder errors, and improved GB28181 media cleanup and recovery.
- Corrected recording-buffer reuse and MP4/H.265 sample handling, CPU resize borders,
  and macOS Docker runtime/build compatibility.
- Bounded system-log retention, reclaimed log space at boot, and integrated retention and
  model-storage handling into package upgrades and recovery.
- Isolated alarm/audit writes from shared SQLite reader snapshots, escaped quotes in alarm
  CSV exports, and streamed model exports without buffering complete archives in memory.
- Validated tensor inputs before allocation, tightened network/MQTT configuration checks,
  and repaired task, graph, listener, and memory-pool lifecycle handling.

## [1.1.0] - 2026-08-24

CosmoEdge 1.1 adds CV186X, RK3576, and RV1126B release-platform support, expands the BMRT/RKNN
media and inference paths, and publishes multi-platform short-run capacity, validated VLM performance, and
controlled 72-hour fixed-profile evidence. Apple Silicon macOS receives a scoped Docker Preview.

### Added

- CV186X benchmark bindings for the open Sophon YOLOV8n detector and helmet classifier, including
  byte-identical device/repository hashes, runtime contracts, and a dedicated CV186X Open-package
  resource set.
- Rockchip RK3576 integration with an RKNN inference backend, native MPP/RGA media pipeline,
  cross-compilation toolchain, accelerator telemetry, integration assets, and
  qualification fixtures.
- Rockchip RV1126B release-platform support with target-specific RKNN artifacts, cross-build and
  board-operation paths, MPP/RGA media integration, and measured short-run workload evidence.
- Apple Silicon macOS Docker Preview for an isolated `linux/amd64` single-local-video workflow.
- Platform-neutral host/backend and media-factory contracts, derived model-artifact identities,
  accelerator and preview-pipeline metrics, and ScenarioBench preview validation for additional
  accelerator families.
- Chip-agnostic Sophon model-directory discovery and explicit `chip_type` validation, preparing a
  shared model metadata and import path for BM1688 and CV186X while keeping compiled artifacts
  target-specific.
- Model Guard 2.3 support for loading CEMC-protected commercial preset models through the CMG
  runtime, device authorization, Open/Protected Sophon package profiles, protected-resource
  checks, and SOURCE deployment. Open packages continue to support plaintext user models without
  device authorization.
- Persistent, resumable staged uploads with idempotent client requests, restart recovery,
  cancellation, and real-time disk/memory admission. Large models, images, videos, and files now
  stream instead of depending on fixed total-size limits; exports support single HTTP Range
  requests (`206`/`416`) and deterministic temporary-file cleanup.
- Detection target details in HTTP event payloads, including persistence/retry propagation and
  target rendering in the HTTP push-test service.
- Platform-neutral offline accuracy validation and CI gates for ScenarioBench and accuracy tools.
- Agent-assisted secondary-development tooling for environment admission, chip compatibility,
  measured model-conversion evidence, and evidence lifecycle governance.
- AVI, DAV, and MKV local-video inputs, plus the restored license-plate OCR workflow.

### Changed

- Improved cross-platform CV detector capacity and runtime efficiency through per-task FPS-aware
  `AiDetector` placement, platform-neutral performance telemetry, more efficient media and buffer
  lifecycles, and backend-specific Sophon and RKNN data-path optimizations.
- Optimized the RK3576 detector path with RGA/MPP-backed preprocessing, native quantized tensor
  handling, persistent RKNN input memory, direct YOLOv8 output decoding, decoder reuse, and
  qualified NPU core scheduling.
- Improved Sophon high-rate and long-running workloads through lower detector reuse defaults,
  glibc allocator tuning, bounded repeated-stream decoder warm-up handling, and explicit BMRuntime
  ownership.
- Reduced preview startup and overlay-switch latency, strengthened publisher recovery and preview
  lifecycle handling, serialized concurrent OSD sessions, and exposed preview pipeline metrics.
- Batched arbitrary numbers of accepted same-frame alarm targets into one event while retaining
  per-target filtering and metadata; reset tracking and suppression state when tasks restart.
- Expanded area-alarm rule interaction, region-rule localization, runtime translations, and task
  category refresh behavior in the web console.
- Unified Sophon package upgrades on the legacy-compatible permanent-MD5 lifecycle, including
  legacy installer invocation, safe internal symlinks, upgrade recovery, and Open/Protected model
  lifecycle checks. Application archives themselves remain unsigned.
- Added a fail-closed Rockchip data-root bridge for prerelease installations: durable state is
  transactionally copied from `/data/cwaiuserdata` to `/userdata/cwaiuserdata`, the source remains
  available for recovery, transient upload/runtime data is excluded, and ambiguous dual-root
  persistent state is rejected before service shutdown. A rejected installation resumes the
  previously active application instead of leaving the service in an upgrade retry loop.
- Simplified linkage runtime task handling and made task saves atomic under resource pressure.
- Hardened change validation, sharded Sophon tests, and aligned ScenarioBench capacity, VLM
  throughput, preview-load, report, and cleanup behavior with the current staging protocol.
- Validated VLM support on BM1688, CV186X, and RK3576 using one controlled-input protocol with
  per-route readiness, task-local completion counters, and an executed 80% FPS gate. Exact
  short-run boundaries are 6, 6, and 4 channels; each platform's fixed candidate package also passes model
  load, task creation, valid inference, event/alarm output, and task recovery after service restart.

### Fixed

- RTSP URLs whose passwords contain `@`, and algorithm-file names with leading-zero prefixes.
- HTTP, WebSocket, multicast-discovery, periodic-timer, shared-database, camera-task, task-binding,
  memory-pool, and thread-pool lifetime/concurrency defects.
- Live-preview playback, padded Annex-B packets, publisher recovery, unbound algorithm previews,
  small startup keyframes, and multi-second overlay-preview black screens.
- Sophon crop validation and odd-dimension image decoding, including even-dimension frame
  alignment for face import.
- Upload and extraction reserve edge cases, image-analysis JSON capacity, media MIME handling,
  upgrade recovery, and propagation of HTTP client transfer failures.
- Area-alarm interval handling for same-frame tracked and untracked targets, including zero track
  IDs and restart-safe suppression state.
- DINO model/task contract validation, HandFrame exception log storms, RKNN classifier and FP16
  input compatibility, and shared-detector throughput accounting.
- Windows Docker checkout/web-port handling, generated mp4v2 timestamps, and cross-backend CPU
  allocator linkage.

### Security

- Blocked `pictureUrl` path traversal in image-library APIs and retained bounded request parsing
  while routing product-sized content through resource-aware streaming paths.
- Externalized manager signing credentials and tightened protected-resource/package admission and
  device-authorization handling.
- Prevented encrypted preset-model export and deletion, with an explicit factory-restore path.
- Hardened upgrade-package verification while permitting safe package-internal symlinks.

### Docs

- Reworked the English and Simplified Chinese README files around the v1.1 platform matrix,
  Open/Protected boundary, published validation baselines, and task-oriented quick starts.
- Added official website links, certified-device purchase links, x86 first-run verification, an
  Ultralytics YOLO deployment guide, and reusable community-case documentation.
- Expanded and restored the bilingual system guide, region-alarm tutorials, runtime localization,
  and model/resource references.
- Added the bilingual CosmoEdge 1.1 multi-platform ScenarioBench report, sanitized single-detector,
  dual-detector, and validated VLM performance reports, environment/model identities, reproduction
  descriptors, release manifest, and file checksums.
- Added the agent-assisted development entry, environment and model-conversion guidance, and a
  contributor pre-commit hook guide.
- Added the bilingual v1.0.0-to-v1.1.0 upgrade and recovery guide, maintainer-ready per-platform
  v1.1.0 release-note drafts, and an explicit GitHub Discussions English-support boundary.

## [1.0.0] - 2026-07-03

First stable public release: a security and CI hardening pass plus feature, refactor, and documentation work over the `v0.1.0` baseline.

### Added

- `tools/scenario-bench` capacity/benchmark tool with pluggable CV/VLM task strategies, multi-task workloads, capacity reporting, VLM inference metrics, initial VLM readiness support, and a workload design doc.
- Sophon BM1688 safety-helmet detection algorithm and models.
- x86 preset algorithm pack with ONNX models.
- Error-message i18n infrastructure for the web console.
- Gitee mirror workflow, dual-repo README, and `MIRRORING.md`.
- Default-password forced-change flow.
- Nightly Sophon build/test workflow and an x86 `cosmo-tests` CI job.

### Changed

- Hardened all GitHub Actions workflows (job timeouts, least-privilege permissions, SHA-pinned actions); pinned the Rust toolchain and frontend Node 22; rewrote `mirror-to-gitee` to direct git push with an ssh-keyscan retry.
- Renamed `PlatformConstants.h` to `NnBackendConstants.h`; aligned `DinoDetector` with `CODING_STYLE.md`.
- Build: removed redundant wrapper scripts, switched the Sophon compose flow to `run --rm`, moved Windows Sophon cross-compilation onto a Docker named volume, corrected `CMAKE_BUILD_TYPE` case, fixed vite `node_modules/.bin` permissions, switched x86 to a pre-built builder image, and excluded the test video from packages.
- Disabled the stdout log callback in production to avoid syslog flooding.
- Localized traffic-statistics dates, the onboarding channel-type hint, and task-run process name/status/algorithm fields; rendered alarm, face, and body-match similarity with consistent decimals.
- Added autocomplete and hidden username fields on auth inputs for accessibility.
- Clarified the public release roadmap in the English and Simplified Chinese README files.

### Fixed

- Hardened `util/Exec` against shell injection via an argv-based `execvp` API, migrated call sites, and added regression tests.
- Guarded `infer` `Ai*Interface` destructors against null `reuse_obj_`.
- Checked `bm_dev_request` return values in the `mem::DeviceContext` constructor.
- Guarded `nn::HungarianAlgorithm::Solve` against an empty cost matrix.
- Removed a dead null check in the `service::EventNotifier` WebSocket close callback and a dead mutex in `AlarmRecordServiceImpl`.
- Reset `SIGPIPE` to `SIG_DFL` in the child process before `execvp`.
- Fixed the auth handler test by mocking `IsDefaultPassword`.

### Security

- Release hardening pass: shell-injection guards, null-safety and lifecycle fixes across `infer`/`mem`/`nn`/`service`, default-password enforcement, and child-process signal hygiene.
- Updated `SECURITY.md` with supported versions, response SLA, and hardening notes, and unified the vulnerability contact to `hello@cosmowander.ai`.

### Docs

- `SECURITY.md` supported-versions table, response SLA, and hardening notes.
- Interface HTML docs synced with the staticfile i18n versions.
- Sophon build/test CI badge and README badge layout refresh; test video added.

## [0.1.0] - 2026-06-29

Initial public open-source baseline.

### Added

- C++17 edge inference engine.
- Visual pipeline orchestrator.
- Web management console.
- x86 developer mode for Linux and Windows.
- Sophon BM1688 release packaging.
- VLM and GroundingDINO integration.
- 26 internally validated pipeline scenarios.
- Open-source README in English and Simplified Chinese.
- VitePress documentation site.
- Tutorials for quick start, scenario configuration, VLM / DINO, pipeline orchestration, and model porting.
- Build, deployment, configuration, troubleshooting, architecture, API, model/resource, frontend, and backend documentation.
- Issue templates and pull request template.
- Security policy.
- Apache License 2.0 `LICENSE`.
- Initial `NOTICE`.

### Changed

- Public quick-start command aligned with the current x86 Docker flow:
  `docker compose -f docker-compose.x86.yml up -d --build`.
- Sophon package build documentation aligned with current helper scripts:
  `scripts/build_sophon_package.sh` and `scripts/build_sophon_package.ps1`.
- README video links moved to the public `v1.0-videos` asset tag.
- Sample camera credentials and device serial values scrubbed from examples.
- Sophon build environment Dockerfile made self-contained.

[Unreleased]: https://github.com/cosmo-wander-ai/cosmo-edge/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.1.0
[1.0.0]: https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.0.0
[0.1.0]: https://github.com/cosmo-wander-ai/cosmo-edge/releases

<!-- Gitee mirrors -->
[Unreleased (Gitee)]: https://gitee.com/cosmo-wander-ai/cosmo-edge/compare/v1.1.0...master
[1.1.0 (Gitee)]: https://gitee.com/cosmo-wander-ai/cosmo-edge/releases
[1.0.0 (Gitee)]: https://gitee.com/cosmo-wander-ai/cosmo-edge/releases
[0.1.0 (Gitee)]: https://gitee.com/cosmo-wander-ai/cosmo-edge/releases

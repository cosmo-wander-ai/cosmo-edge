---
title: RK3576 / RKNN Integration
description: Stable-release build, runtime, model, and validation boundaries for Rockchip RK3576.
prev:
  text: Build Guide
  link: /en/guide/build
next:
  text: macOS Docker Preview
  link: /en/guide/macos-docker-preview
---

# RK3576 / RKNN Integration Guide

## Scope

The RK3576 integration adds a production-oriented CV backend without changing
the behavior of the CPU, CUDA, or Sophon backends:

- RKNN Runtime 2.3.2 executes static-batch detector and classifier models.
- RKLLM Runtime 1.3.0 works with an RKNN vision encoder to execute Qwen3.5
  multimodal models.
- Rockchip MPP performs H.264/H.265 decode and encode.
- The decoder uses delayed Copy-out: frames are sampled or discarded before a
  host I420 copy is requested.
- RGA performs the Rockchip frame-processing operations required by preview and
  OSD paths.
- Full DMA-BUF zero-copy is outside the supported release boundary.

The recommended deployment starting point is the person-detection profile that completed a
12-hour run at four channels and 5 FPS per channel. The latest short staircases reached 16
channels at 5 FPS for the single person-detection workload and 8 channels at 5 FPS per task
for a concurrent mixed workload. Each mixed-workload channel runs person detection alongside
no-safety-helmet analysis, whose pipeline contains detection followed by classification. These
are measured boundaries for the stated models and gates, not replacements for a recommended
profile. See [ScenarioBench v1.1](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/report.html).

## Repository and Evidence Boundary

The repository owns product code, build definitions, unit tests, reproducible
model tooling, deployable RKNN resources, and two reusable acceptance scenarios:

- `tools/scenario-bench/scenarios/rk3576-no-helmet-customer-journey`
- `tools/scenario-bench/scenarios/rk3576-no-helmet-longrun-4x5fps`

Raw device logs, metrics streams, screenshots, exported events, and generated
HTML/XML/JSON reports are external validation artifacts and must not be added to
the source tree. A release evidence manifest binds results to the source commit
and tree, final package SHA-256, device/firmware/runtime versions, model and
dataset hashes, thresholds, cleanup status, and measured values.

Device addresses, account data, local backup paths, and reusable credentials do
not belong in source-controlled configuration or evidence.

## Frozen Toolchain Identities

The machine-readable toolchain and model-input lock is
`config/rknn/toolchain-lock.json`. The supported integration is based on:

- RKNN-Toolkit2 2.3.2
- RKNN Model Zoo 2.3.2
- Ubuntu 22.04 x86_64 conversion host with Python 3.10
- RK3576 Ubuntu 22.04 aarch64 target with kernel 6.1.118 and RKNPU driver 0.9.8

Changing a locked SDK, runtime, input model, or preprocessing contract requires
new conversion and device evidence.

## Runtime Safety Boundary

Keep the board's system RKNN runtime as the rollback baseline. Package RKNN
Runtime 2.3.2 beside CosmoEdge and select it with executable RPATH or a
task-local `LD_LIBRARY_PATH`; do not overwrite `/usr/lib/librknnrt.so`.
Production inference uses the native C API and does not depend on `rknn_server`.

## Model and Preprocessing Contract

The first supported models are:

1. Helmet classification: `1x3x224x224`, ONNX opset 19.
2. YOLOv8 detection: `1x3x640x640`, converted to ONNX opset 19 / IR 9.

Qwen3.5 multimodal deployment is a separate model contract. An importable
directory contains at least:

- `model.rkllm`: a language model targeting RK3576;
- `vision.rknn`: a vision encoder whose image-token count and embedding width
  match the language model;
- `tokenizer.json`: the tokenizer from the exact conversion source model;
- `config.json`: `model_type` is `qwen3_5` and `runtime_backend` is `rkllm`.

Record SHA-256 for the four files as one set. The presence of `librkllmrt.so`,
a text-only model load, or an isolated `vision.rknn` run does not prove
multimodal capability.

CosmoEdge owns resize, channel order, and normalization. Conversion must not
bake in a second mean/std transform. CosmoEdge supplies float32 NCHW tensors;
the RKNN boundary performs one explicit NCHW-to-NHWC copy because Runtime 2.3.2
rejects NCHW on this input-conversion path. Outputs are requested as float32 so
the existing postprocessors remain authoritative.

The production YOLO model exposes three box/class head pairs. The
`yolov8_dfl_v1` host adapter applies DFL and sigmoid, then reconstructs the
logical `[1,84,8400]` contract. A single quantized output is not supported
because its shared scale collapses confidence precision.

## Reproducible Conversion

Prepare the verified offline bundle at an operator-selected path:

```bash
./scripts/rknn/prepare_offline_env.sh "$RKNN_OFFLINE_BUNDLE"
```

The locked YOLO conversion sequence is:

```bash
python tools/rknn/convert_onnx_opset.py \
  --input model-opset22.onnx --output yolov8-opset19-ir9.onnx \
  --opset 19 --ir-version 9

python tools/rknn/extract_yolov8_heads.py \
  --input yolov8-opset19-ir9.onnx --output yolov8-heads.onnx

python tools/rknn/prepare_validation_data.py \
  --spec config/rknn/models/yolov8.json --video "$VALIDATION_VIDEO" \
  --output-dir yolov8-calibration --samples 32

python tools/rknn/convert_model.py \
  --spec config/rknn/models/yolov8.json \
  --platform-profile config/rknn/platforms/rk3576.json \
  --model yolov8-heads.onnx \
  --output yolov8-heads-int8.rknn --quantize \
  --dataset yolov8-calibration/dataset.txt
```

Calibration and numerical-parity samples are unlabeled. They do not replace a
labeled precision/recall/F1 acceptance set.

## Build and Deployment

The public build uses the digest-pinned shared Rockchip image. It maintains one
aarch64 toolchain and RKNN Runtime, then selects the isolated RK3576 MPP/RGA
profile. RKLLM Runtime v1.3.0 comes from a pinned official Rockchip commit.
The base resource directory supplies common actions, layouts, and fonts; the
RKNN resource directory supplies the RK3576 algorithms and models.

```bash
./scripts/docker-compose.sh -f docker-compose.rockchip.yml pull cosmo-rockchip-package
COSMO_TARGET_CHIP=rk3576 ./scripts/docker-compose.sh \
  -f docker-compose.rockchip.yml run --rm cosmo-rockchip-package
sha256sum build_output/rk3576/cosmo-*.tar.gz
```

The shared image and pinned official RKLLM files are public and require no
`docker login`; the helper selects Compose V2/V1. This command builds a
Release package with the Rockchip media backend and leaves the aarch64 test
binary at `build_rknn/cosmo-tests`; it does not enable `COSMO_DEV_MODE`.

RKLLM is mandatory for a formal RK3576 package. A missing header,
`librkllmrt.so`, or license fails configuration instead of silently producing a
package without Qwen3.5 support.

The package distributes RKLLM Runtime, but the Open package does not distribute
Qwen3.5 model files. The deployer must provide a licensed conversion artifact
that matches RK3576 and Runtime 1.3.0 and import it as the four-file set above.

The shared entry removes the previous `build_rknn` directory before building so
a partial cross-compilation cache cannot be reused as release evidence. It uses
host networking for build-time dependency resolution; the one-shot build
service does not publish or listen on application ports.

Board networking on RK3576 is managed by system NetworkManager, not by the
Sophon netplan path in CosmoEdge. Clearing `/userdata/cwaiuserdata` recreates the
default JSON but does not change an existing NetworkManager connection to
`192.168.100.1`; use `ip -4 addr`/`nmcli` as the deployment source of truth.

Keep mutable and packaged roots separate at runtime:

```bash
export COSMO_DATA_DIR=/userdata/cwaiuserdata
export COSMO_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data
export LD_LIBRARY_PATH="$COSMO_APP_DATA_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

`COSMO_DATA_DIR` contains configuration, databases, uploads, and events.
`COSMO_APP_DATA_DIR` contains packaged resources, models, libraries, and
binaries. Use the packaged launcher so transitive shared-library dependencies
resolve from the artifact being validated.

## Reusable Acceptance Scenarios

The customer-journey scenario runs one channel at 5 FPS for a bounded window.
Acceptance includes login, model/task/channel visibility, real raw and
algorithm HTTP-FLV playback, OSD difference, events, reconnect, stop/start
recovery, and cleanup.

The long-run scenario holds four channels at 5 FPS for 12 hours. Run it with
algorithm-preview clients enabled and audit it with `--gate-hours 12`. The
runner stops when the configured disk fuse is reached. Use `--password-stdin`
so credentials do not enter process arguments.

Preview validation requires real `ffmpeg` and `ffprobe` executables. The tool
preflights them before mutating device configuration.

## Qwen3.5 Multimodal Device Acceptance

Formal acceptance supplies a fixed test image on a real RK3576 device and
requires non-empty text related to that image. A text-only prompt proves only
the RKLLM language path and does not replace this gate. Use the CosmoEdge
**Image Analysis** flow, or cross-compile Rockchip's official
[`multimodal_model_demo`](https://github.com/airockchip/rknn-llm/tree/878f9361fd3afa7e167b7079918918f78d2c1c2a/examples/multimodal_model_demo)
with the same aarch64 toolchain as the build image and run it on the device:

```bash
MODEL_DIR=/userdata/cwaiuserdata/resource/models/<qwen3.5-model-directory>
export LD_LIBRARY_PATH=./lib
./demo smoke.jpg "$MODEL_DIR/vision.rknn" "$MODEL_DIR/model.rkllm" \
  64 4096 2 rk3576 \
  '<|vision_start|>' '<|vision_end|>' '<|image_pad|>'
```

Record the package and four model-file SHA-256 values, RKLLM/Toolkit/driver
versions, target platform, quantization type, vision input/output shapes, test
image hash, returned text, and exit status. The gate passes only when both
models load, vision encoding runs successfully, and non-empty image-related
text is returned. After the test, restore `cosmo.service`, confirm it is
`active`, verify a successful management-page response, and recheck the
device's actual IP address.

## Validated Release Boundary

- Four channels at 5 FPS completed the 12-hour gate with zero media
  failure/fallback deltas and stable memory-pool accounting; the corresponding
  CPU measurements remain in that historical evidence record.
- Real raw and algorithm playback, hardware decode/encode, OSD, reconnect, and
  task restart recovery passed on the tested build.
- Delayed Copy-out discarded frames before host copies and is the selected
  optimization for this release.
- The v1.1 public report records a 16-channel, 5 FPS person-detection staircase and an 8-channel,
  5 FPS-per-task concurrent mixed-workload staircase. Each mixed-workload channel combines person
  detection with two-stage no-safety-helmet analysis. Both are short-run measured boundaries and
  have not been promoted to official recommended profiles.
- RK3576 NPU telemetry uses the vendor busy-time counter from
  `/sys/kernel/debug/rknpu/load`, reports the busiest core on the health card,
  and retains every core in the accelerator payload. The startup script exposes
  only this read-only file at `/run/cosmo-edge/metrics/rknpu-load`; the devfreq
  governor signal is never treated as NPU load.
- RK3576 NPU and media allocations share system DDR. Accelerator telemetry
  marks this as `memoryDomain=shared-system`; the dashboard emits one system
  memory capacity instead of adding the same pool again as dedicated VRAM.

These observations are artifact-bound and must be rerun after source, model,
runtime, or package changes. The accepted release record preserves the immutable
package SHA-256, business-accuracy result, credential-safe logs,
event-retention result, cleanup status, and measured values. Raw validation
artifacts remain outside the source tree.

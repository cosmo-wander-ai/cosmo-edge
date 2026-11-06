---
title: CV186X Quick Start
description: Install a matching CosmoEdge release on a prepared CV186X Linux device and produce the first detection event.
prev:
  text: Build Guide
  link: /en/guide/build
next:
  text: RK3576 / RKNN Integration
  link: /en/guide/rk3576-rknn-development
---

# CV186X Quick Start

This guide targets a CV186X Linux device with Sophon runtime dependencies and networking
already prepared. It uses BMRT and CV186X-compatible `.nn` models. The two Sophon models in
the historical v1.1 public benchmark are byte-identical to the BM1688 benchmark artifacts;
other models still require contract-by-contract validation. Historical results do not qualify
a newly built package on a device.

Before installation, verify hardware identity; an IP address or old model directory name is not platform evidence:

```bash
tr -d '\0' </proc/device-tree/model; echo
```

The device-tree string identifies a SoC, accelerator, or firmware stack; it does
not always equal the commercial product-platform name. A CV186X product using the
Sophon compute stack may legitimately report `BM1688`. Do not reject CV186X solely
because of that string. Qualification must bind all three evidence layers:

1. Vendor, BOM, or controlled inventory evidence maps the complete device to the
   CV186X product platform.
2. The package `TARGET_CHIP` is `cv186x`, and the package SHA-256 matches the
   release record.
3. The model loaded by the device matches the CV186X resource record by SHA-256
   and completes one real BMRT or product-task inference on that device.

Stop qualification when the device-tree value is empty and no independent platform
evidence exists, or when device-tree, inventory, package, and runtime evidence
conflict. A running service alone does not qualify CV186X inference.

## 1. Obtain and verify the package

On the build host, select Linux Bash or Windows PowerShell through the
[CV186X build entry](./build.md#cv186x). Default Open output is under
`build_output/public-runtime/cv186x/`, with one package, `TARGET_CHIP`, and `SHA256SUMS`.

Alternatively, obtain the desired release's explicitly labeled CV186X package and checksum
information from [GitHub Release](https://github.com/cosmo-wander-ai/cosmo-edge/releases).
Follow [artifact verification](./build.md#hand-a-sophon-build-to-the-deployment-workflow),
confirm `cv186x` and the SHA-256 against the manifest or release record, and record the actual
full package filename. Source and release-package versions may differ; do not select a
package using a fixed old-version wildcard.

## 2. Install and start

Follow the [Deployment Guide: SSH Installation Path](./deployment.md#ssh-installation-path),
selecting `build_output/public-runtime/cv186x/`. That section separates host-side transfer
from device installation and maintains the shared backup, verification, installation, reboot,
and recovery steps. Use the actual package filename verified above.

After reboot, confirm `cosmo.service` is active and the management page reports the target
package's version, then complete [post-upgrade acceptance](./deployment.md#installation-and-post-upgrade-acceptance).
An existing CosmoEdge device can also upload the same package through **System Management →
System Maintenance → Software Upgrade**.

## 3. Create the first event with the bundled open models

For a `cv186x` build, the build script automatically selects the matching resource
directory. Users do not provide that path. The default `include` package contains the two models
used by the historical v1.1 public CV186X benchmark:

- `YOLOV8n V1.0.0`: person detector, `1x3x640x640`, 7,023,600-byte model file;
- `helmet V1.0.0`: safety-helmet classifier, `1x3x224x224`, 6,001,416-byte model file.

Their repository directories, input/output contracts, and SHA-256 identities are recorded in the
[ScenarioBench v1.1 model identity record](/benchmarks/scenario-bench/v1.1/models/cv186x.json).
The model subdirectories retain the legacy `prod_BM1688_` compatibility prefix because the files
were copied from the CV186X benchmark device without modification. Neither that directory prefix
nor a `BM1688` device-tree string determines the complete product platform by itself. CV186X
qualification binds controlled platform mapping, an exact SHA-256 match between device-loaded
files and the CV186X resource set, and successful device inference. It does not imply that other
BM1688 artifacts are interchangeable.

1. Sign in to the Web console, open **Model Repository**, and confirm that both models are present.
2. Add one test stream under **Video Input**, then create and bind a task with the person detector.
   Add the helmet classifier to the orchestration when validating the complete helmet workflow.
3. Open algorithm preview, confirm OSD and an event, then verify that task state and service logs
   show no continuing error. Record the model SHA-256, literal device-tree string, and inference
   result together in the qualification evidence.

When importing a custom model, its directory must contain a matching `config.json` and model file.
Input dimensions, quantization, preprocessing, postprocessing, and output tensors must match the
configuration. See the [Model Porting Guide](/en/tutorials/05-model-porting/model-porting) for the
full contract.

## Upgrade, recovery, and evidence boundary

- Installation and Web upgrade use the same `cosmo-V<version>-<md5>.tar.gz` lifecycle. Keep power
  connected and verify both service state and software version after recovery.
- If the service does not recover, inspect `systemctl status cosmo.service` and service logs before
  attempting another upload.
- See the [Deployment Guide](/en/guide/deployment) for directories, ports, persistence, failure
  recovery, and rollback boundaries.
- Historical public workload results are in [ScenarioBench v1.1](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/report.html).
  A highest short-run point is not an official recommended profile; size production deployments
  with the actual models, streams, and accuracy requirements.

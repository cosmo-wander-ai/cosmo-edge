---
title: Models and Resources
description: Current resource directories, model templates, algorithm templates, and release notes.
prev:
  text: HTTP Webhook Reference
  link: /en/reference/webhook
next:
  text: Frontend Development
  link: /en/development/frontend
---

# Models and Resources

This document describes the model and resource organization that can be confirmed in the current repository.

## Resource Directories

| Directory | Purpose |
| --- | --- |
| `data/resource/aiboxresource_bm1688` | Resources for the Sophon BM1688 release package. |
| `data/resource/aiboxresource_cv186x` | Resources for the Sophon CV186X release package. |
| `data/resource/aiboxresource_x86` | Resources for the x86 Docker / CPU backend. |
| `data/resource/aiboxresource_rknn` | Rockchip target resources and model overlays, combined with base templates |

The resource directory is selected through `RESOURCE_DIR` at build time.

RKNN builds reuse base templates from `aiboxresource_x86` and apply target-specific resources. RK3576 uses `aiboxresource_rknn`; RV1126B generates `output/platform-artifacts/rv1126b/resource-overlay` from its platform profile and artifact manifest. Base templates, target overlays and the model directory determine the packaged resources. Refer to the package manifest for its actual contents.

## Model Templates

A template defines configuration and parsing options. It does not mean the weights are included or the model is supported on every platform; availability depends on the backend, target chip, runtime and installed resources.

Model templates are located at:

```text
data/resource/*/model_template
```

The templates currently visible include:

- YOLO detection templates
- YOLO classification templates
- DINO
- SAM2
- feature
- keypoints
- Qwen3 / Qwen3VL
- OCR

## License-Plate OCR Package

OCR recognizes plates already located by landmarks; it does not perform full-image text detection. Install the model package through device resources. This repository provides only the `ocr.json` template and never includes weights or private dictionaries.

Each external OCR package must contain:

- `config.json` with `"model_type": "ocr"`;
- the current-platform model file (`.nn` for Sophon, `.onnx` for x86);
- the `.txt` character table named by its configuration.

`models[0].params` must explicitly bind the table and CTC mapping: `character_table_file` must name a root-level `.txt` file, `ctc_blank_index` identifies the blank class, `ctc_prepend_tokens` and `ctc_append_tokens` define additional leading and trailing model classes, and `ctc_class_count` must equal the model output's final dimension. The table plus leading and trailing tokens must match CTC indices exactly; a mismatch fails OCR initialization or inference instead of silently dropping characters. The Add Model wizard requires both the OCR model and its character table and generates these fields.

Legacy license-plate model `2000007` uses 6,625 classes with a 6,624-line table: entry zero is blank and the configuration appends one ASCII space for the final class. Decoding removes blank and consecutive duplicate classes.

A PP-OCR Chinese recognition model may use the standard 6,623-entry table without blank. For a 6,625-class output, the system explicitly prepends blank and appends an ASCII space.

Connect the scene nodes as “plate detection/association → four-point plate landmark → text recognition → event report”. OCR accepts only a strict four-point plate quadrilateral in top-left, bottom-left, bottom-right, top-right order; empty recognition results do not produce an alarm. In the event-report node, select the Vehicle Property alarm attribute to expose `plateSrc`, `plate`, and the rectified plate crop in the event.

## Layout and Components

Resource layout files include:

```text
data/resource/*/layout/modelComponents.json
data/resource/*/layout/actions.json
data/resource/*/layout/linkageStorages.json
```

These files affect the frontend configuration items, model component parameters, action nodes, and linkage strategies.

## Algorithm Templates

Algorithm templates are located at:

```text
data/resource/*/algorithm_template
```

Templates for vision-language models, DINO, YOLO, and related models are visible here. For an official release, it is recommended to generate a "template catalog table" from the current resource directory to avoid a hand-written list going stale.

## x86 and Sophon Differences

x86 path:

```text
data/resource/aiboxresource_x86
```

Sophon path:

```text
data/resource/aiboxresource_bm1688
data/resource/aiboxresource_cv186x
```

The code shows the differences between handling x86 ONNX files and Sophon model packages. The full model porting workflow should be re-validated against the currently releasable model packages.

## Resource Licensing Notes

The resource tree contains model, algorithm, and layout templates as well as
selected public example weights and chip-converted artifacts. The repository's
Apache-2.0 source-code license does not automatically cover model files; use
the bundle manifest and directory-level license as the authority:

- Prebuilt components in the `prebuild/` directory require separate distribution-license review.
- Ultralytics YOLOv8 chip artifacts under
  `data/resource/aiboxresource_rknn/model-artifacts/` are AGPL-3.0 community
  examples, not commercial model deliverables.
- Commercial or proprietary models use independent source, weight, training,
  and license records and do not reuse the community example bundle identity.
- Whether the model encryption feature is included in the current build depends on the CMake option `COSMO_MODEL_GUARD`.
- If you introduce models from a third-party model ecosystem, follow the license requirements of the corresponding models.

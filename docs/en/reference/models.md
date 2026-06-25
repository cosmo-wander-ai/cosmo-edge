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
| `data/resource/aiboxresource` | Resources for the Sophon release package. |
| `data/resource/aiboxresource_x86` | Resources for the x86 Docker / CPU backend. |

The resource directory is selected through `RESOURCE_DIR` at build time.

## Model Templates

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
data/resource/aiboxresource
```

The code shows the differences between handling x86 ONNX files and Sophon model packages. The full model porting workflow should be re-validated against the currently releasable model packages.

## Resource Licensing Notes

The model templates, algorithm templates, and layout configuration files in the resource directory describe the metadata of models and algorithms; they do not contain model weights. Some resource files are for example purposes only:

- Prebuilt components in the `prebuild/` directory require separate distribution-license review.
- Whether the model encryption feature is included in the current build depends on the CMake option `COSMO_MODEL_GUARD`.
- If you introduce models from a third-party model ecosystem, follow the license requirements of the corresponding models.

---
title: Models and Resources
description: Resource directories, model templates, layout files, algorithm templates, x86/Sophon differences, and release notes.
prev:
  text: HTTP Webhook Reference
  link: /en/reference/webhook
next:
  text: Frontend Development
  link: /en/development/frontend
---

# Models and Resources

CosmoEdge uses resource directories to describe models, labels, algorithms, layouts, and scenario-related configuration.

## Resource Directories

| Path | Purpose |
| --- | --- |
| `data/resource/aiboxresource` | Target-device resource set. |
| `data/resource/aiboxresource_x86` | x86-oriented resource set. |
| `prebuild/` | Prebuilt components that need license/distribution review. |

## Model Templates

Model templates describe model identity, type, labels, input/output expectations, and UI/runtime metadata. Public examples must not include proprietary model weights or private download URLs.

## Algorithm Templates

Algorithm templates connect model capabilities to scenario tasks and pipeline nodes. They may include default thresholds, category labels, and parameter schema.

## x86 and Sophon Differences

x86 resources are useful for development and CPU-oriented validation. Sophon resources target BM1688/BM runtime deployment.

## Resource and License Notes

- Confirm all model/resource licenses before distribution.
- Remove private weights and private download links from public resources.
- Replace real device/customer examples in templates.
- Document which resources are examples and which are production-ready.

---
title: 'Volume 5: Model Porting'
description: Convert, upload, validate, and integrate third-party models into CosmoEdge scenario tasks.
prev:
  text: 'Volume 4: Pipeline Orchestration'
  link: /en/tutorials/04-pipeline-orchestration/pipeline-orchestration
next: false
---

# Volume 5: Model Porting

This tutorial explains the model porting workflow from third-party model preparation to end-to-end scenario validation.

## Learning Path

1. Prepare a third-party model.
2. Prepare Docker/conversion environment.
3. Convert model format.
4. Upload model to the system.
5. Validate with images.
6. Integrate into a scenario task.
7. Validate with video.

## Prepare the Model

Prefer ONNX as the first exchange format when possible. Confirm input size, color format, normalization, labels, and output structure before conversion.

## Model Conversion

A typical Sophon conversion path converts ONNX to MLIR, then MLIR to `bmodel`. Exact commands depend on model type and SDK version.

Key checks:

- input shape matches training/export settings;
- pixel format matches preprocessing;
- output tensor names and label order are documented;
- converted model is tested before integration.

## Upload and Validate

Upload the converted model and metadata through the model repository UI or release-supported import path. Validate with still images before creating a live scenario task.

## Integrate into a Scenario Task

Create or update an algorithm template, then build a minimal pipeline that uses the new model. Start with the smallest working chain and add business rules later.

## End-to-End Video Validation

Create a video source, assign the scenario task, view live overlays, and check event records.

Success criteria:

- model loads successfully;
- detections match expected categories;
- false positives are manageable;
- events include expected task/channel/model information;
- screenshots and sample media are safe to publish.

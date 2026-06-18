---
title: 'Volume 4: Pipeline Orchestration'
description: Understand scenario task pipelines, node types, parameter flow, and how to create or modify scenario tasks.
prev:
  text: 'Volume 3: VLM / DINO Guide'
  link: /en/tutorials/03-vlm-guide/vlm-guide
next:
  text: 'Volume 5: Model Porting'
  link: /en/tutorials/05-model-porting/model-porting
---

# Volume 4: Pipeline Orchestration

Scenario tasks are built from pipeline nodes. This guide explains how to understand, modify, and create those pipelines.

## Learning Path

1. Understand common node types.
2. Read data flow between nodes.
3. Modify an existing task.
4. Create a minimal new task.
5. Bind the task to a channel and validate results.

## Common Node Types

Common node types include video decode, model inference, target filtering, tracking, region judgment, sensitivity calculation, visualization, and event reporting.

A good pipeline filters invalid targets early, applies business rules after detection/tracking, and outputs both visual overlays and structured events when needed.

## Modify an Existing Scenario Task

Start from a known task such as helmet detection. Add or adjust one node at a time, then validate the behavior in live preview and event records.

Recommended workflow:

1. Duplicate or backup the existing task configuration.
2. Add a filter or rule node.
3. Tune parameters.
4. Save and validate.
5. Roll back if the result is unstable.

## Create a New Scenario Task

A minimal detection-and-alarm pipeline often contains video decode, object detection, optional tracking, region judgment, sensitivity/debounce, event reporting, and optional visualization.

## Common Mistakes

- Missing input/output connections between nodes.
- Using a category label that does not exist in the model.
- Region rules drawn outside the actual camera view.
- Event reporting node missing or disabled.

## Next Step

Continue with [Volume 5: Model Porting](../05-model-porting/model-porting.md).

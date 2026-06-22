---
title: 'Volume 3: VLM / DINO Guide'
description: Prompt-driven visual judgment with VLM and open-vocabulary detection with DINO.
prev:
  text: 'Volume 2: Scenario Configuration'
  link: /en/tutorials/02-scenario-config/scenario-config
next:
  text: 'Volume 4: Pipeline Orchestration'
  link: /en/tutorials/04-pipeline-orchestration/pipeline-orchestration
---

# Volume 3: VLM / DINO Guide

This guide explains how to use large visual models to handle long-tail scenarios that are difficult to cover with fixed category detectors.

## Why This Chapter Matters

Traditional CV models work well when categories are fixed and enough training data is available. VLM and DINO features help when the requirement is described in natural language or when the target category changes frequently.

## Tool Selection

| Tool | Best For | Avoid When |
| --- | --- | --- |
| Small CV model | Stable categories, high throughput, deterministic behavior | Category changes frequently. |
| VLM | Visual state judgment, yes/no decisions, semantic review | Precise localization or high FPS is required. |
| DINO | Open-vocabulary object detection by text prompt | The prompt is ambiguous or target is too abstract. |

## Part 1: VLM Visual State Judgment

VLM workflows usually follow this path:

1. Define a clear prompt.
2. Select the VLM algorithm/template.
3. Bind it to an image/video task.
4. Review results and iterate the prompt.
5. Convert validated prompts into formal scenario tasks.

## Part 2: DINO Open-Vocabulary Detection

DINO detects objects described by text prompts such as `person`, `garbage`, `helmet`, or `vehicle`.

Prompt rules:

- Prefer concrete nouns.
- Use English category words when the model expects English prompts.
- Avoid long abstract descriptions.
- Test prompts with representative images before using them in a live task.

## Validation Checklist

- Does the model produce stable results on positive samples?
- Does it avoid obvious false positives on negative samples?
- Is latency acceptable?
- Are thresholds documented?
- Are screenshots safe for public docs?

## Next Step

Continue with [Volume 4: Pipeline Orchestration](../04-pipeline-orchestration/pipeline-orchestration.md).

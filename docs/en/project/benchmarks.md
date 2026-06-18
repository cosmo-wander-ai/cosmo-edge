---
title: Benchmark Reproduction Guide
description: Required environment information, metric definitions, test scenarios, execution checks, result table, and report naming.
prev:
  text: Public Validation Report
  link: /en/project/validation
next:
  text: Documentation Home
  link: /en/
---

# Benchmark Reproduction Guide

Use this template when publishing performance or stress-test results.

## Required Environment Information

| Field | Value |
| --- | --- |
| Git commit | TODO |
| Device model | TODO |
| CPU / memory | TODO |
| Accelerator / SDK | TODO |
| OS image | TODO |
| Runtime package | TODO |
| Model/resource version | TODO |

## Metric Definitions

Define throughput, latency, stream count, CPU usage, memory usage, accelerator usage, dropped frames, event delay, and error rate before reporting results.

## Recommended Test Scenarios

- Single stream smoke test.
- Multi-stream sustained test.
- Scenario task with OSD rendering.
- Event push integration.
- Model/resource loading test.
- Restart/recovery test.

## Result Table Template

| Scenario | Streams | Model | FPS | Latency | CPU | Memory | Accelerator | Result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |

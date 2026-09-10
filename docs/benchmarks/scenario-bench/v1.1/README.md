# CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark

> Person detection, no-safety-helmet analysis, and concurrent mixed-workload results on BM1688, CV186X, RK3576, and RV1126B.

Entry points: [English report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/report.html) · [中文报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/report.zh-CN.html) · [72-hour report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/dual-cv-72h/report.html) · [methodology](methodology.md) · [canonical case schema](results/cases.schema.json)

The HTML reports and aggregate indexes linked from this page are generated during the documentation build. The repository keeps the canonical measurements, not duplicate report payloads.

## 72-hour dual-CV configured workload

All four platforms completed one continuous 72-hour controlled local-loop observation. Each channel ran person detection plus no-safety-helmet analysis at 5 FPS per business task. The 72-hour endpoint contains the full observation, so 24- and 48-hour intermediate milestones are not published as separate results.

| Platform | Configured channels | Task bindings | Samples | Min / avg / max FPS | Max discard | Peak CPU / memory / disk | Result |
| --- | ---: | ---: | ---: | --- | ---: | --- | --- |
| BM1688 | 8 | 16 | 4316 / 4320 | 4.68 / 5.086 / 5.49 | 0 | 30% / 44% / 96% | PASS |
| CV186X | 8 | 16 | 4316 / 4320 | 4.54 / 5.085 / 5.29 | 0 | 43% / 44% / 96% | PASS |
| RK3576 | 8 | 16 | 4316 / 4320 | 5.00 / 5.098 / 5.17 | 0 | 46% / 30% / 15% | PASS |
| RV1126B | 4 | 8 | 4316 / 4320 | 4.85 / 5.230 / 5.37 | 0 | 41% / 41% / 47% | PASS |

All four platforms retained 4316 of 4320 expected one-minute samples (99.91%). The largest sampling gap was 60.067 seconds, below the 180-second integrity limit, with zero observed discard, collection errors, missing task bindings, or open critical incidents.

Scope: These results apply to the listed channel counts and controlled local-loop input. Maximum capacity, RTSP resilience, and restart recovery were not measured in this run. Disk observations, restart-state handling, evidence identity, and cleanup limitations are documented in the [methodology](methodology.md). See also the generated [72-hour report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/dual-cv-72h/report.html) and [canonical observation](results/dual-cv-72h.json).

## Concurrent mixed-workload matrix

Each channel runs two business tasks across three model stages: person detection has one detector stage, while no-safety-helmet analysis has a detector followed by a classifier. Both business tasks are configured at 5 FPS.

| Platform | Workload per channel | Model stages/ch | Target FPS/task | Passing channels | Business-task bindings |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | Person detection + no-safety-helmet analysis | 3 | 5 | ≥16 | 32/32 |
| CV186X | Person detection + no-safety-helmet analysis | 3 | 5 | ≥8 | 16/16 |
| RK3576 | Person detection + no-safety-helmet analysis | 3 | 5 | ≥8 | 16/16 |
| RV1126B | Person detection + no-safety-helmet analysis | 3 | 5 | ≥4 | 8/8 |

## Single-task capacity matrix

Values are the last passing channel count. `≥` means the highest configured count passed. `*` means the next channel was blocked during task binding. `†` means the expansion run was blocked by its storage precondition before measurement.

| Platform | Task | 24 FPS | 10 FPS | 7 FPS | 5 FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | Person detection | ≥8 | ≥16 | ≥16 | ≥16 |
| BM1688 | No-safety-helmet analysis | ≥7* | ≥14* | ≥16 | ≥16 |
| CV186X | Person detection | ≥8* | ≥15* | ≥16 | ≥16 |
| CV186X | No-safety-helmet analysis | 6 | ≥13* | ≥16 | ≥16 |
| RK3576 | Person detection | 6 | 12 | ≥16 | ≥8† |
| RK3576 | No-safety-helmet analysis | 6 | 10 | 12 | ≥16 |
| RV1126B | Person detection | 2 | ≥4 | ≥4 | ≥4 |
| RV1126B | No-safety-helmet analysis | 2 | ≥4 | ≥4 | ≥4 |

## Small-model controlled setup

- Small-model CosmoEdge source: `89c73a7464a81ef378686447d7c1eeb88b988455`, tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`.
- Input: fixed local-loop H.264 1920×1080, 24 FPS sample, SHA-256 `3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92`.
- Add one channel per step; hold 30 seconds; sample about every 3 seconds; preview disabled.
- Gates: 80% FPS compliance, zero telemetry missing rate, and at most 5% average discard.
- The no-safety-helmet task is a two-stage detector-plus-classifier pipeline; both nodes receive the same target FPS.

BM1688 and CV186X use byte-identical detector/classifier artifacts. RK3576 and RV1126B use platform-specific RKNN artifacts with the same public I/O contracts. Full hashes are in the [model card](models/model-card.md).

## Canonical data and generated reports

The 49 small-model cases are stored once in four platform-level canonical JSON files. The 72-hour observation and validated VLM performance each have one additional sanitized canonical file. Each source retains hashes that trace back to its private frozen evidence. Bilingual case pages, platform/workload/long-run summaries, indexes, and matrices are generated from these files; they are not additional evidence copies.

| Platform | Canonical cases | Generated overview | Generated case pages | Generated workload reports | 72-hour dual-CV | Validated VLM |
| --- | --- | --- | --- | --- | --- | --- |
| BM1688 | [JSON](results/bm1688/cases.json) | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/cases/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/single-workload/report.html">single</a> · <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/concurrent-mixed/report.html">mixed</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/dual-cv-72h/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/vlm-observation/report.html">open</a> |
| CV186X | [JSON](results/cv186x/cases.json) | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/cases/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/single-workload/report.html">single</a> · <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/concurrent-mixed/report.html">mixed</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/dual-cv-72h/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/vlm-observation/report.html">open</a> |
| RK3576 | [JSON](results/rk3576/cases.json) | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/cases/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/single-workload/report.html">single</a> · <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/concurrent-mixed/report.html">mixed</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/dual-cv-72h/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/vlm-observation/report.html">open</a> |
| RV1126B | [JSON](results/rv1126b/cases.json) | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/cases/report.html">open</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/single-workload/report.html">single</a> · <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/concurrent-mixed/report.html">mixed</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/dual-cv-72h/report.html">open</a> | — |

## Validated VLM performance

BM1688, CV186X, and RK3576 were retested on the frozen V1.1.0 candidate with the same controlled 1080p24 input, prompt, and 60-second step window. Each newly added route completed task-local readiness before formal sampling, and the 80% FPS threshold was an executed PASS/FAIL gate.

| Platform | Target FPS/ch | Executed FPS gate | Last passing channels | First failure |
| --- | ---: | ---: | ---: | --- |
| BM1688 | 0.1 | ≥80% | 6 channels | 7-channel FPS ratio 69.5% |
| CV186X | 0.1 | ≥80% | 6 channels | 7-channel FPS ratio 69.2% |
| RK3576 | 0.1 | ≥80% | 4 channels | 5-channel FPS ratio 69.7% |
| RV1126B | — | — | — | Outside this VLM validation |

BM1688, CV186X, and RK3576 also pass the minimum end-to-end sequence: model load, VLM task creation, valid inference, event/alarm output, and task recovery after service restart. Each platform used the same fixed candidate package as its capacity run. CosmoEdge 1.1 therefore declares VLM support on these three platforms within the recorded package, model, and protocol scope.

The result is an exact short-run gate boundary under this controlled protocol, not maximum-capacity or production-configuration certification. The method, package/model/runtime identities, and end-to-end evidence hashes are consolidated in the [canonical VLM file](results/vlm-observations.json) and [methodology](methodology.md).

## Reproduction files

- [release-manifest.json](release-manifest.json): source, tool, input, and platform identities.
- [dual-cv-72h.json](results/dual-cv-72h.json): sanitized 72-hour configured-workload observations and private-source hashes.
- [methodology.md](methodology.md): procedure and result interpretation.
- [scenarios](scenarios/README.md): sanitized public workload descriptors.
- <a href="./SHA256SUMS">SHA256SUMS</a>: hashes for the canonical repository source; the generated public output receives its own complete checksum inventory at build time.

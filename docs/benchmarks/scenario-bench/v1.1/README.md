# CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark

> Person detection, no-safety-helmet analysis, and concurrent mixed-workload results on BM1688, CV186X, RK3576, and RV1126B.

Entry points: [English report](report.html) · [中文报告](report.zh-CN.html) · [methodology](methodology.md) · [canonical case schema](results/cases.schema.json)

The HTML reports and aggregate indexes linked from this page are generated during the documentation build. The repository keeps the canonical measurements, not duplicate report payloads.

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

## Controlled setup

- CosmoEdge source: `89c73a7464a81ef378686447d7c1eeb88b988455`, tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`.
- Input: fixed local-loop H.264 1920×1080, 24 FPS sample, SHA-256 `3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92`.
- Add one channel per step; hold 30 seconds; sample about every 3 seconds; preview disabled.
- Gates: 80% FPS compliance, zero telemetry missing rate, and at most 5% average discard.
- The no-safety-helmet task is a two-stage detector-plus-classifier pipeline; both nodes receive the same target FPS.

BM1688 and CV186X use byte-identical detector/classifier artifacts. RK3576 and RV1126B use platform-specific RKNN artifacts with the same public I/O contracts. Full hashes are in the [model card](models/model-card.md).

## Canonical data and generated reports

The 49 small-model cases are stored once in four platform-level canonical JSON files. Each entry retains the complete measured staircase and the SHA-256 of its original frozen `summary.json`. Bilingual case pages, platform/workload summaries, indexes, and matrices are generated from these files; they are not additional evidence copies.

| Platform | Canonical cases | Generated overview | Generated case pages | Generated workload reports | Existing VLM |
| --- | --- | --- | --- | --- | --- |
| BM1688 | [JSON](results/bm1688/cases.json) | <a href="./results/bm1688/report.html">open</a> | <a href="./results/bm1688/cases/report.html">open</a> | <a href="./results/bm1688/single-workload/report.html">single</a> · <a href="./results/bm1688/concurrent-mixed/report.html">mixed</a> | <a href="./results/bm1688/vlm-observation/report.html">open</a> |
| CV186X | [JSON](results/cv186x/cases.json) | <a href="./results/cv186x/report.html">open</a> | <a href="./results/cv186x/cases/report.html">open</a> | <a href="./results/cv186x/single-workload/report.html">single</a> · <a href="./results/cv186x/concurrent-mixed/report.html">mixed</a> | <a href="./results/cv186x/vlm-observation/report.html">open</a> |
| RK3576 | [JSON](results/rk3576/cases.json) | <a href="./results/rk3576/report.html">open</a> | <a href="./results/rk3576/cases/report.html">open</a> | <a href="./results/rk3576/single-workload/report.html">single</a> · <a href="./results/rk3576/concurrent-mixed/report.html">mixed</a> | <a href="./results/rk3576/vlm-observation/report.html">open</a> |
| RV1126B | [JSON](results/rv1126b/cases.json) | <a href="./results/rv1126b/report.html">open</a> | <a href="./results/rv1126b/cases/report.html">open</a> | <a href="./results/rv1126b/single-workload/report.html">single</a> · <a href="./results/rv1126b/concurrent-mixed/report.html">mixed</a> | — |

This refresh updates small-model results only. The preceding VLM observations are consolidated in [one canonical file](results/vlm-observations.json) and remain experimental because FPS was not an enabled gate.

Candidate-bound VLM stress and long-running qualification are currently in progress and are not represented by these preserved observations or 30-second staircases. This page will be updated only after those runs complete and their source, package, model, environment, thresholds, duration, and cleanup identities are frozen.

The full pre-simplification evidence tree, including per-case commands, sanitized logs, summaries, metrics, and HTML, is recorded in the manifest as a separate archive. Its hash is frozen, but the archive is **prepared, not published**, and is not tracked in this repository.

## Reproduction files

- [release-manifest.json](release-manifest.json): source, tool, input, and platform identities.
- [methodology.md](methodology.md): procedure and result interpretation.
- [scenarios](scenarios/README.md): sanitized public workload descriptors.
- <a href="./SHA256SUMS">SHA256SUMS</a>: hashes for the canonical repository source; the generated public output receives its own complete checksum inventory at build time.

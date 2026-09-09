---
title: Video-Sample Algorithm Measurement
---

# Video-Sample Algorithm Measurement

`cosmo-accuracy` has one job: send video samples through a real CosmoEdge device, read persisted alarm
events, and measure how the algorithm behaves on those samples.

It reports:

- whether a positive video produced the expected minimum number of events;
- whether a negative video stayed below its allowed maximum;
- positive-hit, negative-clean, and false-positive rates per algorithm;
- result and timing changes across algorithms, configurations, software versions, or concurrency.

This is event-level, sample-level measurement. It has no frame labels or box matching, so it does not
calculate Precision, Recall, F1, mAP, or IoU. It is not a product gate, approved-baseline, or release
certification system.

## Suites and samples

Keep real suites, videos, and results in a private directory outside the repository:

```text
private-suite/
├── suite.yml
├── cases.jsonl
├── task-configs/
│   └── no-helmet.json
└── videos/
```

A task names the algorithm ID and schedule ID used by the device, plus one configuration source:

- `taskConfig` references an exported JSON configuration;
- `configSource: device-default` reads the device default at runtime.

A frozen local-video configuration must keep the video looping:

```json
{"key":"param.videoRepeatCount","value":"0"}
```

Each `cases.jsonl` row identifies a video and its event expectation:

```json
{"id":"helmet-positive-0001","task":"no-helmet","file":"videos/no-helmet/positive-0001.mp4","sha256":"<sha256>","expectation":{"minEvents":1},"tags":["warehouse","quick"]}
```

Negative samples normally use:

```json
{"expectation":{"maxEvents":0}}
```

Case IDs are unique, paths stay under the data root, and SHA-256 binds the run to the intended file.
The suite needs no gates or repetition policy. Existing `gates`, `trials`, and `critical` fields may
remain for compatibility with old private data. Gates and trials are ignored; `critical` is only a
legacy quick-selection marker and never triggers repeated measurement.

## Creating a draft from legacy folders

```bash
cd tools/scenario-bench
node src/accuracy-cli.js init-suite \
  --input-root /private/dataset \
  --output /private/suite-draft \
  --target-chip bm1688
```

The command recognizes positive/negative filename markers and computes sample hashes. It does not
create fake task configurations or guess algorithm and schedule IDs. Fill in those device-required
values and rename `suite.draft.yml` to `suite.yml`.

## Preflight

```bash
export COSMO_ACCURACY_TOKEN='<short-lived-token>'
node src/accuracy-cli.js doctor \
  --profile full \
  --concurrency 1 \
  --suite /private/suite/suite.yml \
  --data-root /private/dataset \
  --target-chip bm1688 \
  --device "${COSMO_DEVICE_URL}" \
  --token-env COSMO_ACCURACY_TOKEN
```

The alternative is `--user <account> --password-stdin`. Plaintext `--password` is rejected.

`doctor` checks only the selected videos:

- suite references, contained paths, and sample hashes;
- a valid finite video stream reported by ffprobe;
- device login, target chip, algorithms, and schedules;
- upload headroom for the requested concurrency;
- Event/Page readability;
- ffmpeg, the HTTP source, and MediaMTX for RTSP mode.

Existing `acc-*` temporary objects are warnings, not blockers, because every new trial has a unique
name. A missing video, algorithm, schedule, or required device capability still stops the run before
the first write.

## Running and concurrency

```bash
node src/accuracy-cli.js run \
  --profile full \
  --concurrency 2 \
  --suite /private/suite/suite.yml \
  --data-root /private/dataset \
  --target-chip bm1688 \
  --device "${COSMO_DEVICE_URL}" \
  --token-env COSMO_ACCURACY_TOKEN \
  --output /private/runs/helmet-v1
```

The execution rules are deliberately small:

- obtain one valid PASS or FAIL result for each selected sample;
- retry infrastructure ERROR according to the suite default;
- do not repeat an algorithm FAIL merely to confirm it;
- calculate rates from valid PASS/FAIL measurements while reporting infrastructure ERROR separately;
- return `0` for a complete measurement even when cases FAIL;
- return `2` when a trial cannot complete or strict per-trial cleanup fails;
- run CV with `--concurrency 1|2|4`;
- run VLM one case at a time after all CV cases finish.

`--profile full` runs the complete selection. `--profile quick` selects representative cases tagged
`quick`; `--case`, `--task`, and `--tag` provide direct filters. Filtering and concurrency do not
downgrade or disqualify a result because the tool has no eligibility or baseline concept.

## How one sample is measured

Each trial:

1. Rechecks the video hash.
2. Uploads the local file or starts a unique RTSP stream.
3. Creates a unique `acc-*` channel and applies the task configuration.
4. Waits for the camera and Decode/Detector counters to advance.
5. For VLM, also waits for a task-local completion counter and direct Qwen latency.
6. Reads persisted events from Event/Page during the observation window.
7. Stops early when an unbounded `minEvents` target is reached or `maxEvents` is exceeded.
8. Stops the task, pages through Event/Page, waits for the event-ID set to settle, and deduplicates it.
9. Evaluates PASS or FAIL from the final event count.
10. Saves alarm images and deletes this trial's task and channel.
11. Verifies cleanup and atomically checkpoints the result.

WebSocket messages are not a counting source. Events must match the channel, algorithm, and time
window so unrelated tasks cannot pollute the result. A cleanup failure for this trial becomes ERROR;
unrelated old `acc-*` objects do not overwrite an otherwise completed measurement.

## Output and exit result

```text
run-dir/
├── run.private.json
├── run.partial.json
├── summary.json
├── report.html
├── integrity.json
└── artifacts/alerts/
```

- `run.private.json` stores complete cases, trials, events, applied configuration hashes, timing, and cleanup;
- `summary.json` stores sanitized algorithm/config identity, sample status, micro/macro metrics, and coverage;
- `report.html` is an offline-readable report;
- `integrity.json` hashes the generated files and images;
- `run.partial.json` resumes unfinished cases when input, device, and tool identity still match.

The private result and sanitized summary are written first. Summary validation, HTML rendering, or
integrity generation failures produce warnings and do not erase a completed algorithm measurement.
The summary excludes credentials, device address, serial number, internal channel IDs, absolute paths,
and alarm URLs.

## Comparing measurements

```bash
node src/accuracy-cli.js compare \
  --reference /private/runs/reference/summary.json \
  --candidate /private/runs/algorithm-b/summary.json \
  --candidate /private/runs/concurrency-2/summary.json \
  --output /private/comparisons/algorithm-and-speed
```

Comparison requires the same selected video samples so the denominator is consistent. Algorithm ID,
taskConfig hash, concurrency, software version, source mode, target chip, suite, and tool version may
differ. They are listed in `contextChanges` instead of being used as eligibility blockers.

The output includes wall time, aggregate trial work, speedup, percentage-point changes in positive and
negative metrics, per-algorithm deltas, and every case status transition. It writes `comparison.json`,
`report.html`, and `integrity.json`.

## Threshold diagnostics and RTSP

`diagnose-threshold` accepts any previously measured FAIL case and scans only the parameters and values
explicitly listed by its suite task. It runs three isolated trials per value to distinguish stable pass,
stable fail, and variation. It never updates the suite, device defaults, or the original result.

`local` uploads the original video directly and is the default measurement path. Optional
`rtsp-deterministic` uses HTTP → ffmpeg → MediaMTX → device RTSP to expose ingestion effects. Both source
modes can be compared; the report makes the difference explicit.

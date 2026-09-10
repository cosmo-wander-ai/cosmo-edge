# Methodology

## How to read the reports

CosmoEdge 1.1 publishes three related evidence sets: small-model short-run capacity tests, one controlled 72-hour fixed-profile observation, and validated VLM short-run performance. Each result is bound to the recorded source, model, input, platform, gate, and duration. The four platform case files, `results/dual-cv-72h.json`, and `results/vlm-observations.json` are the canonical facts; aggregate indexes, matrices, and bilingual pages are generated from them.

BM1688, CV186X, RK3576, and RV1126B form the CosmoEdge 1.1 release-platform report. The no-safety-helmet workflow contains one detector followed by one classifier. A concurrent mixed-workload channel therefore contains two business tasks across three model stages: one person detector plus the detector-and-classifier helmet-analysis pipeline.

## Small-model capacity tests

The 2026-08-19 refresh covers person detection and no-safety-helmet analysis at 24, 10, 7, and 5 FPS, plus a concurrent mixed workload combining both business tasks at 5 FPS per task.

### Fixed variables

- Source commit and tree: see `release-manifest.json`.
- Input: one fixed H.264 1920×1080, 24 FPS sample, looped locally on every channel.
- Preview load: disabled.
- Ramp: add one channel at a time.
- Hold: 30 seconds per channel-count step.
- Sampling: approximately every 3 seconds; the second half of each step is the steady window.
- Models: platform identities and hashes are in `models/`.

ScenarioBench applies the requested FPS to both business tasks and to both stages of the helmet-analysis pipeline.

### Gates

For each complete step:

- minimum processing-FPS ratio: 0.80;
- telemetry missing rate: 0;
- average discard rate: at most 0.05;
- critical-path latency: at most 200 ms when reported;
- detector-node latency: at most 150 ms when reported.

`PASS` means the complete steady window met every enabled gate. A performance stop records the last passing step and the first failed metric. If the next channel cannot be bound, the report records the completed passing steps and labels the next step as binding-blocked. A precondition failure before step 1 is recorded as an execution block, not a measured step.

### Result selection

When both an 8-channel qualification run and a 16-channel expansion run exist:

1. use the completed expansion run for the main matrix;
2. retain the qualification run as a separate canonical case when it carries independent measurements;
3. if expansion is blocked before measurement, keep the completed qualification result;
4. if task binding fails after a steady step completed, count that completed step.

Each selected run is stored once in `results/<platform>/cases.json` with its measured steps and the SHA-256 of the original frozen summary.

## 72-hour configured-profile observation

### Workload and sampling

The completed window runs from `2026-08-20T17:44:30.341Z` through `2026-08-23T17:44:30.341Z`. It uses the same controlled local-loop 1080p24 input, with preview disabled, and runs person detection plus no-safety-helmet analysis on every channel at 5 FPS per business task. BM1688, CV186X, and RK3576 run 8 channels and 16 task bindings; RV1126B runs 4 channels and 8 bindings.

All platforms share one continuous 72-hour evidence window. The endpoint contains the full observation; 24- and 48-hour intermediate milestones are not separate evidence rows. The private multi-platform controller samples once per minute, giving 4320 expected samples per platform. The standard ScenarioBench CLI did not control this run.

### Integrity gates

The complete window passes only when all of the following hold:

- observed samples cover at least 95% of the expected window;
- first- and final-sample boundary lags are each at most 180 seconds;
- the maximum gap between adjacent samples is at most 180 seconds;
- the minimum observed task FPS is at least 80% of the 5 FPS target;
- observed discard is zero;
- collector-error, incomplete-binding, missing-binding, and open-critical-incident counts are zero.

### Resource observations

CPU and memory peaks are observations, not pass/fail gates. Disk utilization was also observational in the executed monitor, and no disk threshold participated in its integrity verdict. The deterministic public projection uses a 99% disk threshold. A 90% safeguard was added only after this observation and applies to future runs; it is not applied retroactively. BM1688 and CV186X stayed at 96% in every observed sample, RK3576 moved from 14% to 15%, and RV1126B moved from 46% to 47%. Accelerator telemetry is excluded from the public comparison because its source units are not uniform across platforms.

### Restart-state handling

Scheduled restart was already disabled on all four platforms before the observation. The controller treated that state as a controlled variable and verified it at startup and throughout the run; it did not need to force or restore the setting. Every platform recorded 80 successful checks, with no failure or corrective write, and ended disabled. This establishes control-variable continuity, not restart recovery.

### Evidence identity and cleanup limitations

The ScenarioBench source snapshot is frozen, but the private controller files were updated after the long-running process started and no launch-time controller digest was emitted. The launch-time controller bytes are therefore not claimed as frozen.

After the window completed, the public reports were regenerated deterministically from the complete private `metrics.jsonl` streams and final state. Public artifact hashes identify that projection. Separate SHA-256 values in the canonical record identify the private run manifest, suite state, suite summary, projection tool, and every platform metrics, summary, report, restart-guard, and cleanup artifact.

The private monitor record reports completed task and channel cleanup, restored layouts, zero remaining run-owned channels, and no cleanup errors. It did not emit a separate final-state artifact, so the cleanup conclusion is limited to that monitor record.

### Result interpretation

`PASS` means the configured channel profile completed the controlled 72-hour observation, passed the executed monitor checks, and passed the recorded post-run publication projection. The observation establishes sustained operation for the recorded profile. Capacity sizing, RTSP recovery, restart recovery, and final product-release approval are evaluated separately.

The canonical public projection is stored once in `results/dual-cv-72h.json`. Raw device identities, internal task/model/channel identifiers, addresses, commands, and local paths remain in private evidence referenced by SHA-256.

## Validated VLM performance

The 2026-08-24 validation uses the same frozen V1.1.0 candidate source, controlled local-loop 1080p24 input, prompt, and timing on BM1688, CV186X, and RK3576. It adds one channel per step, holds each measured level for 60 seconds, and samples every 3 seconds. Throughput comes from each task's local completion queue rather than a shared worker-batch counter.

### Task-local readiness and executed gates

Before formal sampling at every added level, the newly added route must advance its task-local completion counter and expose direct Qwen latency. Readiness probes are outside the 60-second hold window. Once readiness passes, every active route is evaluated against the following executed gates:

1. minimum task-local completion FPS ratio at least 80% of the 0.1 FPS-per-channel target;
2. zero telemetry missing rate;
3. average discard rate at most 5% and per-channel packet discard rate at most 1%; and
4. disk use no greater than 99%.

`PASS` therefore means the readiness precondition completed and the measured step passed the actual runtime gates. The exact short-run boundary requires a contiguous passing prefix followed immediately by a measured gate failure. Later configured levels are not needed once that adjacent failure is recorded.

### Results and scope

BM1688 and CV186X each pass six channels and first fail at seven, where the minimum active-route FPS ratios are 0.695 and 0.692. RK3576 passes four channels and first fails at five with a 0.697 ratio. In all three cases the first failure is the executed 80% FPS gate. RV1126B is outside this VLM validation.

Formal platform acceptance also checks model loading, VLM task creation, a valid inference result, event or alarm output, and task recovery after a service restart. On each platform, this acceptance used the same fixed candidate package as its capacity run. All three platforms pass; the canonical VLM file binds each result to its evidence SHA-256 without publishing private paths or device labels.

These are exact short-run boundaries for this protocol, not maximum-capacity certification, VLM long-run evidence, semantic-accuracy certification, or production sizing recommendations.

## Reproduction and verification

1. Verify the source, ScenarioBench, model, and sample hashes.
2. Resolve the public scenario descriptors to the device-local model and task configuration.
3. Run each case with its recorded target FPS and maximum channel count.
4. Keep raw summaries, commands, and sanitized logs in private evidence; project public measurements and source hashes into the canonical files.
5. When the private 72-hour source is available, run `npm run benchmarks:v1.1:verify-long-run-private -- --evidence-root <private-run-root>` to verify its hashes and semantics without copying raw evidence into the repository.
6. Run `npm run benchmarks:v1.1:validate` to check case semantics, public scrub, links, deterministic report generation, and checksums.
7. When the private VLM source is available, run `npm run benchmarks:v1.1:verify-vlm-private -- --evidence-root <private-run-root> --canonical docs/benchmarks/scenario-bench/v1.1/results/vlm-observations.json`.
8. Run the documentation build to generate bilingual HTML, aggregate indexes, matrices, and the built-output checksum inventory.

When the separately held small-model evidence archive is available, pass it to the validator with `--archive <path>`. That optional check verifies the archive hash and compares all 49 canonical small-model cases with their original frozen summaries. The archive is prepared but not published. VLM and 72-hour raw runs remain separate private evidence referenced by source hashes.

The private verifier emits only sanitized status and never publishes the evidence-root path. No public form may contain a device address, credential, device serial, internal model/task identifier, channel identifier, or local absolute path.

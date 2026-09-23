# InspecSafe evaluation tools

These tools implement the legacy `inspecsafe_rgb_en_joint_v1` and level-only
`inspecsafe_rgb_level_v2` protocols. Constructed tests demonstrate tooling behavior
only; they do not establish model quality, conversion success or device acceptance.
Keep datasets, model outputs and task-specific paths in a private ignored run directory.

## Level-only output with Markdown compatibility

For level-only evaluations, pass `--protocol inspecsafe_rgb_level_v2` to **both**
`tools.vlm_eval.runner` and `tools.vlm_eval.report`. The default remains the legacy
strict protocol, so replaying historical results does not silently accept wrappers.

The level protocol requires exactly `{"safety_level":"IV"}`, with I–IV as valid
values and no description. It also accepts one complete outer triple-backtick
fence, with either a `json` label (case-insensitive) or no language label. Opening
and closing fences must occupy separate lines; LF and CRLF are supported. Text
outside the fence, other language labels, multiple objects, duplicate/extra fields,
incomplete JSON, invalid levels and null remain invalid. No answer is inferred or
repaired. The prompt should still request plain JSON.

The supervisor preserves `raw_output`, records `output_protocol` and `output_parse`,
and counts compatible answers in completion and throughput only when execution
succeeded. Reports recalculate parsing from the raw text and include
`protocol_valid_count/rate`, `strict_valid_count/rate`, `strict_accuracy` (unwrapped
correct answers divided by the full truth count) and `markdown_recovered_count`.
`accuracy` and the confusion matrix use compatible answers; `joint_valid` fields
are retained as completion aliases for existing consumers. Failed first attempts
remain failures regardless of their text, and later retries never replace them.
An explicit result protocol must match the scoring protocol. Description scoring
is not applicable and does not require BGE or a description-score bundle.

Use a single frozen evaluation pass to measure level quality and request time;
the legacy three-round performance workflow below is not required by this protocol.
Keep historical raw files and reports separate from new evaluations.

```bash
python3 -m tools.vlm_eval.runner \
  --protocol inspecsafe_rgb_level_v2 \
  --manifest "$RUN/requests.independent.jsonl" --prompt "$RUN/prompt.txt" \
  --config "$RUN/config.json" --worker /path/to/cosmo-vlm-eval \
  --output "$RUN/results.jsonl" --warmup-image "$RUN/engineering.jpg" \
  --warmup 1 --timeout 60 --startup-timeout 120 --mode engineering
python3 -m tools.vlm_eval.report \
  --protocol inspecsafe_rgb_level_v2 \
  --truth "$RUN/truth.independent.jsonl" --results "$RUN/results.jsonl" \
  --output "$RUN/report.json"
```

These are illustrative commands; formal readiness and actual timing parameters
still apply. Execution identity now binds `output_protocol` as well as the parser
and supervisor hashes. For this protocol, `scorer_verified` means verification of
the level parser and failure-preserving metrics; it does not require a description
encoder, BGE weights or semantic scores. The following sections describe the legacy workflow unless
explicitly stated otherwise.

## Prepare private manifests

```bash
python3 -m tools.vlm_eval.prepare \
  --full "$RUN/frozen-full.jsonl" --source-map "$RUN/source-map.jsonl" \
  --annotations "$RUN/annotations.jsonl" --archive "$RUN/test.tar.gz" \
  --output "$RUN/prepared" --extract
```

The existing private calibration inventory maps as follows:

| Input | Required fields |
| --- | --- |
| Full frozen split | `asset_id`, `image_sha256`, `group_id`, Boolean `primary_heldout_eligible`, Boolean `assigned_to_calibration` |
| Source map | `asset_id`, `sha256`, `member` (exact archive member) |
| Annotation inventory | `asset_id`, `level_candidate`, `reference_text`, `source_member_stem`, `image.width`, `image.height` |

Source names are retained only on the scoring side. Scene is the annotation stem's basename prefix before the first `-`; review this dataset convention during semantic audit. Requests contain only `id`, neutral `image` path and `image_sha256`. Calibration groups cannot enter the independent split. Image bytes are verified against the frozen hash on extraction; without `--extract`, request paths are preparation records and may not exist yet.

Outputs include full/independent truth and requests, a deterministic performance request subset, label review selections, and file hashes. Performance selection round-robins scene/level/pixel-area strata with hash ordering, default seed 20260917 and count 100. Label review includes two samples per scene/level and all class III samples. These selections precede any model results. Review status starts pending; preparation does not mark labels or formal readiness verified.

## Execute a dedicated worker

```bash
python3 -m tools.vlm_eval.runner \
  --manifest "$RUN/prepared/requests.full.jsonl" --prompt "$RUN/prompt.txt" \
  --config "$RUN/config.json" --worker /path/to/cosmo-vlm-eval \
  --output "$RUN/results.jsonl" --warmup-image "$RUN/engineering.jpg" \
  --warmup 1 --timeout 60 --startup-timeout 120 --mode engineering
```

The worker emits `{"type":"ready","effective_config":{...}}`, then reads request objects containing `id`, `image`, `prompt`. Each response has `type:"result"`, matching `id`, execution status, and `raw_output`; the supervisor adds `attempt:1` and `elapsed_seconds`. Each worker must clear model history per request. The JSON answer must contain exactly `description` and `safety_level`. Duplicate/extra fields, Markdown wrappers, trailing text and non-JSON constants fail structural parsing. Description is trimmed and must be nonempty; safety level is trimmed at its boundaries and must then be exactly I, II, III or IV. Case and internal whitespace are not repaired. Missing fields are independently invalid.

Timeout failures are flushed and fsynced before terminating the process group and confirming exit. Recovery starts a new worker and verifies warmup, including after a failed final request. Failures remain in the output; automatic recovery does not retry the failed sample. Warmup requires an engineering image from a different source and verifies execution rather than answer format: `ok` and `empty_output` are accepted when no process restart is required. Warmup raw output and parser diagnostics are retained. Empty formal answers remain failures for scoring but do not alone trigger a process restart. No existing video service is stopped by this tool.

The metadata records cold starts, recovery events, cache policy, timing window and local worker RSS sampled every 20 ms. Throughput excludes only the initial startup and warmup; recovery shutdown, loading and warmup remain in the window. Setup durations and their exclusion decisions are recorded separately. Only execution-successful, strictly valid joint responses count toward effective throughput. RSS excludes accelerator allocations, descendants and remote-device memory; it is not a system/device memory measurement. Short peaks between samples can be missed. Run three separately recorded performance rounds; do not select the best round. Normal final unloading is outside the window; failure-triggered termination and recovery are inside it.

Performance summaries report valid-response and all-attempt latency separately. Image-preparation failures count as attempts even when no worker request was sent. Startup/warmup failures and explicitly unexecuted rows are distinguished. An incomplete round retains its failure counts and unexecuted count but has no complete-round throughput; a three-round throughput aggregate is unavailable unless all three rounds completed.

Recovery rounds require setup timing records that prove only the initial setup was excluded. Older metadata with recovery but without that evidence cannot produce a complete-round rate. A verified zero-recovery run can retain its measured window; scoring corrections must preserve the original raw files and capture identity and record any derived metadata separately.

Formal runs require `--mode formal --readiness readiness.json`. The record must have Boolean true values for `input_reference_verified`, `device_input_verified`, `artifact_verified`, `small_sample_verified`, `label_audit_complete`, `scorer_verified`, `parameters_frozen`, `timeout_recovery_verified`, and matching `manifest_sha256`, `prompt_sha256`, `config_sha256`. Populate it from measured acceptance evidence only. Engineering mode outputs are explicitly marked and cannot establish formal acceptance.

## Replay scoring

```bash
python3 -m tools.vlm_eval.report --truth "$RUN/prepared/truth.full.jsonl" \
  --results "$RUN/results.jsonl" --output "$RUN/report.json"
```

Truth requires `id`, `safety_level`; description scoring additionally uses `description`. `scene` and `group_id` are preserved in summaries. Results use the worker schema above; omitted attempt means 1. Duplicate attempts and IDs outside the frozen truth manifest are rejected. For independent reports, filter both truth and results to the same frozen IDs. Missing attempt 1 remains missing even if retries exist.

Reports retain failures in the denominator, expose INVALID/MISSING confusion columns, four-class macro-F1, per-class support/recall/F1, anomaly recall, normal false-positive/correct/incomplete rates, risk underestimation, I-to-IV errors, completion and failure rates, latency and scene-level reports. Unsupported truth classes have null recall/F1 and are excluded from macro-F1; false predictions into them remain counted. JSON null represents N/A. Raw outputs stay in the input JSONL, whose hash is included in report provenance.

Optional `--window-seconds` must come from the matching supervisor measurement window. Optional `--description-scores scores.json` consumes a precomputed BGE-M3 bundle:

```json
{
  "truth_sha256": "<SHA256 of exact truth JSONL>",
  "results_sha256": "<SHA256 of exact results JSONL>",
  "scorer": {
    "model": "BAAI/bge-m3",
    "weights_sha256": "<SHA256 of frozen weight manifest>",
    "encoding": "<frozen dense encoding configuration>",
    "normalization": "<frozen normalization configuration>",
    "truncation": "<frozen truncation configuration>",
    "grade_stripping": "<frozen grade removal rule>"
  },
  "scores": {"sample-id": {"status": "ok", "cosine": 0.8}}
}
```

Cosine values must be finite in [-1, 1]. This tool does not generate embeddings or substitute another model. Absent/failed scores for valid descriptions make aggregate description scores null until replayed; missing model descriptions count zero only when scoring otherwise completed. Freeze BGE weights, text processing, encoding and grade-stripping policy before formal evaluation. The example number above is illustrative, not a model measurement.

## Constructed checks

Run in the repository-supported Docker build image:

```bash
python3 -m unittest tools.vlm_eval.test_scoring tools.vlm_eval.test_runner tools.vlm_eval.test_prepare
```

These cover parsing, failure accounting, retry isolation, source-group leakage, neutral requests, hard timeout persistence, process exit and recovery warmup. Hardware input parity, actual configuration, history reset, context capacity, output stopping and peak device memory still require device evidence.

### Offline dense description encoder

`description.py` implements BGE-M3 dense encoding directly with the existing torch/transformers runtime. [BAAI's official dense-embedding documentation](https://bge-model.com/tutorial/1_Embedding/1.2.4.html) specifies normalized CLS hidden state. The project uses CPU float32, evaluation mode, no retrieval instruction, two texts per batch, L2 normalization and their dot product. Default maximum length is 8192; changes must be frozen before formal scoring.

```bash
python3 -m tools.vlm_eval.description --truth "$RUN/truth.jsonl" \
  --results "$RUN/results.jsonl" --model-dir "$RUN/bge-m3" \
  --output "$RUN/description-scores.json"
```

The model directory must contain a `weights-manifest.json` identifying `BAAI/bge-m3`, an immutable 40-character revision and SHA-256 entries for the dense weight file and tokenizer/config files. All listed hashes are verified before loading. Loading is local-only with remote code disabled. No dependencies, models or global caches are downloaded by this command. The score bundle freezes runtime versions, implementation hash, model revision and weight-manifest hash.

Grade stripping is a **project rule**, not a BGE official preprocessing standard: `explicit-level-labels-v2` removes complete explicit safety/risk-level clauses and level/grade labels, accepting Roman I–IV, Arabic 1–4 and English one–four and bracketed Roman level labels from both texts, then normalizes whitespace and surrounding punctuation. It retains bare `I` to avoid deleting the English pronoun and retains unlabelled Roman numerals inside scene descriptions. Dataset label review must verify this rule against the source annotations. Empty text after stripping and encoder failures are scoring errors requiring replay, never fabricated similarity zeroes. Process-level loading failures produce no completed bundle. Unit tests include grade stripping and failure isolation; actual weights must additionally pass constructed-text smoke checks.

### Formal execution identity

The readiness JSON must also contain `execution_identity`, matching the actual supervisor arguments and file contents:

```json
{
  "execution_identity": {
    "timeout_seconds": 120.0,
    "startup_timeout_seconds": 180.0,
    "warmup_count": 1,
    "warmup_image_sha256": "<SHA256>",
    "worker_sha256": "<SHA256 of the executable selected by --worker>",
    "artifact_sha256": {
      "model_path": "<SHA256 of the compiled bmodel file>",
      "tokenizer_path": "<SHA256 of the tokenizer JSON file>"
    }
  }
}
```

The numbers above illustrate the schema, not recommended or measured deadlines. The standalone worker config is a JSON object containing `model_path` and `tokenizer_path` as **file paths**, plus `max_new_tokens` and `context_length`; it does not accept a Hugging Face model directory in either file field. Relative paths resolve from the supervisor/worker working directory. `execution_identity(args)` computes this record with streaming file hashes after readiness evidence has been established. Changing a timeout, warmup count/image, executable, model bytes or tokenizer bytes requires a new validated readiness record, even if file paths remain unchanged. The supervisor retains the checked identity in formal-run metadata. Engineering mode does not claim this formal binding.

The report automatically adds `views.primary_independent` when every truth row
contains a Boolean `primary_heldout_eligible`. `--conflicts audit-summary.json`
accepts a predeclared `conflict_ids` list and adds a separately named sensitivity
view; it never changes the full report or its denominator. The primary report
continues to use publisher labels. Do not select conflict IDs after looking at
model outcomes.

Compare a device input dump with its frozen processor reference in an admitted
NumPy environment:

```bash
python3 -m tools.vlm_eval.compare_inputs --reference reference.npz \
  --dump-prefix device/sample --output comparison.json
```

This requires exact token IDs and grid, finite float32 patches, identical shape,
and maximum absolute error at most `2e-7`. Use original-size decoded RGB PNGs to
exercise device padding/resizing while isolating JPEG decoder differences.
A comparison of fabricated dumps only validates the checker, not the device.

### Canonical RGB inputs and device comparison

If decoder parity fails for JPEG but lossless RGB input passes, freeze original-size canonical PNGs with the admitted Pillow environment:

```bash
python3 -m tools.vlm_eval.canonicalize --manifest "$RUN/data/requests.full.jsonl" \
  --output "$RUN/canonical-rgb"
python3 -m tools.vlm_eval.compare_inputs --reference "$RUN/protocol/reference.npz" \
  --dump-prefix "$RUN/device/synthetic" --output "$RUN/device/synthetic-comparison.json"
```

Canonicalization requires source hashes, preserves IDs/order and original dimensions, applies no EXIF/ICC transform or resizing, verifies lossless PNG roundtrips and records source/PNG/RGB hashes. Use the resulting `requests.jsonl` and derive subset requests by the existing IDs; retain original truth and split manifests. Changing encoded inputs requires rebinding readiness to the new request-manifest hashes. Device dumps contain exact token IDs/grid and float32 patch values; comparison requires exact token/grid agreement, finite matching shapes and absolute pixel tolerance 2e-7 (relative tolerance zero). A failed comparison exits nonzero and writes its evidence; it is not an inference or quality score.

`cosmo-vlm-eval --check-input CONFIG.json` loads only `tokenizer_path` and processes stdin image/prompt/ID requests with a required `input_dump_prefix`. It emits `input_check_ready` then `input_check` records with `inference_executed:false`; no model is initialized. This checks the actual decoder, shared preprocessing and tokenizer without TPU inference.

`cosmo-vlm-eval --legacy-check CONFIG.json` is an engineering-only regression mode. It loads the model but never enables evaluation configuration, preserving the default resize, generation stopping and business output filter. It emits `legacy_check_ready` and `legacy_check`, with diagnostic `raw_output` and filtered `business_output`. These message types are intentionally rejected by the formal supervisor. Budgets come from artifact/business defaults, not the evaluation 256-token setting.

### RK3576 evaluation adapter

Offline builds and fixture checks do not establish device acceptance. Each RK run requires its own artifact and hardware admission evidence. Build with the existing Rockchip Docker environment using `scripts/build_rknn.sh ... -E`; `-E` enables `BUILD_VLM_EVAL` and requires RKLLM SDK/runtime availability. CMake chooses `cosmo_vlm_eval_rk.cc` only for RKLLM builds. Sophon builds retain their original source file and behavior.

The RK executable accepts `--config CONFIG.json`, then the same image/prompt/ID JSONL request schema. Its configuration differs from Sophon:

```json
{
  "backend": "rkllm",
  "model_path": "/private/model/model.rkllm",
  "max_new_tokens": 256,
  "context_length": 2048,
  "expected_input_tokens": 682,
  "vision_normalization": "embedded_mean127.5_std127.5"
}
```

`model_path` may identify the language artifact or its directory; `vision.rknn` must be adjacent. RKLLM carries its tokenizer inside the compiled artifact, so no external `tokenizer_path` is consumed by this CLI. `expected_input_tokens` is reference metadata, not a requirement that the measured RK count match another processor or platform. The backend records the observed input count and reference-match flag, and checks that the actual counts fit the configured context and output budgets. Declaring embedded mean/std does not prove the visual artifact implements it; admission and device tensor verification remain required. Requests provide original-size lossless RGB PNGs, and the backend performs the fixed evaluation preprocessing.

The backend returns unfiltered accumulated SDK text, SDK status/error codes and telemetry where available. Exact EOS versus output-budget termination is not exposed by this SDK: `truncated` remains null and the stop reason reports the observed SDK state rather than inventing certainty. `output_tokens` retains the SDK `RKLLMPerfStat.generate_tokens` convention. `callback_output_tokens` counts NORMAL result callbacks and excludes the FINISH callback token ID. `output_budget_reached` is based on that callback count reaching `max_new_tokens`; the callback count is also checked against the output budget. Reaching the budget does not prove truncation or identify the stopping cause. The JSONL transport reports image decode/allocation errors per instance and isolates vendor stdout from protocol output.

The Python readiness validator selects RK artifact identity only when config `backend` is `rkllm`; absent backend preserves the Sophon contract. It hashes the actual language artifact (including its embedded tokenizer) and adjacent `vision.rknn`, recording `artifact_sha256.model_path` and `artifact_sha256.vision_path`. Resolution matches C++: a directory selects its `model.rkllm`; an explicit `.rkllm` path selects that file; another file path selects sibling `model.rkllm`. No dummy external tokenizer is required. Config JSON, executable, supervisor parameters and all eight formal admission gates remain mandatory. This adds identity validation only. A run may be admitted only when its own artifact, device input, small-sample inference and timeout-recovery evidence satisfies the applicable admission gates. Code or fixture validation alone cannot establish those hardware facts. Any reused evidence must match the current frozen artifacts, configuration and applicable device conditions. The contract covers public-runtime raw artifacts; protected vision/model-guard certificate dependencies require separate admission and identity binding. Device hard timeouts must still terminate the dedicated process and confirm exit before restart; SDK abort alone is not acceptance evidence.

The RK CLI rejects missing or non-`rkllm` `backend` before model initialization. In formal mode, the supervisor additionally checks `ready.effective_config.backend` against the backend whose artifacts were bound in the frozen config; missing/mismatched identity is a startup failure. A warmup response with `process_restart_required:true` is a warmup failure even if its status and JSON fields appear valid. Recovery stops, preserving one row for every remaining manifest ID without sending those real requests. These checks do not turn unverified RK hardware into an admitted target.

### Paired platform comparison

After both platforms have actual results under the same frozen protocol, run:

```bash
python3 -m tools.vlm_eval.compare_platforms --truth "$RUN/data/truth.full.jsonl" \
  --results-a "$RUN/bm1688/results.jsonl" --results-b "$RUN/rk3576/results.jsonl" \
  --iterations 2000 --seed 20260917 --output "$RUN/paired-comparison.json"
```

Only `primary_heldout_eligible:true` rows enter this comparison. Both platforms use exactly the same frozen IDs, including missing/failed first attempts; retries never replace them. The bootstrap draws `group_id` source clusters with replacement and applies each draw to both platforms, retaining all samples in each selected cluster. It reports B-minus-A accuracy, macro-F1, joint completion, anomaly recall and normal false-positive differences with descriptive 95% percentile intervals. Smaller normal false-positive differences favor B; larger differences favor B for the other metrics. Fixed seed and input hashes support replay. Missing/unknown groups, duplicate attempts and out-of-manifest IDs are rejected. One source group yields no interval. Sparse classes may disappear in bootstrap draws; scorer support rules still apply, with undefined-replicate counts disclosed. These intervals are not business thresholds or proof that hardware alone caused a difference. Protocol/artifact comparability must be established separately; do not run without actual results from both platforms.

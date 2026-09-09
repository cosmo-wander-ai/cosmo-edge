# CosmoEdge 1.1 benchmark repository checklist

Status: **prepared for repository review; not yet published**

- [x] Source commit and tree frozen.
- [x] ScenarioBench source hashes frozen, including detector/classifier FPS override.
- [x] Controlled video metadata and SHA-256 frozen.
- [x] BM1688, CV186X, RK3576, and RV1126B model hashes recorded.
- [x] 49 small-model cases preserved once in four schema-bound canonical files.
- [x] Every canonical case retains the original frozen summary SHA-256.
- [x] Platform summaries, case pages, workload matrices, and indexes generated deterministically at build time.
- [x] 72-hour dual-CV observations preserved once in a sanitized canonical file with exact hashes for the private run manifest, suite state/summary, projection tool, and every platform metrics, summary, report, restart-guard, and cleanup artifact.
- [x] BM1688, CV186X, and RK3576 completed the configured 8-channel profile; RV1126B completed the configured 4-channel profile across one continuous 72-hour evidence window.
- [x] One-minute sampling coverage, boundary/gap integrity, throughput, discard, binding/telemetry completeness, incidents, scheduled-restart control state, disk trends, and cleanup limitations retained in canonical evidence and generated bilingual reports.
- [x] Executed-versus-post-run policy boundary recorded: disk was observational during execution, the deterministic projection uses 99%, and the later 90% safeguard is future-only and non-retroactive.
- [x] Scheduled restart was initially disabled on all platforms and held disabled; no restoration or restart-resilience claim is inferred.
- [x] ScenarioBench source identity is frozen; launch-time private-controller bytes remain explicitly unfrozen because no launch digest was emitted.
- [x] Private run hashes and semantics verified read-only without copying raw evidence or private paths into the repository.
- [x] Long-run wording is limited to the configured controlled local-loop profile and does not claim a capacity maximum, RTSP resilience, production recommendation, or product-release qualification.
- [x] 2026-08-24 BM1688, CV186X, and RK3576 rerun on the frozen V1.1.0 candidate with one input, prompt, timing, readiness, and gate protocol.
- [x] The executed 80% FPS gate resolves exact short-run boundaries: BM1688 6 (first failure 7), CV186X 6 (first failure 7), and RK3576 4 (first failure 5).
- [x] Unified task-local readiness and stop semantics retained in canonical evidence and methodology.
- [x] VLM task-local completion-counter and per-route readiness tool changes covered by tests.
- [x] ScenarioBench records readiness evidence and emits exact capacity only for an adjacent measured gate failure.
- [x] Canonical JSON semantics, generated HTML links/languages, public-data scrub, and source/generated checksums validated.
- [x] The 49-case small-model pre-simplification archive is frozen separately by commit, tree, and SHA-256.
- [x] VLM canonical data freezes metrics, summary, report, RC commit/tree, package, model, tokenizer, config, and runtime SHA-256 identities.
- [x] RK3576 VLM model SHA-256 recomputed after installing the frozen candidate.
- [x] BM1688, CV186X, and RK3576 pass model load, task creation, valid inference, event/alarm output, and task recovery after service restart on the frozen candidate package.

This checklist qualifies the benchmark publication candidate and the configured controlled 72-hour profile only. It does not claim broader soak stability, product-release qualification, RTSP resilience, accuracy acceptance, or customer-journey acceptance.

Do not copy `private-evidence/` or the small-model full evidence archive into the repository. Publishing the separately recorded archive as a release asset requires an explicit release action and final provenance review.

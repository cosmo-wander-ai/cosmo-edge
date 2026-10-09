# Laya offline reference tools

Device inference uses the repository-bound native Laya pipeline in `cosmo-engine`.
The device package does not install this directory or require Python, a Unix
socket worker, or a separate Laya service. Native model archives are prepared
with `scripts/laya_runtime/package_native_model.py`.

These helpers support offline question preparation, numeric comparisons and
explicit device graph replay using supplied, frozen assets. They are not model
weights and do not download or activate a model.

- `question_compiler.py`, `compile_questions.py`: deterministic question and
  cache reference used by the preparation tests and parity verifier.
- `image_frontend.py`, `dynamic_frontend.py`, `visual_backend.py`: reference
  preprocessing and multi-question evaluation, with no resident service entrypoint.
- `bmrt_bridge.py`: binding used for explicitly invoked numeric device replay.
- `visual_protocol.py`, `laya_shadow_worker.py`: historical reference framing;
  the latter retains its filename for frozen manifest compatibility but contains
  only `read_exact`, with no model loader, listener or executable worker.

The former `managed_worker.py`, `dynamic_worker.py`, standalone shadow worker
implementation, deployment units and worker lifecycle verifier have been removed.
Legacy C++ protocol/audit compatibility and historical alarm display remain;
production service registration selects the native compiler and inference path.

Run the asset-independent reference regressions from the repository root:

```sh
python3 -m unittest discover -s test -p 'test_laya_*.py' -v
```

They require NumPy and Pillow. Real tokenizer/model numerical parity additionally
requires the explicitly supplied reference assets and is not established by these
fixture tests. Old frozen manifests must retain their original private files and
hashes; do not point them at changed reference sources.

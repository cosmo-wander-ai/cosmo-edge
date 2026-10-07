#!/usr/bin/env python3
"""Replay prepared dynamic questions on one frozen BM1688 decision graph.

No service changes, model replacement, tokenizer installation, or business
qualification. The caller must free enough memory before running this probe.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for data in iter(lambda: f.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "reference", "questions", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--manifest-sha256", required=True)
    p.add_argument("--max-probability-error", type=float, default=.05)
    a = p.parse_args()
    if a.output.exists():
        raise FileExistsError("use a new evidence directory")
    if not 0 < a.max_probability_error <= .05:
        raise ValueError("numeric probability tolerance cannot exceed .05")
    if digest(a.manifest) != a.manifest_sha256:
        raise ValueError("manifest identity mismatch")
    manifest = json.loads(a.manifest.read_text())
    base = a.manifest.parent
    for name, spec in manifest["implementation"].items():
        if digest(base / spec["path"]) != spec["sha256"]:
            raise ValueError("frozen implementation mismatch: " + name)
    sys.path.insert(0, str(base))
    import numpy as np
    from bmrt_bridge import Bridge, frozen_file
    helper = frozen_file(base, manifest["cpu_helper"])
    spec = importlib.util.spec_from_file_location("frozen_cpu", helper)
    cpu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cpu)
    question_pack = json.loads(frozen_file(base, manifest["question_pack"]).read_text())
    heads_path = Path(manifest["cpu_directory"]) / "cpu_heads.npz"
    if digest(heads_path) != question_pack["cpu_files"]["cpu_heads.npz"]:
        raise ValueError("CPU heads mismatch")
    with np.load(heads_path, allow_pickle=False) as archive:
        heads = {key: archive[key] for key in archive.files}
    native = json.loads((a.reference / "report.json").read_text())
    if native["status"] != "PASS" or not native["cases"]:
        raise ValueError("native dynamic numeric gate must pass first")
    a.output.mkdir(mode=0o700, parents=True)
    report = {"status": "RUNNING", "scope": "decision graph numeric replay only",
              "manifest_sha256": a.manifest_sha256, "reference_sha256": digest(a.reference / "report.json"),
              "verifier_sha256": digest(__file__), "cases": [],
              "acceptance": {"finite": True, "same_top_option": True,
                             "max_calibrated_probability_abs_error": a.max_probability_error},
              "business_accuracy": "UNVERIFIED", "service_or_configuration_changed": False,
              "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip()}
    output = a.output / "report.json"
    bridge = None

    def save():
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")

    def probs(logits, temperature):
        x = logits[0] / temperature
        x = np.exp(x - x.max())
        return x / x.sum()

    try:
        bridge = Bridge(frozen_file(base, manifest["library"]), manifest.get("device_id", 0))
        report["before"] = bridge.snapshot()
        # Measured decision graph allocation is about 244 MiB. Retain margin.
        if report["before"]["heaps"][0]["available_mb"] < 330:
            report.update(status="NOT_RUN_MEMORY_RESERVE", required_heap0_free_mb=330)
            save()
            return 77
        graph = bridge.load(frozen_file(base, manifest["models"]["decision"]), manifest["models"]["decision"])
        report["after_load"] = bridge.snapshot()
        report["model_sha256"] = manifest["models"]["decision"]["sha256"]
        for item in native["cases"]:
            key = item["compiled_sha256"]
            pack = json.loads((a.questions / (key + ".json")).read_text())
            if pack["compiled_sha256"] != key:
                raise ValueError("question identity mismatch")
            with np.load(a.reference / (key + "-feeds.npz"), allow_pickle=False) as archive:
                feeds = {k: archive[k] for k in archive.files}
            with np.load(a.reference / (key + "-reference.npz"), allow_pickle=False) as archive:
                expected = {k: archive[k] for k in archive.files}
            start = time.monotonic()
            results, timing = graph.run(feeds)
            logits, acts = cpu.score_and_act(results["hidden_states"], expected["marker_positions"],
                                             expected["marker_mask"], heads)
            actual = probs(logits, pack["temperature"]["value"])
            target = probs(expected["option_logits"], pack["temperature"]["value"])
            finite = bool(np.isfinite(logits).all() and np.isfinite(acts).all() and np.isfinite(actual).all())
            same = bool(actual.argmax() == target.argmax())
            delta = float(np.abs(actual - target).max())
            row = {"id": pack["question_id"], "compiled_sha256": key,
                   "status": "PASS" if finite and same and delta <= a.max_probability_error else "FAIL",
                   "finite": finite, "same_top_option": same, "probability_max_abs": delta,
                   "option_logits_max_abs": float(np.abs(logits - expected["option_logits"]).max()),
                   "action_logits_max_abs": float(np.abs(acts - expected["action_logits"]).max()),
                   "probabilities": actual.tolist(), "reference_probabilities": target.tolist(),
                   "timing": timing, "seconds": time.monotonic() - start,
                   "inputs_sha256": digest(a.reference / (key + "-feeds.npz"))}
            np.savez_compressed(a.output / (key + "-scores.npz"), logits=logits, actions=acts,
                                probabilities=actual, reference_probabilities=target)
            report["cases"].append(row)
            save()
            print(json.dumps({k: row[k] for k in ("id", "status", "probability_max_abs")}), flush=True)
        report["status"] = "PASS" if all(x["status"] == "PASS" for x in report["cases"]) else "FAIL"
    except Exception as error:
        report.update(status="ERROR", error=type(error).__name__ + ": " + str(error))
        raise
    finally:
        if bridge:
            bridge.unload_all()
            report["after_unload"] = bridge.snapshot()
            bridge.close()
        save()
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

"""Resident visual encoder with bounded, versioned question replay.

Scores are predictions, not permission to filter alarms. Business policy and
scene qualification live in Cosmo's decision service and are audited separately.
"""
from collections import OrderedDict
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image

from bmrt_bridge import Bridge, frozen_file, sha256
from dynamic_frontend import DynamicImageFrontend, dynamic_decision_feeds
from question_compiler import QuestionCache
from visual_protocol import validate_request, response_base, failed_item, failure_response


class QuestionResolver:
    def __init__(self, cache, bindings, config, capacity=32):
        self.cache = cache
        self.bindings = bindings
        self.config = config
        self.capacity = capacity
        self.frontends = OrderedDict()

    def get(self, item):
        key = item["compiled_sha256"]
        if key not in self.frontends:
            frontend = DynamicImageFrontend(self.cache.get(key), key, self.bindings, self.config)
            self.frontends[key] = frontend
            while len(self.frontends) > self.capacity:
                self.frontends.popitem(last=False)
        frontend = self.frontends[key]
        self.frontends.move_to_end(key)
        if (frontend.pack["question_id"], frontend.pack["question_version"]) != (
                item["question_id"], item["question_version"]):
            raise ValueError("question_identity_mismatch")
        return frontend


class MultiQuestionRunner:
    """Single serving thread; visual features never survive a request boundary."""
    def __init__(self, models, table, heads, scorer, resolver, identity, manifest_sha256,
                 clock=time.monotonic):
        self.models, self.table, self.heads = models, table, heads
        self.scorer, self.resolver = scorer, resolver
        self.identity, self.manifest_sha256 = identity, manifest_sha256
        self.clock = clock

    def infer(self, request, encoded):
        validate_request(request)
        if request["manifest_sha256"] != self.manifest_sha256:
            return failure_response(request, "model_release_mismatch")
        deadline = request["deadline_monotonic_ms"] / 1000
        if self.clock() >= deadline:
            return failure_response(request, "deadline_exceeded")
        if len(encoded) != request["image_size"]:
            return failure_response(request, "image_size_mismatch")
        started = self.clock()
        rows, ready = [], []
        for item in request["items"]:
            try:
                frontend = self.resolver.get(item)
                ready.append((len(rows), item, frontend))
                rows.append(None)
            except (ValueError, OSError, KeyError, TypeError):
                rows.append(failed_item(item, "question_missing_or_mismatched"))
        if not ready:
            return {**response_base(request), "status": "unknown", "items": rows}
        try:
            with Image.open(io.BytesIO(encoded)) as image:
                if image.format != "JPEG" or image.size != (request["image_width"], request["image_height"]):
                    return failure_response(request, "image_format_or_dimensions_mismatch")
            prepared = ready[0][2].prepare(encoded, square_policy="center-pad-white-v1")
        except (ValueError, OSError):
            return failure_response(request, "invalid_image")
        preprocess_ms = (self.clock() - started) * 1000
        if self.clock() >= deadline:
            return failure_response(request, "deadline_exceeded")
        tower, tower_time = self.models["tower"].run({"pixel_values": prepared["pixel_values"]})
        if self.clock() >= deadline:
            return failure_response(request, "deadline_exceeded")
        adapter, adapter_time = self.models["adapter"].run({"tower_features": tower["tower_features"]})
        for index, item, frontend in ready:
            if self.clock() >= deadline:
                return failure_response(request, "deadline_exceeded")
            inputs = frontend.question_inputs()
            feeds = dynamic_decision_feeds(inputs, adapter["visual_tokens"], self.table, self.heads)
            decision, decision_time = self.models["decision"].run(feeds)
            logits, acts = self.scorer(decision["hidden_states"], inputs["marker_positions"],
                                      inputs["marker_mask"], self.heads)
            count = len(inputs["option_labels"])
            if logits.shape != (1, count) or acts.ndim != 2 or acts.shape[0] != 1 or not (
                    np.isfinite(logits).all() and np.isfinite(acts).all()):
                rows[index] = failed_item(item, "nonfinite_or_invalid_scores")
                continue
            scaled = logits[0] / inputs["temperature"]["value"]
            probabilities = np.exp(scaled - scaled.max())
            probabilities /= probabilities.sum()
            if not np.isfinite(probabilities).all():
                rows[index] = failed_item(item, "nonfinite_scores")
                continue
            order = np.sort(probabilities)
            rows[index] = {**item, "status": "completed", "business_qualified": False,
                           "ordered_options": inputs["option_labels"],
                           "probabilities": probabilities.tolist(),
                           "top1": inputs["option_labels"][int(probabilities.argmax())],
                           "top2_margin": float(order[-1] - order[-2]),
                           "qtype": inputs["qtype"], "temperature": inputs["temperature"],
                           "raw_option_logits": logits[0].tolist(), "raw_action_logits": acts[0].tolist(),
                           "decision_timing_ms": decision_time}
        # A complete response is the commit boundary. Even earlier scores in a
        # late batch cannot escape as successful decisions for a new task epoch.
        if self.clock() >= deadline:
            return failure_response(request, "deadline_exceeded")
        completed = sum(r["status"] == "completed" for r in rows)
        return {**response_base(request),
                "status": "completed" if completed == len(rows) else "partial" if completed else "unknown",
                "items": rows, "model_hashes": self.identity,
                "image_sha256": hashlib.sha256(encoded).hexdigest(),
                "image_transform": {k: v for k, v in prepared["metadata"].items()
                                    if k not in ("question_id", "question_version", "compiled_question_sha256")},
                "timing_ms": {"preprocess": preprocess_ms, "tower": tower_time, "adapter": adapter_time,
                              "worker_total": (self.clock() - started) * 1000}}


class Backend:
    def __init__(self, manifest_path, expected_sha256, cache_directory):
        if sha256(manifest_path) != expected_sha256:
            raise ValueError("worker_manifest_identity_mismatch")
        manifest = json.loads(Path(manifest_path).read_text())
        base = Path(manifest_path).parent
        if manifest.get("schema") != 2 or manifest.get("qualification") != "business-acceptance-pending":
            raise ValueError("unsupported_dynamic_manifest")
        required = {"visual_backend.py", "visual_protocol.py", "dynamic_frontend.py", "question_compiler.py",
                    "image_frontend.py", "bmrt_bridge.py", "laya_shadow_worker.py", "managed_worker.py",
                    "dynamic_worker.py", "compile_questions.py"}
        if not required <= set(manifest["implementation"]):
            raise ValueError("incomplete_implementation_manifest")
        for name, spec in manifest["implementation"].items():
            if frozen_file(base, spec) != Path(__file__).with_name(name).resolve():
                raise ValueError("implementation_path_mismatch")
        cpu_dir = base / manifest["cpu_directory"]
        for name in ("token_embeddings.f16.npy", "cpu_heads.npz", "rl_agent_config.json"):
            if sha256(cpu_dir / name) != manifest["cpu_files"][name]:
                raise ValueError("cpu_asset_identity_mismatch")
        table = np.load(cpu_dir / "token_embeddings.f16.npy", mmap_mode="r", allow_pickle=False)
        if table.shape != (256000, 768) or table.dtype != np.float16:
            raise ValueError("CPU embedding table profile mismatch")
        with np.load(cpu_dir / "cpu_heads.npz", allow_pickle=False) as archive:
            heads = {k: archive[k].copy() for k in archive.files}
        config = json.loads((cpu_dir / "rl_agent_config.json").read_text())
        # The compiler uses this exact local tokenizer; the resident worker only
        # validates its identity, and never imports tokenizers or its large table.
        frozen_file(base, manifest["tokenizer"])
        bindings = {"tokenizer_sha256": manifest["tokenizer"]["sha256"],
                    "model_config_sha256": manifest["cpu_files"]["rl_agent_config.json"]}
        resolver = QuestionResolver(QuestionCache(cache_directory), bindings, config)
        helper = frozen_file(base, manifest["cpu_helper"])
        spec = importlib.util.spec_from_file_location("frozen_visual_cpu", helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        paths = {role: frozen_file(base, spec) for role, spec in manifest["models"].items()}
        self.bridge = Bridge(frozen_file(base, manifest["library"]), manifest.get("device_id", 0))
        try:
            self.before = self.bridge.snapshot()
            if self.before["heaps"][0]["available_mb"] < 1150:
                raise RuntimeError("insufficient_model_memory_reserve")
            models = {role: self.bridge.load(paths[role], manifest["models"][role])
                      for role in ("tower", "adapter", "decision")}
            self.after = self.bridge.snapshot()
            self.identity = {role: manifest["models"][role]["sha256"] for role in models}
            self.runner = MultiQuestionRunner(models, table, heads, module.score_and_act,
                                              resolver, self.identity, expected_sha256)
        except BaseException:
            self.bridge.close()
            raise

    def infer(self, request, encoded):
        return self.runner.infer(request, encoded)

    def close(self):
        self.bridge.close()

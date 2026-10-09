"""Offline reference runner for versioned question replay.

Scores are predictions, not permission to filter alarms. Business policy and
scene qualification live in Cosmo's decision service and are audited separately.
"""
from collections import OrderedDict
import hashlib
import io
import time

import numpy as np
from PIL import Image

from dynamic_frontend import DynamicImageFrontend, dynamic_decision_feeds
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
    """Reference evaluation; visual features never survive a request boundary."""
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

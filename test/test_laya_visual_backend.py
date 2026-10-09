"""Typed protocol, per-ROI visual reuse, item isolation and configuration regressions."""
import copy
import io
from pathlib import Path
import sys
import tempfile
import time
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/laya_reference"))
from compile_questions import compile_batch
from question_compiler import QuestionCompiler, QuestionCache
from visual_backend import MultiQuestionRunner, QuestionResolver
from visual_protocol import PROTOCOL, PROFILE, validate_request, parse_json

CONFIG = {"head_max_len": 256, "temperature": [1., 1., 1.], "temperature_image": [1., 1., 1.]}
MANIFEST = "c" * 64


class Tokenizer:
    sha256 = "a" * 64

    def encode(self, text):
        return [ord(c) + 10 for c in text]


def spec(ident="helmet", text="帽?"):
    return {"id": ident, "version": 1, "type": "noul", "instructions": text}


def image(value):
    out = io.BytesIO()
    Image.new("RGB", (30, 20), value).save(out, format="JPEG")
    return out.getvalue()


def request(packs, encoded):
    return {"protocol": PROTOCOL, "profile": PROFILE, "manifest_sha256": MANIFEST,
            "request_id": "request", "task_id": "task", "run_epoch": "epoch", "frame_id": "frame",
            "roi_id": "area-A", "config_revision": "revision-1",
            "deadline_monotonic_ms": int(time.monotonic() * 1000) + 10000,
            "image_encoding": "jpeg", "image_size": len(encoded), "image_width": 30, "image_height": 20,
            "items": [{"item_id": "item-" + str(i),
                       **{k: p[k] for k in ("question_id", "question_version", "compiled_sha256")}}
                      for i, p in enumerate(packs)]}


class Graph:
    def __init__(self, role, after=None):
        self.role, self.after, self.calls = role, after, []

    def run(self, feeds):
        self.calls.append({k: v.copy() for k, v in feeds.items()})
        if self.role == "tower":
            out = {"tower_features": np.full((1, 256, 1152), feeds["pixel_values"].mean(), np.float32)}
        elif self.role == "adapter":
            out = {"visual_tokens": np.full((1, 130, 768), feeds["tower_features"].mean(), np.float32)}
        else:
            out = {"hidden_states": feeds["inputs_embeds"]}
        if self.after:
            self.after()
        return out, {"total_ms": 1.0}


def score(hidden, markers, mask, heads):
    return np.asarray([[1., 2.]], np.float32), np.asarray([[0., 1.]], np.float32)


class VisualBackendTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.cache = QuestionCache(self.directory.name)
        self.compiler = QuestionCompiler(Tokenizer(), CONFIG, "b" * 64)
        self.packs = [self.compiler.compile(spec()), self.compiler.compile(spec("fire", "火?"))]
        for p in self.packs:
            self.cache.put(p)
        self.graphs = {k: Graph(k) for k in ("tower", "adapter", "decision")}
        # A broadcast fixture avoids allocating a real 375 MiB embedding table.
        table = np.broadcast_to(np.zeros((1, 768), np.float16), (256000, 768))
        heads = {"type_emb.weight": np.zeros((3, 768), np.float32)}
        self.clock = time.monotonic()
        self.runner = MultiQuestionRunner(self.graphs, table, heads, score,
            QuestionResolver(self.cache, self.packs[0]["bindings"], CONFIG), {"test_only": "fixture"},
            MANIFEST, clock=lambda: self.clock)

    def test_multi_question_shares_only_this_image_and_preserves_identities(self):
        a = image("black")
        req = request(self.packs, a)
        result = self.runner.infer(req, a)
        self.assertEqual(result["status"], "completed")
        self.assertEqual([x["item_id"] for x in result["items"]], ["item-0", "item-1"])
        self.assertEqual([x["top1"] for x in result["items"]], ["true", "true"])
        self.assertFalse(any(x["business_qualified"] for x in result["items"]))
        self.assertEqual([len(self.graphs[k].calls) for k in self.graphs], [1, 1, 2])
        b = image("white")
        next_request = request(self.packs, b)
        next_request.update(roi_id="area-B", frame_id="next", run_epoch="new-epoch")
        second = self.runner.infer(next_request, b)
        self.assertEqual(second["roi_id"], "area-B")
        self.assertEqual(second["run_epoch"], "new-epoch")
        self.assertNotEqual(result["image_sha256"], second["image_sha256"])
        self.assertEqual([len(self.graphs[k].calls) for k in self.graphs], [2, 2, 4])
        self.assertFalse(np.array_equal(self.graphs["decision"].calls[0]["inputs_embeds"],
                                       self.graphs["decision"].calls[2]["inputs_embeds"]))

    def test_missing_or_mismatched_question_does_not_replace_another_item(self):
        data = image("red")
        req = request(self.packs, data)
        req["items"][0]["question_version"] = 2
        result = self.runner.infer(req, data)
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["items"][0]["reason"], "question_missing_or_mismatched")
        self.assertEqual(result["items"][1]["status"], "completed")
        self.assertEqual(len(self.graphs["decision"].calls), 1)
        req["manifest_sha256"] = "f" * 64
        result = self.runner.infer(req, data)
        self.assertEqual(result["reason"], "model_release_mismatch")
        self.assertEqual(len(self.graphs["tower"].calls), 1)

    def test_late_batch_never_publishes_even_earlier_scores(self):
        data = image("red")
        req = request(self.packs, data)
        deadline = req["deadline_monotonic_ms"] / 1000
        def expire():
            self.clock = deadline + 1
        self.graphs["decision"].after = expire
        result = self.runner.infer(req, data)
        self.assertEqual(result["reason"], "deadline_exceeded")
        self.assertEqual(len(result["items"]), 2)
        self.assertFalse(any("probabilities" in x for x in result["items"]))
        self.assertEqual(len(self.graphs["decision"].calls), 1)

    def test_image_binding_and_nonfinite_outputs(self):
        data = image("red")
        req = request(self.packs[:1], data)
        req["image_width"] += 1
        self.assertEqual(self.runner.infer(req, data)["reason"], "image_format_or_dimensions_mismatch")
        self.assertEqual(len(self.graphs["tower"].calls), 0)
        req["image_width"] -= 1
        self.runner.scorer = lambda *args: (np.array([[float("nan"), 0.]]), np.zeros((1, 2)))
        result = self.runner.infer(req, data)
        self.assertEqual(result["items"][0]["reason"], "nonfinite_or_invalid_scores")

    def test_compile_batch_failure_never_publishes_partial_configuration(self):
        with tempfile.TemporaryDirectory() as root:
            cache = QuestionCache(root)
            q = {"questions": [{"question": spec()}, {"question": spec("long", "x" * 300)}]}
            with self.assertRaises(ValueError):
                compile_batch(q, self.compiler, cache)
            self.assertEqual(list(Path(root).glob("*.json")), [])
            q["questions"].pop()
            result = compile_batch(q, self.compiler, cache)
            pack = cache.get(result["questions"][0]["compiled_sha256"])
            self.assertEqual(pack["question"], spec())

    def test_protocol_rejects_ambiguous_and_unbounded_requests(self):
        req = request(self.packs, image("red"))
        self.assertEqual(validate_request(req), req)
        for key, value in (("image_size", True), ("deadline_monotonic_ms", True),
                           ("items", req["items"] * 5), ("profile", "wrong")):
            bad = copy.deepcopy(req)
            bad[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_request(bad)
        bad = copy.deepcopy(req)
        bad["items"][1]["item_id"] = bad["items"][0]["item_id"]
        with self.assertRaises(ValueError):
            validate_request(bad)
        for payload in (b'{"a":1,"a":2}', b'{"x":NaN}', b'{"x":1e999}'):
            with self.assertRaises(ValueError):
                parse_json(payload)


if __name__ == "__main__":
    unittest.main()

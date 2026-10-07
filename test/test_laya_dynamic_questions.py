"""Question identity, rejection and padding-isolation regression tests.

The test tokenizer is only a deterministic boundary fixture. Real tokenizer and
upstream sequence equality are tested by verify_laya_question_parity.py.
"""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/laya_runtime"))
from question_compiler import QuestionCompiler, QuestionCache, fingerprint, normalize_question
from dynamic_frontend import DynamicImageFrontend, dynamic_masks
from image_frontend import additive_masks

CONFIG = {"head_max_len": 256, "temperature": [1.0, 1.0, 1.0],
          "temperature_image": [1.2, 1.1, 1.3],
          "temperature_by_options": {"image|noul:2": 1.25, "image|choice:3-5": 1.1}}


class TokenizerFixture:
    sha256 = "a" * 64

    def encode(self, text):
        return [10 + ord(c) for c in text]


def choice():
    return {"id": "helmet", "version": 1, "type": "choice", "instructions": "帽?",
            "criteria": {"戴帽": "", "未戴": "", "未知": ""}}


def compiler():
    return QuestionCompiler(TokenizerFixture(), CONFIG, "b" * 64)


class DynamicQuestionTests(unittest.TestCase):
    def test_question_order_and_revision_change_identity(self):
        original = choice()
        a = compiler().compile(original)
        reordered = copy.deepcopy(original)
        reordered["criteria"] = dict(reversed(list(reordered["criteria"].items())))
        b = compiler().compile(reordered)
        edited = copy.deepcopy(original)
        edited["instructions"] = "无帽?"
        c = compiler().compile(edited)
        edited["version"] = 2
        d = compiler().compile(edited)
        self.assertEqual(len({p["compiled_sha256"] for p in (a, b, c, d)}), 4)
        self.assertEqual(b["option_labels"], list(reordered["criteria"]))

    def test_boolean_semantics_ignore_dictionary_order(self):
        q = {"id": "fire", "version": 1, "type": "noul", "instructions": "火?",
             "criteria": {"true": "是", "false": "否"}, "labels": {"true": "是", "false": "否"}}
        p = compiler().compile(q)
        self.assertEqual(p["option_labels"], ["false", "true"])
        self.assertEqual(p["rendered_options"], ["否: 否", "是: 是"])
        self.assertEqual(p["qtype"], 2)
        self.assertEqual(p["temperature"]["value"], 1.25)

    def test_overflow_and_reserved_marker_are_errors(self):
        for change in ({"instructions": "长" * 180}, {"criteria": {"a": "长" * 49, "b": ""}},
                       {"instructions": "<mask>"}, {"version": True}, {"type": "score"}):
            q = choice()
            q.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                compiler().compile(q)
        with self.assertRaisesRegex(ValueError, "sequence_budget_exceeded"):
            compiler().compile(choice(), "背景" * 80)

    def test_cache_preserves_options_and_rejects_tampering(self):
        p = compiler().compile(choice())
        with tempfile.TemporaryDirectory() as directory:
            cache = QuestionCache(directory)
            key = cache.put(p)
            self.assertEqual(cache.get(key), p)
            self.assertEqual(list(cache.get(key)["question"]["criteria"]), p["option_labels"])
            path = Path(directory) / (key + ".json")
            changed = json.loads(path.read_text())
            changed["question"]["instructions"] = "changed"
            path.write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                cache.get(key)
            with self.assertRaises(ValueError):
                cache.get("../outside")

    def test_padding_fill_never_exposes_padded_keys_or_changes_real_queries(self):
        for valid in (150, 180, 191, 192, 220, 256):
            masks = dynamic_masks(valid)
            local = masks["sliding_attention_mask"][0, 0] == 0
            full = masks["full_attention_mask"][0, 0] == 0
            self.assertTrue(local.any(axis=-1).all())
            self.assertFalse(local[:, valid:].any())
            self.assertFalse(full[:, valid:].any())
            p = np.arange(256)
            expected = (p[None, :] < valid) & (np.abs(p[:, None] - p[None, :]) <= 64)
            np.testing.assert_array_equal(local[:valid], expected[:valid])
            if valid >= 192:
                for name, old in additive_masks(valid).items():
                    np.testing.assert_array_equal(masks[name], old)

    def test_prepare_isolated_inputs_and_asset_binding(self):
        p = compiler().compile(choice(), "背景")
        frontend = DynamicImageFrontend(p, p["compiled_sha256"], p["bindings"], CONFIG)
        img = Image.new("RGB", (80, 40), "red")
        a = frontend.prepare(img)
        a["input_ids"][0, 0] = 99
        a["sliding_attention_mask"][:] = 99
        b = frontend.prepare(img)
        self.assertEqual(b["input_ids"][0, 0], 2)
        self.assertFalse((b["sliding_attention_mask"] == 99).any())
        self.assertEqual(b["metadata"]["question_version"], 1)
        with self.assertRaises(ValueError):
            DynamicImageFrontend(p, p["compiled_sha256"], {}, CONFIG)
        bad = copy.deepcopy(p)
        bad["sequence"]["marker_positions"][0] += 1
        bad.pop("compiled_sha256")
        bad["compiled_sha256"] = fingerprint(bad)
        with self.assertRaisesRegex(ValueError, "marker"):
            DynamicImageFrontend(bad, bad["compiled_sha256"], p["bindings"], CONFIG)


if __name__ == "__main__":
    unittest.main()

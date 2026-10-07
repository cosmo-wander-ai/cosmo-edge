"""Configuration-cache, explicit option-order and atomic-preparation regressions."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/laya_runtime"))
from compile_questions import prepare_cached
from question_compiler import QuestionCompiler, QuestionCache


class Tokenizer:
    sha256 = "a" * 64
    def encode(self, text):
        return [ord(c) + 10 for c in text]


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.cache = QuestionCache(self.root.name)
        self.config = {"head_max_len": 256, "temperature": [1., 1., 1.]}
        self.compiler = QuestionCompiler(Tokenizer(), self.config, "b" * 64)
        self.binding = {"manifest_sha256": "c" * 64, "assets": {
            "tokenizer_sha256": "a" * 64, "model_config_sha256": "b" * 64}, "implementation": {"test": "d" * 64}}
        self.question = {"id": "helmet", "version": 1, "type": "choice", "instructions": "帽?",
                         "criteria": [{"label": "yes", "description": "有"},
                                      {"label": "no", "description": "无"}]}

    def encoded(self, question=None):
        return json.dumps({"questions": [{"question": question or self.question, "text_state": ""}]},
                          ensure_ascii=False, sort_keys=True).encode()

    def test_explicit_order_survives_sorted_engine_json(self):
        data = self.encoded()
        result = prepare_cached(data, self.binding, self.cache, lambda: self.compiler)
        self.assertEqual(result["questions"][0]["ordered_options"], ["yes", "no"])
        self.assertEqual(result["input_sha256"], hashlib.sha256(data).hexdigest())
        duplicate = copy.deepcopy(self.question)
        duplicate["criteria"][1]["label"] = "yes"
        with self.assertRaisesRegex(ValueError, "duplicate option"):
            prepare_cached(self.encoded(duplicate), self.binding, self.cache, lambda: self.compiler)

    def test_warm_restart_does_not_construct_tokenizer(self):
        first = prepare_cached(self.encoded(), self.binding, self.cache, lambda: self.compiler)
        self.assertFalse(first["cache_hit"])
        def forbidden():
            self.fail("warm cache allocated a tokenizer")
        second = prepare_cached(self.encoded(), self.binding, QuestionCache(self.root.name), forbidden)
        self.assertTrue(second["cache_hit"])
        self.assertEqual(first["questions"], second["questions"])

    def test_edit_order_and_text_each_invalidate_compiled_identity(self):
        first = prepare_cached(self.encoded(), self.binding, self.cache, lambda: self.compiler)
        edited = copy.deepcopy(self.question)
        edited["criteria"].reverse()
        second = prepare_cached(self.encoded(edited), self.binding, self.cache, lambda: self.compiler)
        edited["instructions"] = "火?"
        third = prepare_cached(self.encoded(edited), self.binding, self.cache, lambda: self.compiler)
        identities = {r["questions"][0]["compiled_sha256"] for r in (first, second, third)}
        self.assertEqual(len(identities), 3)
        self.assertEqual(second["questions"][0]["ordered_options"], ["no", "yes"])

    def test_corruption_is_reported_and_does_not_recompile_silently(self):
        result = prepare_cached(self.encoded(), self.binding, self.cache, lambda: self.compiler)
        ref = result["questions"][0]
        path = Path(self.root.name) / (ref["compiled_sha256"] + ".json")
        pack = json.loads(path.read_text())
        pack["option_labels"].reverse()
        path.write_text(json.dumps(pack))
        with self.assertRaisesRegex(ValueError, "corrupted"):
            prepare_cached(self.encoded(), self.binding, self.cache, lambda: self.fail("unexpected recompile"))

    def test_release_change_does_not_reuse_previous_preparation_receipt(self):
        prepare_cached(self.encoded(), self.binding, self.cache, lambda: self.compiler)
        binding = copy.deepcopy(self.binding)
        binding["manifest_sha256"] = "e" * 64
        calls = []
        result = prepare_cached(self.encoded(), binding, self.cache,
                                lambda: calls.append(True) or self.compiler)
        self.assertEqual(calls, [True])
        self.assertFalse(result["cache_hit"])
        self.assertEqual(result["binding"], binding)

    def test_failed_batch_does_not_publish_a_partial_preparation(self):
        bad = copy.deepcopy(self.question)
        bad.update(id="too-long", instructions="A" * 300)
        request = {"questions": [{"question": self.question}, {"question": bad}]}
        with self.assertRaises(ValueError):
            prepare_cached(json.dumps(request).encode(), self.binding, self.cache, lambda: self.compiler)
        self.assertEqual(list(Path(self.root.name).rglob("*.json")), [])


if __name__ == "__main__":
    unittest.main()

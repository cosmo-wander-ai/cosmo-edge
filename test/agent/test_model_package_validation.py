#!/usr/bin/env python3
"""Synthetic package fixtures exercise local checks, not runnable model acceptance."""

import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow as core
import model_conversion_workflow as conversion
from model_package_validation import inspect_container


def cenn(*payloads: bytes) -> bytes:
    return struct.pack("<4sHHII8Q", b"CENN", 1, 80, len(payloads), 0,
                       *(list(map(len, payloads)) + [0] * (8 - len(payloads)))) + b"".join(payloads)


def package_fixture(run: Path, *, input_shape=None, output_shape=None):
    input_shape = input_shape or [1, 3, 320, 320]
    output_shape = output_shape or [1, 7, 2100]
    package = run / "prod_BM1688_1234567_fixture_V1.0.0"
    package.mkdir()
    payload = b"synthetic-bmodel-fixture"
    model = run / "candidate.bmodel"
    model.write_bytes(payload)
    (package / "model.nn").write_bytes(cenn(payload))
    config = {
        "chip_type": "BM1688", "model_type": "yolov8_det",
        "labels": [{"id": str(i), "name": f"category-{i}"} for i in range(3)],
        "models": [{"inputs": [{"name": "images", "shape": input_shape}],
                    "outputs": [{"name": "output", "shape": output_shape}],
                    "params": {"input_size": input_shape[2:]}}],
    }
    (package / "config.json").write_text(json.dumps(config))
    report = run / "model-info.txt"
    report.write_text(f"input: images, {input_shape}, float32, scale: 1\noutput: output, {output_shape}, float32, scale: 1\n")
    parameters = {
        "raw": {"outputKind": "model-package", "packageDirectory": package.name},
        "targetChip": "bm1688", "inputLayout": "NCHW",
        "inputShapes": [input_shape], "expectedOutputShapes": [output_shape],
    }
    artifacts = [{"path": model.name, "sha256": core.sha256_file(model), "sizeBytes": len(payload)}]
    info = {"status": "PASS", "contractMatches": True,
            "report": report.name, "reportSha256": core.sha256_file(report)}
    return package, config, parameters, artifacts, info


class ModelPackageValidationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.run = Path(self.temp.name)
        self.package, self.config, self.parameters, self.artifacts, self.info = package_fixture(self.run)

    def check(self):
        (self.package / "config.json").write_text(json.dumps(self.config))
        return conversion._package_stage({}, self.run, self.parameters, self.artifacts, self.info)

    def test_golden_header_and_payload_digest_exclude_container(self):
        path = self.run / "golden.nn"
        # Fixed bytes match BuildPlainNnHeader({3}); no dependency on the writer above.
        path.write_bytes(bytes.fromhex("43454e4e0100500001000000000000000300000000000000") + bytes(56) + b"abc")
        result = inspect_container(path)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["segments"][0]["sha256"], hashlib.sha256(b"abc").hexdigest())
        self.assertNotEqual(result["segments"][0]["sha256"], core.sha256_file(path))

    def test_valid_non_640_candidate_and_both_output_layouts(self):
        self.assertEqual(self.check()["status"], "PASS")
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            _, _, parameters, artifacts, info = package_fixture(run, input_shape=[1, 3, 384, 512], output_shape=[1, 4032, 7])
            self.assertEqual(conversion._package_stage({}, run, parameters, artifacts, info)["status"], "PASS")

    def test_streamed_payload_larger_than_chunk(self):
        path = self.run / "large.nn"
        payload = b"x" * (1024 * 1024 + 17)
        path.write_bytes(cenn(payload))
        self.assertEqual(inspect_container(path)["segments"][0]["sha256"], hashlib.sha256(payload).hexdigest())

    def test_raw_bmodel_and_nonmodel_text_fail(self):
        for payload in (b"not a model", (self.run / "candidate.bmodel").read_bytes(), b""):
            with self.subTest(payload=payload):
                (self.package / "model.nn").write_bytes(payload)
                self.assertEqual(self.check()["status"], "FAIL")

    def test_malformed_cenn_headers_sizes_and_trailing_data_fail(self):
        original = (self.package / "model.nn").read_bytes()
        variants = [original[:25], original[:-1], original + b"x"]
        for offset, fmt, value in ((6, "H", 79), (8, "I", 0), (8, "I", 9),
                                   (12, "I", 1), (16, "Q", 0), (24, "Q", 1),
                                   (16, "Q", (1 << 64) - 1)):
            bad = bytearray(original)
            struct.pack_into("<" + fmt, bad, offset, value)
            variants.append(bytes(bad))
        for payload in variants:
            with self.subTest(payload=payload[:24]):
                (self.package / "model.nn").write_bytes(payload)
                self.assertEqual(self.check()["status"], "FAIL")

    def test_protected_and_new_header_version_remain_unverified(self):
        newer = bytearray((self.package / "model.nn").read_bytes())
        struct.pack_into("<H", newer, 4, 2)
        for payload in (b"CEMC" + bytes(80), bytes(newer)):
            (self.package / "model.nn").write_bytes(payload)
            self.assertEqual(self.check()["status"], "UNVERIFIED")

    def test_legacy_encrypted_header_is_rejected_by_current_policy(self):
        (self.package / "model.nn").write_bytes(b"\x01\x00\x01\xec" + bytes(80))
        result = self.check()
        self.assertEqual(result["status"], "FAIL")
        self.assertIn("rejected", result["checks"][0]["detail"])

    def test_effective_input_batch_uses_max_batch_and_its_real_default(self):
        model = self.config["models"][0]
        self.assertNotIn("max_batch", model)
        self.assertEqual(self.check()["status"], "PASS")  # production parser defaults to 1
        model["max_batch"] = 4
        self.assertEqual(self.check()["status"], "FAIL")  # configured axis 0 cannot hide batch 4
        model["max_batch"] = 0
        self.assertEqual(self.check()["status"], "FAIL")
        model["max_batch"] = "4"
        self.assertEqual(self.check()["status"], "UNVERIFIED")
        model["max_batch"] = 1
        model["inputs"][0]["shape"] = [-1, 3, 320, 320]
        model["outputs"][0]["shape"] = [-1, 7, 2100]
        self.assertEqual(self.check()["status"], "PASS")
        model["inputs"][0]["shape"] = [4, 3, 320, 320]
        self.assertEqual(self.check()["status"], "PASS")  # effective first axis is still 1
        model["inputs"][0]["shape"] = [-1, 3, -1, 320]
        self.assertEqual(self.check()["status"], "FAIL")  # only the batch axis is replaced

    def test_batch_four_candidate_requires_batch_four_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            package, config, parameters, artifacts, info = package_fixture(
                run, input_shape=[4, 3, 320, 320], output_shape=[4, 7, 2100])
            self.assertEqual(conversion._package_stage({}, run, parameters, artifacts, info)["status"], "FAIL")
            config["models"][0]["max_batch"] = 4
            config["models"][0]["inputs"][0]["shape"] = [-1, 3, 320, 320]
            (package / "config.json").write_text(json.dumps(config))
            self.assertEqual(conversion._package_stage({}, run, parameters, artifacts, info)["status"], "PASS")

    def test_feature_affine_template_uses_output_hw_without_input_size(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            package, _, parameters, artifacts, info = package_fixture(
                run, input_shape=[1, 3, 112, 112], output_shape=[1, 512])
            config = json.loads((ROOT / "data/resource/aiboxresource_bm1688/model_template/feature.json").read_text())
            params = config["models"][0]["params"]
            self.assertNotIn("input_size", params)
            for changes, expected in (({}, "PASS"), ({"output_hw": [224, 224]}, "FAIL"),
                                      ({"output_hw": None}, "PASS"),
                                      ({"use_affine_crop": False, "output_hw": [224, 224], "input_size": [112, 112]}, "PASS")):
                params.update(changes)
                (package / "config.json").write_text(json.dumps(config))
                self.assertEqual(conversion._package_stage({}, run, parameters, artifacts, info)["status"], expected)

    def test_unknown_preprocessing_contract_is_unverified_not_rejected(self):
        self.config["model_type"] = "custom_pipeline"
        self.config["models"][0]["params"] = {"output_hw": [320, 320]}
        self.assertEqual(self.check()["status"], "UNVERIFIED")

    def test_same_size_wrong_payload_is_rejected(self):
        original = (self.run / "candidate.bmodel").read_bytes()
        (self.package / "model.nn").write_bytes(cenn(b"x" * len(original)))
        result = self.check()
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(next(c for c in result["checks"] if c["field"] == "segments[0].artifact")["status"], "FAIL")

    def test_declared_shape_input_size_chip_conflicts_fail_with_values(self):
        variants = (("output", [1, 84, 2100]), ("input", [1, 3, 640, 640]),
                    ("size", [640, 640]), ("chip", "CV186X"))
        baseline = json.dumps(self.config)
        for key, value in variants:
            self.config = json.loads(baseline)
            if key in ("output", "input"):
                self.config["models"][0][key + "s"][0]["shape"] = value
            elif key == "size":
                self.config["models"][0]["params"]["input_size"] = value
            else:
                self.config["chip_type"] = value
            with self.subTest(key=key):
                result = self.check()
                self.assertEqual(result["status"], "FAIL")
                self.assertTrue(any(c["status"] == "FAIL" and "expected" in c and "actual" in c for c in result["checks"]))

    def test_yolov8_duplicate_and_out_of_range_labels_fail_but_subset_passes(self):
        for ids, status in (([0, 0], "FAIL"), ([0, 3], "FAIL"), ([79], "FAIL"),
                            ([-1], "FAIL"), ([True], "FAIL"), ([0, 2], "PASS"), ([], "PASS")):
            self.config["labels"] = [{"id": value} for value in ids]
            with self.subTest(ids=ids):
                self.assertEqual(self.check()["status"], status)

    def test_other_model_types_do_not_inherit_yolov8_label_rule(self):
        self.config["model_type"] = "classify"
        self.config["labels"] = [{"id": "79"}]
        self.assertEqual(self.check()["status"], "PASS")

    def test_missing_or_ambiguous_model_info_cannot_pass(self):
        for text in ("[1,3,320,320] [1,7,2100]", "input: a, [1,3,320,320]\noutput: a, [1,7,2100]\noutput: b, [1,7,2100]"):
            (self.run / self.info["report"]).write_text(text)
            self.info["reportSha256"] = core.sha256_file(self.run / self.info["report"])
            self.assertEqual(self.check()["status"], "UNVERIFIED")
        self.info.pop("report")
        self.assertEqual(self.check()["status"], "UNVERIFIED")

    def test_unfrozen_report_identity_is_unverified(self):
        self.info.pop("reportSha256")
        self.assertEqual(self.check()["status"], "UNVERIFIED")

    def test_report_tampering_or_directional_shape_conflict_fails(self):
        report = self.run / self.info["report"]
        report.write_text("input: images, [1,3,320,320]\noutput: out, [1,84,2100]\n")
        self.assertEqual(self.check()["status"], "FAIL")
        self.info["reportSha256"] = core.sha256_file(report)
        self.assertEqual(self.check()["status"], "FAIL")

    def test_incomplete_artifact_or_shape_evidence_is_unverified(self):
        self.artifacts = []
        self.assertEqual(self.check()["status"], "UNVERIFIED")
        self.parameters["expectedOutputShapes"] = []
        self.assertEqual(self.check()["status"], "UNVERIFIED")

    def test_multiple_payloads_need_per_segment_configuration_evidence(self):
        payload = (self.run / "candidate.bmodel").read_bytes()
        (self.package / "model.nn").write_bytes(cenn(payload, payload))
        self.assertEqual(self.check()["status"], "FAIL")  # two segments, one config
        self.config["models"].append(self.config["models"][0])
        self.assertEqual(self.check()["status"], "UNVERIFIED")
        (self.package / "model.nn").write_bytes(cenn(payload, b"second-unrecorded-model"))
        self.assertEqual(self.check()["status"], "UNVERIFIED")

    def test_non_object_config_or_escaping_symlink_fails(self):
        self.config = []
        self.assertEqual(self.check()["status"], "FAIL")
        (self.package / "model.nn").unlink()
        (self.package / "model.nn").symlink_to(ROOT / "AGENTS.md")
        self.assertEqual(self.check()["status"], "FAIL")

    def test_bmodel_only_does_not_require_a_package(self):
        self.parameters["raw"] = {"outputKind": "bmodel"}
        self.assertEqual(self.check()["status"], "SKIP")


if __name__ == "__main__":
    unittest.main()

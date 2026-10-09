import json
import unittest

from tools.vlm_eval.parser import LEVEL_PROTOCOL, parse_output
from tools.vlm_eval.scoring import score_results


def row(key, level="IV", **kwargs):
    return dict(id=key, status="ok", raw_output=json.dumps(dict(description="A scene.", safety_level=level)), **kwargs)


class ParserTests(unittest.TestCase):
    def test_strict_structure(self):
        for raw in ('```json\n{}\n```', '{} trailing', '[]', '{"description":"a","description":"b"}',
                    '{"description":"a","safety_level":"IV","extra":1}', '{"description":NaN}'):
            self.assertEqual(parse_output(raw)["error"], "parse_failure")

    def test_independent_fields_and_no_level_repair(self):
        result = parse_output('{"description":" A scene. ","safety_level":" iv "}')
        self.assertTrue(result["description_valid"])
        self.assertFalse(result["safety_level_valid"])
        self.assertEqual(result["description"], "A scene.")
        self.assertTrue(parse_output('{"safety_level":"I"}')["safety_level_valid"])


class ScoringTests(unittest.TestCase):
    def test_failures_and_retries_preserve_denominator(self):
        truths = [dict(id=str(i), safety_level=level) for i, level in enumerate(("I", "II", "III", "IV"))]
        result = score_results(truths, [row("0", "I"), dict(id="1", status="timeout"), row("1", "II", attempt=2)])
        self.assertEqual(result["total"], 4)
        self.assertEqual(result["accuracy"], .25)
        self.assertEqual(result["macro_f1"], .25)
        self.assertEqual(result["confusion_matrix"]["II"]["INVALID"], 1)
        self.assertEqual(result["confusion_matrix"]["III"]["MISSING"], 1)
        self.assertEqual(result["retry_count"], 1)
        self.assertEqual(result["per_level"]["II"]["recall"], 0)

    def test_unsupported_class_excluded_but_false_positives_reported(self):
        result = score_results([dict(id="a", safety_level="IV")], [row("a", "I")])
        self.assertIsNone(result["per_level"]["I"]["f1"])
        self.assertEqual(result["per_level"]["I"]["false_positive"], 1)
        self.assertEqual(result["macro_f1"], 0)
        self.assertIsNone(result["anomaly_recall"])

    def test_scoring_service_fault_is_not_zero(self):
        truth = [dict(id="a", safety_level="IV"), dict(id="b", safety_level="IV")]
        result = score_results(truth, [row("a")])
        self.assertIsNone(result["description"]["all_sample_score"])
        result = score_results(truth, [row("a")], {"a": dict(status="ok", cosine=.8)})
        self.assertEqual(result["description"]["valid_mean"], .8)
        self.assertEqual(result["description"]["all_sample_score"], .4)

    def test_execution_failure_cannot_become_valid_answer(self):
        result = row("a")
        result["status"] = "timeout"
        report = score_results([dict(id="a", safety_level="IV")], [result])
        self.assertEqual(report["joint_valid_count"], 0)
        self.assertEqual(report["accuracy"], 0)

    def test_duplicate_attempt_rejected(self):
        with self.assertRaises(ValueError):
            score_results([dict(id="a", safety_level="IV")], [row("a"), row("a")])

    def test_scene_metrics_keep_scene_denominator(self):
        truths = [dict(id="a", safety_level="I", scene="road"),
                  dict(id="b", safety_level="IV", scene="road"),
                  dict(id="c", safety_level="IV", scene="room")]
        report = score_results(truths, [row("a", "I"), row("c")])
        self.assertEqual(report["by_scene"]["road"]["total"], 2)
        self.assertEqual(report["by_scene"]["road"]["accuracy"], .5)
        self.assertEqual(report["by_scene"]["room"]["accuracy"], 1)

    def test_retry_without_first_stays_missing(self):
        report = score_results([dict(id="a", safety_level="IV")], [row("a", attempt=2)])
        self.assertEqual(report["confusion_matrix"]["IV"]["MISSING"], 1)

    def test_empty_manifest_is_na(self):
        report = score_results([], [])
        self.assertIsNone(report["accuracy"])
        self.assertIsNone(report["macro_f1"])


class LevelScoringTests(unittest.TestCase):
    def test_fences_count_as_predictions_with_strict_comparison(self):
        truths = [dict(id="a", safety_level="IV", scene="room"),
                  dict(id="b", safety_level="I", scene="room")]
        rows = [dict(id="a", status="ok", raw_output='```json\n{"safety_level":"IV"}\n```', elapsed_seconds=2),
                dict(id="b", status="ok", raw_output='{"safety_level":"I"}', elapsed_seconds=3)]
        report = score_results(truths, rows, protocol=LEVEL_PROTOCOL)
        self.assertEqual(report["accuracy"], 1)
        self.assertEqual(report["protocol_valid_rate"], 1)
        self.assertEqual(report["strict_valid_rate"], .5)
        self.assertEqual(report["strict_correct_count"], 1)
        self.assertEqual(report["strict_accuracy"], .5)
        self.assertEqual(report["markdown_recovered_count"], 1)
        self.assertEqual(report["description"]["status"], "not_applicable")
        self.assertIsNone(report["description"]["all_sample_score"])
        self.assertEqual(report["description"]["scoring_failure_count"], 0)
        self.assertEqual(report["latency_seconds"]["measured_valid_count"], 2)
        self.assertEqual(report["by_scene"]["room"]["markdown_recovered_count"], 1)
        self.assertEqual(report["by_scene"]["room"]["protocol"], LEVEL_PROTOCOL)

    def test_null_failure_missing_and_retry_are_not_recovered(self):
        truths = [dict(id=key, safety_level="IV") for key in "abcd"]
        rows = [dict(id="a", status="ok", raw_output='```json\n{"safety_level":null}\n```'),
                dict(id="b", status="timeout", raw_output='```json\n{"safety_level":"IV"}\n```'),
                dict(id="c", status="ok", attempt=2, raw_output='{"safety_level":"IV"}')]
        report = score_results(truths, rows, protocol=LEVEL_PROTOCOL)
        self.assertEqual(report["total"], 4)
        self.assertEqual(report["retry_count"], 1)
        self.assertEqual(report["protocol_valid_count"], 0)
        self.assertEqual(report["strict_valid_count"], 0)
        self.assertEqual(report["strict_correct_count"], 0)
        self.assertEqual(report["strict_accuracy"], 0)
        self.assertEqual(report["markdown_recovered_count"], 0)
        self.assertEqual(report["confusion_matrix"]["IV"]["INVALID"], 2)
        self.assertEqual(report["confusion_matrix"]["IV"]["MISSING"], 2)
        self.assertFalse(report["records"][1]["markdown_fence"])

    def test_recorded_protocol_must_match_scoring_protocol(self):
        truth = [dict(id="a", safety_level="IV")]
        tagged = dict(id="a", status="ok", raw_output='{"safety_level":"IV"}', output_protocol=LEVEL_PROTOCOL)
        with self.assertRaisesRegex(ValueError, "output protocol"):
            score_results(truth, [tagged])
        self.assertEqual(score_results(truth, [tagged], protocol=LEVEL_PROTOCOL)["accuracy"], 1)
        tagged["attempt"] = 2
        with self.assertRaisesRegex(ValueError, "output protocol"):
            score_results(truth, [tagged])

    def test_strict_accuracy_excludes_wrong_valid_level(self):
        report = score_results([dict(id="a", safety_level="I")],
                               [dict(id="a", status="ok", raw_output='{"safety_level":"IV"}')],
                               protocol=LEVEL_PROTOCOL)
        self.assertEqual(report["strict_valid_count"], 1)
        self.assertEqual(report["strict_correct_count"], 0)
        self.assertEqual(report["strict_accuracy"], 0)

    def test_legacy_joint_output_not_accepted_by_level_protocol(self):
        report = score_results([dict(id="a", safety_level="IV")], [row("a")], protocol=LEVEL_PROTOCOL)
        self.assertEqual(report["accuracy"], 0)
        self.assertEqual(report["protocol_valid_count"], 0)


class CompletionDenominatorTest(unittest.TestCase):
    def test_normal_level_only_is_incomplete(self):
        report = score_results([{"id": "n", "safety_level": "IV"}],
                               [{"id": "n", "status": "ok", "raw_output": '{"safety_level":"IV","description":""}'}])
        self.assertEqual(report["accuracy"], 1)
        self.assertEqual(report["joint_valid_rate"], 0)
        self.assertEqual(report["normal_incomplete_rate"], 1)


if __name__ == "__main__":
    unittest.main()

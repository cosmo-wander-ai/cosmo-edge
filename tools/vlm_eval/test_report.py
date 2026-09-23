import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.vlm_eval.parser import JOINT_PROTOCOL, LEVEL_PROTOCOL
from tools.vlm_eval.report import main


class ReportProtocolTests(unittest.TestCase):
    def test_cli_protocol_reaches_all_views_and_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            truth, results, output, conflicts = [root / name for name in ("truth.jsonl", "results.jsonl", "report.json", "conflicts.json")]
            truth.write_text(json.dumps(dict(id="a", safety_level="IV", scene="room", primary_heldout_eligible=True)) + "\n")
            raw = '```json\n{"safety_level":"IV"}\n```'
            results.write_text(json.dumps(dict(id="a", status="ok", raw_output=raw)) + "\n")
            conflicts.write_text('{"conflict_ids":[]}')
            arguments = ["report", "--truth", str(truth), "--results", str(results), "--output", str(output), "--conflicts", str(conflicts)]
            with patch("sys.argv", arguments + ["--protocol", LEVEL_PROTOCOL]):
                main()
            report = json.loads(output.read_text())
            self.assertEqual(report["provenance"]["protocol"], LEVEL_PROTOCOL)
            for view in [report, report["by_scene"]["room"], *report["views"].values()]:
                self.assertEqual(view["protocol"], LEVEL_PROTOCOL)
                self.assertEqual(view["accuracy"], 1)
                self.assertEqual(view["strict_valid_count"], 0)
                self.assertEqual(view["strict_correct_count"], 0)
                self.assertEqual(view["strict_accuracy"], 0)
                self.assertEqual(view["markdown_recovered_count"], 1)
            self.assertEqual(json.loads(results.read_text())["raw_output"], raw)
            with patch("sys.argv", arguments):
                main()
            legacy = json.loads(output.read_text())
            self.assertEqual(legacy["protocol"], JOINT_PROTOCOL)
            self.assertEqual(legacy["version"], "inspecsafe-scoring-v1")
            self.assertEqual(legacy["accuracy"], 0)

    def test_level_protocol_rejects_description_scores(self):
        with patch("sys.argv", ["report", "--truth", "unused", "--results", "unused", "--output", "unused",
                                 "--protocol", LEVEL_PROTOCOL, "--description-scores", "unused"]):
            with self.assertRaises(SystemExit) as raised:
                main()
        self.assertEqual(raised.exception.code, 2)


class ReportViewsTest(unittest.TestCase):
    def test_sensitivity_does_not_replace_full_or_independent_denominators(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            truths = [{'id': 'a', 'safety_level': 'I', 'primary_heldout_eligible': True},
                      {'id': 'b', 'safety_level': 'IV', 'primary_heldout_eligible': False}]
            results = [{'id': 'b', 'status': 'ok', 'raw_output': '{"description":"Scene","safety_level":"IV"}'}]
            for name, rows in [('truth', truths), ('results', results)]:
                (root / name).write_text(''.join(json.dumps(row)+'\n' for row in rows))
            (root / 'conflicts').write_text(json.dumps({'conflict_ids': ['a']}))
            subprocess.run([sys.executable, '-m', 'tools.vlm_eval.report', '--truth', str(root/'truth'),
                            '--results', str(root/'results'), '--output', str(root/'report'),
                            '--conflicts', str(root/'conflicts')], check=True)
            report = json.loads((root/'report').read_text())
            self.assertEqual(report['total'], 2)
            self.assertEqual(report['accuracy'], .5)
            self.assertEqual(report['views']['primary_independent']['total'], 1)
            self.assertEqual(report['views']['primary_independent']['accuracy'], 0)
            self.assertEqual(report['views']['sensitivity_excluding_predeclared_conflicts']['accuracy'], 1)
            self.assertEqual(report['failures']['missing'], 1)



if __name__ == "__main__":
    unittest.main()

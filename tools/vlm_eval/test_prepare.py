import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from tools.vlm_eval.prepare import prepare


class PrepareTests(unittest.TestCase):
    def fixture(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        digest = hashlib.sha256(b'image').hexdigest()
        full = [dict(asset_id='sample1', image_sha256=digest, group_id='g1', primary_heldout_eligible=True, assigned_to_calibration=False)]
        source = [dict(asset_id='sample1', sha256=digest, member='test/private.jpg')]
        annotations = [dict(asset_id='sample1', level_candidate='III', reference_text='A scene.', source_member_stem='road-private', image=dict(width=400, height=200))]
        args = SimpleNamespace(full=str(root/'full'), source_map=str(root/'source'), annotations=str(root/'annotations'),
                               output=str(root/'out'), archive=str(root/'archive'), extract=False, seed=1, performance_count=100)
        return args, full, source, annotations

    def execute(self, args, full, source, annotations):
        for path, rows in ((args.full, full), (args.source_map, source), (args.annotations, annotations)):
            Path(path).write_text(''.join(json.dumps(row)+'\n' for row in rows))
        prepare(args)

    def test_neutral_model_view_and_pending_review(self):
        args, full, source, annotations = self.fixture()
        self.execute(args, full, source, annotations)
        request = json.loads((Path(args.output)/'requests.full.jsonl').read_text())
        self.assertEqual(set(request), {'id', 'image', 'image_sha256'})
        self.assertNotIn('private', request['image'])
        truth = json.loads((Path(args.output)/'truth.full.jsonl').read_text())
        self.assertEqual(truth['semantic_review'], 'pending')
        self.assertFalse(json.loads((Path(args.output)/'manifest.json').read_text())['formal_evaluation_ready'])
        self.assertEqual((Path(args.output)/'requests.independent.jsonl').read_text(), (Path(args.output)/'requests.full.jsonl').read_text())

    def test_duplicate_mapping_rejected(self):
        args, full, source, annotations = self.fixture()
        with self.assertRaisesRegex(ValueError, 'duplicate source'):
            self.execute(args, full, source*2, annotations)

    def test_calibration_group_leakage_rejected(self):
        args, full, source, annotations = self.fixture()
        full.append(dict(full[0], asset_id='sample2', assigned_to_calibration=True, primary_heldout_eligible=False))
        with self.assertRaisesRegex(ValueError, 'leaks'):
            self.execute(args, full, source, annotations)

    def test_invalid_truth_rejected(self):
        args, full, source, annotations = self.fixture()
        annotations[0]['level_candidate'] = 'V'
        with self.assertRaisesRegex(ValueError, 'invalid reference'):
            self.execute(args, full, source, annotations)


if __name__ == '__main__':
    unittest.main()

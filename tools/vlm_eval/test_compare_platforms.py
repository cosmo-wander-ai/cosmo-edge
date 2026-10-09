import json
import unittest
from unittest.mock import patch
from tools.vlm_eval.compare_platforms import compare


def row(key, level='IV', attempt=1):
    return dict(id=key, attempt=attempt, status='ok', raw_output=json.dumps(dict(description='A scene',safety_level=level)))


class PairedBootstrapTests(unittest.TestCase):
    def truths(self):
        return [dict(id='a',safety_level='I',group_id='g1',primary_heldout_eligible=True),
                dict(id='b',safety_level='IV',group_id='g1',primary_heldout_eligible=True),
                dict(id='c',safety_level='IV',group_id='g2',primary_heldout_eligible=True),
                dict(id='cal',safety_level='IV',group_id='g3',primary_heldout_eligible=False)]

    def test_missing_and_retry_keep_pair_denominator(self):
        report = compare(self.truths(),[row('a','I'),row('b')],[row('a','I'),row('c'),row('b',attempt=2)],50,7)
        self.assertEqual(report['primary_sample_count'],3)
        self.assertEqual(report['missing_first_attempts'],{'a':1,'b':1})
        self.assertEqual(report['metrics']['accuracy']['platform_a'],2/3)
        self.assertEqual(report['metrics']['accuracy']['platform_b'],2/3)

    def test_identical_results_have_paired_zero_interval(self):
        results=[row('a','I'),row('b'),row('c')]
        report=compare(self.truths(),results,results,50,7)
        self.assertEqual(report['metrics']['accuracy']['percentile_95_interval'],[0,0])
        self.assertEqual(report,compare(self.truths(),results,results,50,7))

    def test_entire_source_cluster_is_repeated_for_both_platforms(self):
        with patch('tools.vlm_eval.compare_platforms.random.Random') as random_class:
            random_class.return_value.choices.return_value = ['g1','g1']
            report = compare(self.truths(), [row('a','I'),row('b')], [row('c')], 4, 7)
        self.assertEqual(report['metrics']['accuracy']['percentile_95_interval'],[-1,-1])
        self.assertEqual(random_class.return_value.choices.call_count,4)

    def test_sparse_class_absent_draws_disclosed_even_when_macro_f1_defined(self):
        truths = [dict(id='rare', safety_level='III', group_id='rare-group', primary_heldout_eligible=True),
                  dict(id='normal', safety_level='IV', group_id='normal-group', primary_heldout_eligible=True)]
        results = [row('rare','III'), row('normal')]
        with patch('tools.vlm_eval.compare_platforms.random.Random') as random_class:
            random_class.return_value.choices.side_effect = [
                ['normal-group','normal-group'], ['rare-group','normal-group']]
            report = compare(truths,results,results,2,7)
        self.assertEqual(report['absent_bootstrap_replicates_by_class'],{'I':2,'II':2,'III':1,'IV':0})
        self.assertEqual(report['metrics']['macro_f1']['defined_bootstrap_replicates'],2)
        self.assertEqual(report['metrics']['macro_f1']['platform_a'],1)

    def test_unknown_id_and_duplicate_attempt_rejected(self):
        for results in ([row('unknown')],[row('a'),row('a')]):
            with self.assertRaises(ValueError):
                compare(self.truths(),results,[],10)

    def test_single_source_group_interval_is_na(self):
        report=compare(self.truths()[:2],[row('a','I')],[],10)
        self.assertIsNone(report['metrics']['accuracy']['percentile_95_interval'])
        self.assertEqual(report['source_group_count'],1)

    def test_missing_group_rejected(self):
        truths=self.truths(); truths[0].pop('group_id')
        with self.assertRaisesRegex(ValueError,'group_id'):
            compare(truths,[],[],10)


if __name__ == '__main__':
    unittest.main()

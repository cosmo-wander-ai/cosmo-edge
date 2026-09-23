import copy
import unittest

from tools.vlm_eval.summarize_performance import summarize, summarize_memory
from tools.vlm_eval.parser import JOINT_PROTOCOL, LEVEL_PROTOCOL


class PerformanceSummaryTests(unittest.TestCase):
    def inputs(self):
        manifest = [dict(id='a'), dict(id='b')]
        result = [dict(id='a', status='ok', request_attempted=True, raw_output='{"description":"A scene","safety_level":"IV"}', elapsed_seconds=2),
                  dict(id='b', status='timeout', request_attempted=True, raw_output='', elapsed_seconds=1)]
        meta = dict(manifest_sha256='frozen', requests=2, prompt_sha256='prompt', config_sha256='config',
                    execution_identity={'worker':'worker'}, joint_valid_count=1,
                    measurement_window_seconds=4, effective_throughput=.25,
                    memory={'status':'sampled','peak_rss_kib':100}, cold_start_seconds=[1, 1], restarts=1,
                    excluded_startup_and_warmup_seconds=1, included_recovery_setup_seconds=1,
                    setup_timings=[dict(initial=True, seconds=1, excluded_from_measurement=True),
                                   dict(initial=False, seconds=1, excluded_from_measurement=False)],
                    exit_code=0, terminal_reason=None, unconfirmed_processes=[])
        return manifest, [(copy.deepcopy(result), copy.deepcopy(meta)) for _ in range(3)]

    def test_memory_errors_remain_explicit(self):
        rows = [dict(event='start'), dict(event='error', error='unavailable'), dict(event='stop')]
        report = summarize_memory(rows)
        self.assertEqual(report['status'], 'incomplete_or_errors')
        self.assertIsNone(report['peak_used'])
        self.assertEqual(report['errors'], 1)

    def test_all_rounds_and_failure_denominator(self):
        manifest, rounds = self.inputs()
        report = summarize(manifest, rounds, 'frozen')
        self.assertEqual(len(report['rounds']), 3)
        self.assertEqual(report['frozen_unique_request_count'], 2)
        self.assertEqual(report['rounds'][0]['failed'], 1)
        self.assertEqual(report['throughput_across_rounds']['mean'], .25)
        self.assertEqual(report['sampled_worker_peak_rss_kib']['sampled_rounds'], 3)

    def test_incomplete_round_never_becomes_complete_throughput(self):
        manifest, rounds = self.inputs()
        rounds[1][0][1].update(status='not_run_recovery_failed', request_attempted=False, phase='not_run')
        rounds[1][1].update(exit_code=2, terminal_reason='warmup_failed')
        report = summarize(manifest, rounds, 'frozen')
        self.assertEqual(report['rounds'][1]['status'], 'incomplete')
        self.assertEqual(report['rounds'][1]['unexecuted'], 1)
        self.assertIsNone(report['rounds'][1]['effective_throughput'])
        self.assertIsNone(report['throughput_across_rounds']['mean'])

    def test_preparation_failure_is_in_all_attempt_latency(self):
        manifest, rounds = self.inputs()
        rounds[0][0][1].update(status='inference_error', request_attempted=False,
                              phase='prepare', elapsed_seconds=1)
        report = summarize(manifest, rounds, 'frozen')
        self.assertEqual(report['rounds'][0]['status'], 'complete')
        self.assertEqual(report['rounds'][0]['all_attempt_latency_seconds']['attempted'], 2)
        self.assertEqual(report['rounds'][0]['all_attempt_latency_seconds']['p50'], 1.5)

    def test_old_recovery_window_without_setup_evidence_is_rejected(self):
        manifest, rounds = self.inputs()
        rounds[0][1].pop('setup_timings')
        with self.assertRaisesRegex(ValueError, 'recovery setup timing'):
            summarize(manifest, rounds, 'frozen')

    def test_old_zero_recovery_window_remains_usable(self):
        manifest, rounds = self.inputs()
        for _, meta in rounds:
            meta.update(restarts=0, cold_start_seconds=[1])
            meta.pop('setup_timings')
            meta.pop('included_recovery_setup_seconds')
        self.assertEqual(summarize(manifest, rounds, 'frozen')['throughput_across_rounds']['mean'], .25)

    def test_raw_count_mismatch_rejected(self):
        manifest, rounds = self.inputs()
        rounds[1][1]['joint_valid_count'] = 2
        with self.assertRaisesRegex(ValueError, 'valid count'):
            summarize(manifest, rounds, 'frozen')

    def test_condition_change_rejected(self):
        manifest, rounds = self.inputs()
        rounds[1][1]['config_sha256'] = 'changed'
        with self.assertRaisesRegex(ValueError, 'conditions differ'):
            summarize(manifest, rounds, 'frozen')

    def test_level_protocol_uses_matching_parser(self):
        manifest, rounds = self.inputs()
        for rows, meta in rounds:
            meta['output_protocol'] = LEVEL_PROTOCOL
            rows[0]['raw_output'] = '```json\n{"safety_level":"IV"}\n```'
            rows[0]['output_protocol'] = LEVEL_PROTOCOL
        self.assertEqual(summarize(manifest, rounds, 'frozen')['rounds'][0]['joint_valid'], 1)
        rounds[1][1]['output_protocol'] = JOINT_PROTOCOL
        with self.assertRaisesRegex(ValueError, 'conditions differ'):
            summarize(manifest, rounds, 'frozen')

    def test_missing_round_not_fabricated(self):
        manifest, rounds = self.inputs()
        with self.assertRaisesRegex(ValueError, 'three'):
            summarize(manifest, rounds[:2], 'frozen')

    def test_missing_memory_remains_unavailable(self):
        manifest, rounds = self.inputs()
        for _, meta in rounds:
            meta['memory'] = {'status':'unavailable'}
        self.assertIsNone(summarize(manifest, rounds, 'frozen')['sampled_worker_peak_rss_kib']['maximum'])


if __name__ == '__main__':
    unittest.main()

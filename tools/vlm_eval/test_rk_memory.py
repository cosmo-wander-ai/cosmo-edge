import tempfile
from pathlib import Path
import unittest

from tools.vlm_eval.rk_memory import Sampler, parse_kib, summarize


class RkMemoryTests(unittest.TestCase):
    def fixture(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        (root/'meminfo').write_text('MemTotal: 4096 kB\nMemAvailable: 1024 kB\nShmem: 80 kB\n')
        (root/'pid').write_text('42')
        process = root/'42'; process.mkdir()
        (process/'stat').write_text('42 (worker with ) space) ' + ' '.join(['S']+['0']*18+['12345']))
        (process/'status').write_text('Name: worker\nVmRSS: 512 kB\nVmHWM: 600 kB\n')
        return root

    def test_reports_distinct_scopes_without_npu_claim(self):
        root = self.fixture()
        row = Sampler(root, root/'pid').sample()
        self.assertEqual(row['system_unavailable_kib'], 3072)
        self.assertEqual(row['worker']['rss_kib'], 512)
        self.assertEqual(row['worker']['start_ticks'], 12345)
        self.assertIsNone(row['system']['CmaTotal'])
        self.assertIsNone(row['npu_allocated_memory']['value'])
        report = summarize([dict(event='sample', **row), dict(event='stop')])
        self.assertEqual(report['sampled_worker_peak_rss_kib'], 512)
        self.assertEqual(report['peak_system_unavailable_kib'], 3072)

    def test_missing_process_preserves_system_sample_and_null_rss(self):
        root = self.fixture(); (root/'42/status').unlink()
        row = Sampler(root, root/'pid').sample()
        self.assertEqual(row['worker']['status'], 'unavailable')
        self.assertIsNone(row['worker']['rss_kib'])
        self.assertEqual(row['system']['MemAvailable'], 1024)
        report = summarize([dict(event='sample', **row), dict(event='stop')])
        self.assertEqual(report['status'], 'incomplete_or_errors')
        self.assertEqual(report['system_status'], 'complete')
        self.assertEqual(report['worker_status'], 'partial_or_unavailable')

    def test_restarted_worker_has_new_identity(self):
        root = self.fixture(); sampler = Sampler(root, root/'pid')
        a = sampler.sample()
        path = root/'42/stat'; path.write_text(path.read_text().replace('12345', '45678'))
        b = sampler.sample()
        report = summarize([dict(event='sample', **a), dict(event='sample', **b), dict(event='stop')])
        self.assertEqual(report['worker_identities'], [(42, 12345), (42, 45678)])

    def test_missing_or_invalid_system_measurements_rejected(self):
        for text in ('MemTotal: 12 kB', 'MemTotal: -1 kB\nMemAvailable: 0 kB'):
            with self.assertRaises(ValueError):
                parse_kib(text, ('MemTotal', 'MemAvailable'))
        root = self.fixture(); (root/'meminfo').write_text('MemTotal: 1 kB\nMemAvailable: 2 kB')
        with self.assertRaises(ValueError):
            Sampler(root).sample()

    def test_no_samples_or_no_stop_not_success(self):
        self.assertEqual(summarize([])['status'], 'incomplete_or_errors')
        row = Sampler(self.fixture()).sample()
        self.assertEqual(summarize([dict(event='sample', **row)])['status'], 'incomplete_or_errors')
        self.assertIsNone(summarize([dict(event='sample', **row)])['sampled_worker_peak_rss_kib'])


if __name__ == '__main__':
    unittest.main()

import ctypes
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import threading
import unittest

from tools.vlm_eval.sophon_memory import DeviceStat, Sampler, collect, decode_stat


class SophonMemoryTests(unittest.TestCase):
    def api(self, status=0):
        freed = []
        def request(pointer, device):
            ctypes.cast(pointer, ctypes.POINTER(ctypes.c_void_p))[0] = ctypes.c_void_p(123)
            return 0
        def get(handle, pointer):
            stat = ctypes.cast(pointer, ctypes.POINTER(DeviceStat)).contents
            stat.mem_total = 100
            stat.mem_used = 30
            stat.tpu_util = 20
            stat.heap_num = 1
            stat.heap_stat[0].mem_total = 100
            stat.heap_stat[0].mem_used = 30
            stat.heap_stat[0].mem_avail = 70
            return status
        return SimpleNamespace(bm_dev_request=request, bm_get_stat=get, bm_dev_free=lambda h: freed.append(h.value)), freed

    def test_abi_and_decode_and_cleanup(self):
        self.assertEqual(ctypes.sizeof(DeviceStat), 64)
        api, freed = self.api()
        with Sampler(api=api) as sampler:
            self.assertEqual(sampler.sample()['heaps'][0]['available'], 70)
        sampler.close()
        self.assertEqual(freed, [123])

    def test_stat_failure_still_releases(self):
        api, freed = self.api(status=-1)
        with self.assertRaisesRegex(RuntimeError, 'bm_get_stat'):
            with Sampler(api=api) as sampler:
                sampler.sample()
        self.assertEqual(freed, [123])

    def test_invalid_data_rejected_without_clamping(self):
        stat = DeviceStat(); stat.heap_num = 5
        with self.assertRaisesRegex(ValueError, 'heap count'):
            decode_stat(stat)
        stat.heap_num = 1; stat.heap_stat[0].mem_avail = 1
        with self.assertRaisesRegex(ValueError, 'heap memory'):
            decode_stat(stat)

    def test_existing_stop_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(interval=.01, stop_file=directory+'/stop', output=directory+'/memory', library='fake', device=0)
            Path(args.stop_file).touch()
            with self.assertRaisesRegex(ValueError, 'already exists'):
                collect(args, threading.Event())
            self.assertFalse(Path(args.output).exists())

    def test_open_error_logged_and_stop_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(interval=.01, stop_file=directory+'/stop', output=directory+'/memory', library='fake', device=0)
            def failing(*args):
                raise RuntimeError('constructed open failure')
            self.assertEqual(collect(args, threading.Event(), failing), 2)
            rows = [json.loads(line) for line in Path(args.output).read_text().splitlines()]
            self.assertEqual([row['event'] for row in rows], ['start','error','stop'])
            self.assertFalse(rows[-1]['handle_released'])


if __name__ == '__main__':
    unittest.main()

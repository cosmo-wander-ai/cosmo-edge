"""Read-only whole-device bmlib memory sampling, independent of inference/RSS."""
import argparse
import ctypes
import json
import math
from pathlib import Path
import signal
import threading
import time


class HeapStat(ctypes.Structure):
    _fields_ = [('mem_total', ctypes.c_uint), ('mem_avail', ctypes.c_uint), ('mem_used', ctypes.c_uint)]


class DeviceStat(ctypes.Structure):
    _fields_ = [('mem_total', ctypes.c_int), ('mem_used', ctypes.c_int), ('tpu_util', ctypes.c_int),
                ('heap_num', ctypes.c_int), ('heap_stat', HeapStat * 4)]


def decode_stat(stat):
    if not 0 <= stat.heap_num <= 4:
        raise ValueError('invalid heap count')
    if stat.mem_total < 0 or not 0 <= stat.mem_used <= stat.mem_total or not 0 <= stat.tpu_util <= 100:
        raise ValueError('invalid device totals/utilization')
    heaps = []
    for index in range(stat.heap_num):
        heap = stat.heap_stat[index]
        if heap.mem_avail > heap.mem_total or heap.mem_used > heap.mem_total:
            raise ValueError('invalid heap memory bounds')
        heaps.append(dict(index=index, total=heap.mem_total, used=heap.mem_used, available=heap.mem_avail))
    # Do not force sum(heaps)==device totals: runtime accounting can differ.
    return dict(total=stat.mem_total, used=stat.mem_used, available=sum(h['available'] for h in heaps),
                available_source='sum of reported heap available', tpu_util_percent=stat.tpu_util, heaps=heaps,
                unit='runtime-reported MB', scope='whole-device bmlib heaps; includes other processes; not additive with RSS')


class Sampler:
    def __init__(self, library='libbmlib.so.0', device=0, api=None):
        self.api = api if api is not None else ctypes.CDLL(library)
        self.api.bm_dev_request.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_int]
        self.api.bm_dev_request.restype = ctypes.c_int
        self.api.bm_get_stat.argtypes = [ctypes.c_void_p, ctypes.POINTER(DeviceStat)]
        self.api.bm_get_stat.restype = ctypes.c_int
        self.api.bm_dev_free.argtypes = [ctypes.c_void_p]
        self.api.bm_dev_free.restype = None
        self.handle = ctypes.c_void_p()
        status = self.api.bm_dev_request(ctypes.byref(self.handle), device)
        if status != 0 or not self.handle.value:
            raise RuntimeError('bm_dev_request failed: ' + str(status))

    def sample(self):
        if not self.handle.value:
            raise RuntimeError('sampler is closed')
        stat = DeviceStat()
        status = self.api.bm_get_stat(self.handle, ctypes.byref(stat))
        if status != 0:
            raise RuntimeError('bm_get_stat failed: ' + str(status))
        return decode_stat(stat)

    def close(self):
        if self.handle.value:
            self.api.bm_dev_free(self.handle)
            self.handle = ctypes.c_void_p()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def collect(args, stopped, factory=Sampler):
    if not math.isfinite(args.interval) or args.interval <= 0:
        raise ValueError('sampling interval must be finite and positive')
    if Path(args.stop_file).exists():
        raise ValueError('stop file already exists; use a fresh run path')
    with Path(args.output).open('x') as stream:
        def emit(event, **fields):
            stream.write(json.dumps(dict(event=event, monotonic_seconds=time.monotonic(),
                                         unix_seconds=time.time(), **fields), allow_nan=False)+'\n')
            stream.flush()
        emit('start', interval_seconds=args.interval, device=args.device, library=args.library,
             abi='libsophon-0.4.11 bm_dev_stat_t; sizeof=' + str(ctypes.sizeof(DeviceStat)),
             scope='whole-device heaps; not process-exclusive; do not add to RSS',
             sampling_window='entire sampler lifetime, which may include startup/warmup/recovery')
        sampler = None
        errors = samples = 0
        try:
            sampler = factory(args.library, args.device)
            while not stopped.is_set() and not Path(args.stop_file).exists():
                try:
                    emit('sample', **sampler.sample())
                    samples += 1
                except Exception as error:
                    emit('error', error_type=type(error).__name__, error=str(error))
                    errors += 1
                stopped.wait(args.interval)
        except Exception as error:
            emit('error', error_type=type(error).__name__, error=str(error))
            errors += 1
        finally:
            if sampler is not None:
                sampler.close()
            emit('stop', samples=samples, errors=errors, handle_released=sampler is not None)
    return 0 if samples and not errors else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interval', type=float, default=.2)
    parser.add_argument('--output', required=True)
    parser.add_argument('--stop-file', required=True)
    parser.add_argument('--device', type=int, default=0)
    parser.add_argument('--library', default='libbmlib.so.0')
    args = parser.parse_args()
    stopped = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stopped.set())
    raise SystemExit(collect(args, stopped))


if __name__ == '__main__':
    main()

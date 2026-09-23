"""Read-only Linux shared-memory observations for RK devices; no NPU allocation claims."""
import argparse
import json
import math
from pathlib import Path
import signal
import threading
import time


def parse_kib(text, required=()):
    values = {}
    for line in text.splitlines():
        key, separator, value = line.partition(':')
        if not separator:
            continue
        parts = value.split()
        if len(parts) == 2 and parts[1] == 'kB':
            if not parts[0].isdigit():
                raise ValueError('invalid nonnegative memory value: ' + key)
            values[key] = int(parts[0])
    if any(key not in values for key in required):
        raise ValueError('missing required memory fields')
    return values


def process_start_ticks(text):
    # A comm field can contain spaces and parentheses; fields after its final ')'
    # start with field 3 (state). Linux starttime is field 22.
    tail = text.rsplit(')', 1)[1].split()
    return int(tail[19])


class Sampler:
    def __init__(self, proc_root='/proc', worker_pid_file=None):
        self.proc = Path(proc_root)
        self.worker_pid_file = Path(worker_pid_file) if worker_pid_file else None

    def sample(self):
        mem = parse_kib((self.proc / 'meminfo').read_text(), ('MemTotal', 'MemAvailable'))
        if mem['MemAvailable'] > mem['MemTotal']:
            raise ValueError('MemAvailable exceeds MemTotal')
        result = {'unit': 'KiB', 'system': {key: mem.get(key) for key in
                  ('MemTotal', 'MemAvailable', 'MemFree', 'Buffers', 'Cached', 'SReclaimable',
                   'Shmem', 'CmaTotal', 'CmaFree', 'SwapTotal', 'SwapFree')},
                  'system_unavailable_kib': mem['MemTotal'] - mem['MemAvailable'],
                  'worker': {'status': 'not_configured', 'rss_kib': None},
                  'npu_allocated_memory': {'status': 'unavailable', 'value': None}}
        if self.worker_pid_file is None:
            return result
        try:
            pid = int(self.worker_pid_file.read_text().strip())
            if pid <= 0:
                raise ValueError('worker PID must be positive')
            directory = self.proc / str(pid)
            before = process_start_ticks((directory / 'stat').read_text())
            status = parse_kib((directory / 'status').read_text(), ('VmRSS',))
            after = process_start_ticks((directory / 'stat').read_text())
            if before != after:
                raise ValueError('worker PID changed identity during sample')
            result['worker'] = {'status': 'sampled', 'pid': pid, 'start_ticks': before,
                                'rss_kib': status['VmRSS'], 'hwm_kib': status.get('VmHWM')}
        except (OSError, ValueError, IndexError) as error:
            result['worker'] = {'status': 'unavailable', 'rss_kib': None,
                                'error': type(error).__name__ + ': ' + str(error)}
        return result


def summarize(rows):
    samples = [r for r in rows if r.get('event') == 'sample']
    errors = [r for r in rows if r.get('event') == 'error']
    workers = [r['worker'] for r in samples if r['worker']['status'] == 'sampled']
    stopped = any(r.get('event') == 'stop' for r in rows)
    configured = any(r['worker']['status'] != 'not_configured' for r in samples)
    worker_status = ('complete' if workers and len(workers) == len(samples) else 'partial_or_unavailable') if configured else 'not_configured'
    return {'status': 'complete' if samples and stopped and not errors and worker_status != 'partial_or_unavailable' else 'incomplete_or_errors',
            'system_status': 'complete' if samples and stopped and not errors else 'incomplete_or_errors',
            'worker_status': worker_status,
            'unit': 'KiB', 'samples': len(samples), 'system_errors': len(errors),
            'minimum_system_available_kib': min((r['system']['MemAvailable'] for r in samples), default=None),
            'peak_system_unavailable_kib': max((r['system_unavailable_kib'] for r in samples), default=None),
            'worker_samples': len(workers), 'worker_unavailable_samples': len(samples) - len(workers),
            'sampled_worker_peak_rss_kib': max((r['rss_kib'] for r in workers), default=None),
            'worker_identities': sorted({(r['pid'], r['start_ticks']) for r in workers}),
            'npu_allocated_memory': {'status': 'unavailable', 'value': None},
            'scope': 'System MemTotal-MemAvailable is an unavailable-memory estimate including all processes and kernel; worker VmRSS is only the named local process. Do not add them. Neither measures NPU allocations, DMA-BUF totals, children, or remote memory.',
            'sampling_window': 'whole sampler lifetime; may include cold start, warmup, recovery and other workloads'}


def collect(args, stopped, factory=Sampler):
    if not math.isfinite(args.interval) or args.interval <= 0:
        raise ValueError('sampling interval must be positive and finite')
    if Path(args.stop_file).exists():
        raise ValueError('stop file already exists')
    sampler = factory(worker_pid_file=args.worker_pid_file)
    rows = []
    with Path(args.output).open('x') as stream:
        def emit(event, **fields):
            row = dict(event=event, monotonic_seconds=time.monotonic(), unix_seconds=time.time(), **fields)
            rows.append(row)
            stream.write(json.dumps(row, allow_nan=False) + '\n')
            stream.flush()
        emit('start', version='rk-linux-shared-memory-v1', interval_seconds=args.interval,
             worker_pid_file=args.worker_pid_file, npu_memory_available=False)
        while not stopped.is_set() and not Path(args.stop_file).exists():
            try:
                emit('sample', **sampler.sample())
            except Exception as error:
                emit('error', error_type=type(error).__name__, error=str(error))
            stopped.wait(args.interval)
        emit('stop')
    report = summarize(rows)
    if args.summary_output:
        with Path(args.summary_output).open('x') as stream:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write('\n')
    return 0 if report['status'] == 'complete' else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interval', type=float, default=.2)
    parser.add_argument('--output', required=True)
    parser.add_argument('--summary-output')
    parser.add_argument('--stop-file', required=True)
    parser.add_argument('--worker-pid-file', help='Caller-maintained file containing the actual worker PID; update atomically after restart')
    args = parser.parse_args()
    stopped = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stopped.set())
    raise SystemExit(collect(args, stopped))


if __name__ == '__main__':
    main()

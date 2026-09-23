"""Serial JSONL device-process supervisor with enforced timeout recovery."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import signal
import shutil
import subprocess
import threading
import time
import math

from .parser import JOINT_PROTOCOL, PROTOCOLS, parse_output


class TerminationUnconfirmed(RuntimeError):
    def __init__(self, pid, error):
        super().__init__(f'worker {pid} exit unconfirmed: {error}')
        self.pid = pid


class Worker:
    def __init__(self, command, log, startup_timeout, expected_backend=None):
        self.messages = queue.Queue()
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=log, text=True, bufsize=1, start_new_session=True)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.peak_rss_kib = 0
        self.rss_samples = 0
        self.sampling_stop = threading.Event()
        self.sampler = threading.Thread(target=self._sample_rss, daemon=True)
        self.sampler.start()
        try:
            ready = self.receive(startup_timeout)
            if ready.get('type') != 'ready':
                raise RuntimeError('worker did not emit ready')
            self.effective_config = ready.get('effective_config', {})
            if expected_backend is not None and (not isinstance(self.effective_config, dict) or
                    self.effective_config.get('backend') != expected_backend):
                raise RuntimeError('ready backend does not match frozen worker config')
        except BaseException:
            try:
                self.close()
            except Exception as error:
                raise TerminationUnconfirmed(self.process.pid, error) from error
            raise

    def _sample_rss(self):
        while not self.sampling_stop.is_set():
            try:
                for line in Path(f'/proc/{self.process.pid}/status').read_text().splitlines():
                    if line.startswith('VmRSS:'):
                        self.peak_rss_kib = max(self.peak_rss_kib, int(line.split()[1]))
                        self.rss_samples += 1
            except (OSError, ValueError):
                pass
            self.sampling_stop.wait(.02)

    def _read(self):
        try:
            for line in self.process.stdout:
                self.messages.put(json.loads(line))
        except Exception as error:
            self.messages.put(error)
        finally:
            self.messages.put(EOFError('worker stdout closed'))

    def receive(self, timeout):
        try:
            value = self.messages.get(timeout=timeout)
        except queue.Empty as error:
            raise TimeoutError('worker deadline exceeded') from error
        if isinstance(value, Exception):
            raise value
        if not isinstance(value, dict):
            raise ValueError('worker response must be an object')
        return value

    def infer(self, request, timeout):
        self.process.stdin.write(json.dumps(request) + '\n')
        self.process.stdin.flush()
        result = self.receive(timeout)
        if result.get('type') != 'result' or result.get('id') != request['id']:
            raise ValueError('worker response type/id mismatch')
        return result

    def close(self):
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.process.wait(timeout=5)
        self.sampling_stop.set()
        self.sampler.join(timeout=2)
        self.reader.join(timeout=2)
        self.process.stdin.close()
        self.process.stdout.close()


def read_manifest(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip()]
    identifiers = [row['id'] for row in rows]
    if len(set(identifiers)) != len(rows):
        raise ValueError('duplicate request id')
    for row in rows:
        if set(row) - {'id', 'image', 'image_sha256'}:
            raise ValueError('request manifest contains fields outside the model-input allowlist')
    return rows


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def execution_identity(args):
    """Bind the standalone worker's file-based config and supervisor settings."""
    config = json.loads(Path(args.config).read_text())
    if not isinstance(config, dict):
        raise ValueError('worker config must be a JSON object')
    artifacts = {}
    backend = config.get('backend', 'sophon')
    if backend == 'rkllm':
        path = config.get('model_path')
        if not isinstance(path, str) or not path:
            raise ValueError('RKLLM model_path must name a model file or directory')
        configured = Path(path)
        model_dir = configured if configured.is_dir() else configured.parent
        language = configured if configured.suffix == '.rkllm' else model_dir / 'model.rkllm'
        for field, artifact in (('model_path', language), ('vision_path', model_dir / 'vision.rknn')):
            if not artifact.is_file():
                raise ValueError(f'RKLLM {field} must name a readable file')
            artifacts[field] = file_sha256(artifact)
    elif backend == 'sophon':
        for field in ('model_path', 'tokenizer_path'):
            path = config.get(field)
            if not isinstance(path, str) or not path or not Path(path).is_file():
                raise ValueError(f'worker config {field} must name a readable file')
            artifacts[field] = file_sha256(path)
    else:
        raise ValueError('unsupported worker backend')
    worker_path = shutil.which(args.worker)
    if worker_path is None:
        raise ValueError('worker executable not found')
    return {'timeout_seconds': args.timeout, 'startup_timeout_seconds': args.startup_timeout,
            'output_protocol': getattr(args, 'protocol', JOINT_PROTOCOL),
            'warmup_count': args.warmup, 'warmup_image_sha256': file_sha256(args.warmup_image),
            'worker_sha256': file_sha256(worker_path), 'artifact_sha256': artifacts,
            'supervisor_sha256': file_sha256(__file__),
            'parser_sha256': file_sha256(Path(__file__).with_name('parser.py'))}


def validate_readiness(args):
    if getattr(args, 'mode', 'engineering') != 'formal':
        return
    if not getattr(args, 'readiness', None):
        raise ValueError('formal evaluation requires a frozen readiness record')
    record = json.loads(Path(args.readiness).read_text())
    for gate in ('input_reference_verified', 'device_input_verified', 'artifact_verified',
                 'small_sample_verified', 'label_audit_complete', 'scorer_verified',
                 'parameters_frozen', 'timeout_recovery_verified'):
        if record.get(gate) is not True:
            raise ValueError(f'formal readiness gate is not true: {gate}')
    for field in ('manifest', 'prompt', 'config'):
        actual = file_sha256(getattr(args, field))
        if record.get(field + '_sha256') != actual:
            raise ValueError(f'formal readiness hash mismatch: {field}')
    identity = execution_identity(args)
    if record.get('execution_identity') != identity:
        raise ValueError('formal readiness execution_identity mismatch')
    return identity


def run(args):
    protocol = getattr(args, 'protocol', JOINT_PROTOCOL)
    if protocol not in PROTOCOLS:
        raise ValueError('unsupported output protocol')
    identity = validate_readiness(args)
    expected_backend = (json.loads(Path(args.config).read_text()).get('backend', 'sophon')
                        if getattr(args, 'mode', 'engineering') == 'formal' else None)
    rows = read_manifest(args.manifest)
    prompt = Path(args.prompt).read_text()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [args.worker, '--config', args.config]
    worker = None
    restarts = 0
    startup_attempts = 0
    phase = 'startup'
    cold_starts = []
    events = []
    memory_runs = []
    excluded_setup_seconds = 0.0
    recovery_setup_seconds = 0.0
    setup_timings = []
    valid_count = 0
    strict_valid_count = 0
    markdown_recovered_count = 0
    measurement_end = None
    terminal_reason = None
    unconfirmed_processes = []

    def record_setup(started, initial):
        nonlocal excluded_setup_seconds, recovery_setup_seconds
        duration = time.monotonic() - started
        if initial:
            excluded_setup_seconds += duration
        else:
            recovery_setup_seconds += duration
        setup_timings.append({'initial': initial, 'seconds': duration,
                              'excluded_from_measurement': initial})

    def close_worker(reason):
        nonlocal worker, terminal_reason
        if worker is None:
            return True
        pid = worker.process.pid
        try:
            worker.close()
        except Exception as error:
            terminal_reason = 'termination_unconfirmed'
            unconfirmed_processes.append({'pid': pid, 'error': str(error), 'reason': reason})
            events.append({'event': 'worker_exit_unconfirmed', 'pid': pid, 'error': str(error), 'reason': reason})
            # Never start another worker or retry close blindly after an unconfirmed exit.
            worker = None
            return False
        memory_runs.append({'pid': pid, 'peak_rss_kib': worker.peak_rss_kib,
                            'samples': worker.rss_samples})
        events.append({'event': 'worker_exit_confirmed', 'reason': reason,
                       'returncode': worker.process.returncode, 'pid': pid})
        worker = None
        return True
    window_start = time.monotonic()
    try:
        with output.open('x') as stream, output.with_suffix('.stderr.log').open('x') as log:
            def persist(result):
                nonlocal valid_count, strict_valid_count, markdown_recovered_count
                parsed = parse_output(result.get('raw_output'), protocol)
                result['output_protocol'] = protocol
                result['output_parse'] = parsed
                if result.get('status') == 'ok':
                    valid_count += parsed['protocol_valid']
                    strict_valid_count += parsed['strict_valid']
                    markdown_recovered_count += parsed['markdown_recovered']
                stream.write(json.dumps(result) + '\n')
                stream.flush()
                os.fsync(stream.fileno())

            def start_and_warm():
                nonlocal worker, phase, restarts, startup_attempts
                initial_setup = startup_attempts == 0
                if not initial_setup:
                    restarts += 1
                startup_attempts += 1
                phase = 'startup'
                events.append({'event': 'worker_start_attempt', 'initial': initial_setup})
                cold_start = time.monotonic()
                setup_start = cold_start
                try:
                    worker = Worker(command, log, args.startup_timeout, expected_backend)
                except Exception:
                    record_setup(setup_start, initial_setup)
                    raise
                cold_starts.append(time.monotonic() - cold_start)
                events.append({'event': 'worker_ready', 'pid': worker.process.pid})
                phase = 'warmup'
                try:
                    for index in range(args.warmup):
                        warm = worker.infer({'id': f'warmup-{index}', 'image': args.warmup_image,
                                             'prompt': prompt}, args.timeout)
                        events.append({'event': 'warmup_response', 'status': warm.get('status'),
                                       'raw_output': warm.get('raw_output'),
                                       'joint_valid': parse_output(warm.get('raw_output'), protocol)['joint_valid']})
                        if (warm.get('status') not in ('ok', 'empty_output') or
                                warm.get('process_restart_required') is True):
                            raise RuntimeError('recovery warmup failed')
                finally:
                    record_setup(setup_start, initial_setup)
                events.append({'event': 'warmup_complete', 'count': args.warmup})

            for row in rows:
                started = time.monotonic()
                phase = 'startup' if worker is None else 'prepare'
                request_attempted = False
                result = {'type': 'result', 'id': row['id'], 'attempt': 1, 'raw_output': ''}
                if terminal_reason:
                    result.update(status='termination_unconfirmed' if terminal_reason == 'termination_unconfirmed' else 'not_run_recovery_failed',
                                  phase='not_run', request_attempted=False, elapsed_seconds=0.0,
                                  terminal_reason=terminal_reason)
                    persist(result)
                    continue
                try:
                    if worker is None:
                        start_and_warm()
                        started = time.monotonic()
                    phase = 'prepare'
                    image = Path(row['image'])
                    payload = image.read_bytes()
                    if row.get('image_sha256') and hashlib.sha256(payload).hexdigest() != row['image_sha256']:
                        raise ValueError('image hash mismatch')
                    phase = 'request'
                    request_attempted = True
                    result.update(worker.infer({'id': row['id'], 'image': str(image), 'prompt': prompt}, args.timeout))
                    result['effective_config'] = worker.effective_config
                    if result.get('process_restart_required') is True:
                        result['status'] = 'inference_error'
                        raise RuntimeError('worker requires process restart before further inference')
                    if result.get('status') not in ('ok', 'empty_output'):
                        raise RuntimeError('worker reported failure: ' + str(result.get('status')))
                except Exception as error:
                    if isinstance(error, TerminationUnconfirmed):
                        terminal_reason = 'termination_unconfirmed'
                        unconfirmed_processes.append({'pid': error.pid, 'error': str(error), 'reason': phase})
                    elif phase in ('startup', 'warmup'):
                        terminal_reason = phase + '_failed'
                    status = ('startup_error' if phase == 'startup' else 'warmup_error') if phase in ('startup', 'warmup') else (
                        'timeout' if isinstance(error, TimeoutError) else (
                            result.get('status') if result.get('status') not in (None, 'ok') else 'inference_error'))
                    result.update(status=status, error=str(error), phase=phase,
                                  request_attempted=request_attempted, attempt=1)
                    # Persist the actual failure before stopping the device process.
                    result['elapsed_seconds'] = time.monotonic() - started
                    persist(result)
                    events.append({'event': 'failure_persisted', 'id': row['id'], 'status': result['status'], 'phase': phase})
                    if worker is not None:
                        close_worker('request_failure' if request_attempted else phase + '_failure')
                    continue
                result.update(phase='request', request_attempted=True, attempt=1)
                result['elapsed_seconds'] = time.monotonic() - started
                persist(result)
            # The last failed request still requires verified recovery, even without
            # another manifest row. Recovery must never create a synthetic quality row.
            if worker is None and startup_attempts and not terminal_reason:
                try:
                    start_and_warm()
                    events.append({'event': 'tail_recovery_complete'})
                except Exception as error:
                    if isinstance(error, TerminationUnconfirmed):
                        terminal_reason = 'termination_unconfirmed'
                        unconfirmed_processes.append({'pid': error.pid, 'error': str(error),
                                                      'reason': phase})
                    else:
                        terminal_reason = phase + '_failed'
                    events.append({'event': 'tail_recovery_failed', 'phase': phase,
                                   'error': str(error), 'terminal_reason': terminal_reason})
                    close_worker('tail_recovery_failure')
    finally:
        measurement_end = time.monotonic()
        close_worker('run_complete')
    metadata = {'requests': len(rows), 'restarts': restarts, 'cold_start_seconds': cold_starts,
                'cold_start_scope': 'fresh worker/model load; OS cache not flushed; identity hashing precedes timer',
                'wall_seconds_including_startup_and_recovery': time.monotonic() - window_start,
                'warmup_count_per_start': args.warmup, 'timeout_seconds': args.timeout,
                'cache_policy': 'no_explicit_os_cache_flush', 'concurrency': 1,
                'mode': getattr(args, 'mode', 'engineering'),
                'output_protocol': protocol,
                'execution_identity': identity,
                'events': events,
                'terminal_reason': terminal_reason,
                'unconfirmed_processes': unconfirmed_processes,
                'exit_code': 2 if terminal_reason else 0,
                'measurement_window_seconds': max(0, measurement_end - window_start - excluded_setup_seconds),
                'excluded_startup_and_warmup_seconds': excluded_setup_seconds,
                'included_recovery_setup_seconds': recovery_setup_seconds,
                'setup_timings': setup_timings,
                'joint_valid_count': valid_count,
                'protocol_valid_count': valid_count,
                'strict_valid_count': strict_valid_count,
                'markdown_recovered_count': markdown_recovered_count,
                'memory': {'status': 'sampled' if any(r['samples'] for r in memory_runs) else 'unavailable',
                           'scope': 'local worker /proc VmRSS only; excludes device accelerator, children and remote memory',
                           'sampling_window': 'worker lifetime including cold start, warmup and requests',
                           'sampling_interval_seconds': .02, 'runs': memory_runs,
                           'peak_rss_kib': max((r['peak_rss_kib'] for r in memory_runs), default=0)},
                'manifest_sha256': hashlib.sha256(Path(args.manifest).read_bytes()).hexdigest(),
                'prompt_sha256': hashlib.sha256(Path(args.prompt).read_bytes()).hexdigest(),
                'config_sha256': hashlib.sha256(Path(args.config).read_bytes()).hexdigest()}
    window = metadata['measurement_window_seconds']
    metadata['effective_throughput'] = valid_count / window if window > 0 else None
    output.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata['exit_code']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('manifest', 'prompt', 'config', 'worker', 'output', 'warmup-image'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--timeout', type=float, required=True)
    parser.add_argument('--startup-timeout', type=float, required=True)
    parser.add_argument('--warmup', type=int, default=1)
    parser.add_argument('--mode', choices=('engineering', 'formal'), default='engineering')
    parser.add_argument('--protocol', choices=PROTOCOLS, default=JOINT_PROTOCOL)
    parser.add_argument('--readiness')
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not math.isfinite(args.startup_timeout) or args.timeout <= 0 or args.startup_timeout <= 0 or args.warmup < 1:
        parser.error('positive timeouts and at least one warmup are required')
    raise SystemExit(run(args))


if __name__ == '__main__':
    main()

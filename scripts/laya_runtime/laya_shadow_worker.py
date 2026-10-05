#!/usr/bin/env python3
"""Private, local-only Laya shadow worker; never authorizes alarm filtering."""
import argparse
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import signal
import socket
import stat
import struct
import time

PROTOCOL = 'laya-shadow-v1'
MAX_JSON = 16384
MAX_IMAGE = 2 * 1024 * 1024
PROFILE = 'laya-256p-256s-v1'


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for part in iter(lambda: f.read(1024 * 1024), b''):
            h.update(part)
    return h.hexdigest()


def read_exact(conn, count, deadline=None):
    out = bytearray()
    while len(out) < count:
        if deadline is not None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise socket.timeout('request_deadline_exceeded')
            conn.settimeout(remaining)
        part = conn.recv(count - len(out))
        if not part:
            raise ValueError('truncated_message')
        out.extend(part)
    return bytes(out)


def read_request(conn, timeout=1.5):
    deadline = time.monotonic() + timeout
    size = struct.unpack('!I', read_exact(conn, 4, deadline))[0]
    if not 2 <= size <= MAX_JSON:
        raise ValueError('invalid_metadata_size')
    def reject_constant(value):
        raise ValueError('nonfinite_json_constant')
    try:
        value = json.loads(read_exact(conn, size, deadline).decode('utf-8'), parse_constant=reject_constant)
    except RecursionError as error:
        raise ValueError('metadata_too_deep') from error
    stack = [(value, 0)]
    while stack:
        item, depth = stack.pop()
        if depth > 16:
            raise ValueError('metadata_too_deep')
        if isinstance(item, float) and not math.isfinite(item):
            raise ValueError('nonfinite_json_number')
        if isinstance(item, dict):
            stack.extend((v, depth + 1) for v in item.values())
        elif isinstance(item, list):
            stack.extend((v, depth + 1) for v in item)
    if not isinstance(value, dict):
        raise ValueError('metadata_not_object')
    if value.get('protocol') != PROTOCOL or value.get('profile') != PROFILE:
        raise ValueError('unsupported_protocol_or_profile')
    if value.get('question_id') != 'helmet-review-en-v1' or type(value.get('question_version')) is not int or value['question_version'] != 1:
        raise ValueError('unsupported_question')
    for field in ('request_id', 'task_id', 'run_epoch'):
        if not isinstance(value.get(field), str) or not 1 <= len(value[field]) <= 256:
            raise ValueError('invalid_identity_' + field)
    size = value.get('image_size')
    if type(size) is not int or not 1 <= size <= MAX_IMAGE:
        raise ValueError('invalid_image_size')
    if value.get('image_encoding') != 'jpeg':
        raise ValueError('unsupported_image_encoding')
    for field in ('image_width', 'image_height'):
        if type(value.get(field)) is not int or not 1 <= value[field] <= 2048:
            raise ValueError('invalid_' + field)
    return value, read_exact(conn, size, deadline)


def write_response(conn, value):
    data = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode()
    if len(data) > MAX_JSON:
        raise ValueError('response_too_large')
    conn.sendall(struct.pack('!I', len(data)) + data)


def response_base(request):
    return {'protocol': PROTOCOL, 'request_id': request['request_id'],
            'run_epoch': request['run_epoch'], 'profile': PROFILE,
            'question_id': 'helmet-review-en-v1', 'question_version': 1}


class Backend:
    def __init__(self, manifest_path, expected_sha):
        if digest(manifest_path) != expected_sha:
            raise ValueError('worker_manifest_identity_mismatch')
        self.manifest = json.loads(Path(manifest_path).read_text())
        self.base = Path(manifest_path).parent
        if self.manifest.get('schema') != 1 or self.manifest.get('qualification') != 'diagnostic-shadow-only':
            raise ValueError('unsupported_worker_manifest')
        def frozen(spec):
            path = (self.base / spec['path']).resolve()
            if digest(path) != spec['sha256']:
                raise ValueError('artifact_identity_mismatch:' + path.name)
            return path
        for name, spec in self.manifest['implementation'].items():
            if frozen(spec) != Path(__file__).with_name(name).resolve():
                raise ValueError('implementation_path_mismatch')
        from bmrt_bridge import Bridge
        from image_frontend import ImageFrontend, load_cpu_assets
        self.frontend = ImageFrontend(frozen(self.manifest['question_pack']), self.manifest['question_pack']['sha256'])
        cpu_dir = Path(self.manifest['cpu_directory'])
        self.table, self.heads, self.cfg = load_cpu_assets(cpu_dir, self.frontend.pack)
        helper = frozen(self.manifest['cpu_helper'])
        spec = importlib.util.spec_from_file_location('frozen_laya_cpu', helper)
        self.cpu = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.cpu)
        paths = {role: frozen(value) for role, value in self.manifest['models'].items()}
        self.bridge = Bridge(frozen(self.manifest['library']), self.manifest.get('device_id', 0))
        try:
            self.before = self.bridge.snapshot()
            self.models = {role: self.bridge.load(paths[role], self.manifest['models'][role])
                           for role in ('tower', 'adapter', 'decision')}
            self.after = self.bridge.snapshot()
        except BaseException:
            self.bridge.close()
            raise
        self.identity = {role: self.manifest['models'][role]['sha256'] for role in self.models}

    def infer(self, request, encoded):
        import numpy as np
        from PIL import Image
        from image_frontend import decision_feeds
        start = time.perf_counter()
        # Limit decoded and padded allocation before ImageFrontend decodes data.
        with Image.open(io.BytesIO(encoded)) as image:
            if image.format != 'JPEG' or max(image.size) ** 2 > 4194304:
                raise ValueError('unsupported_image_format_or_dimensions')
            if request.get('image_width') is not None and request['image_width'] != image.width:
                raise ValueError('roi_width_mismatch')
            if request.get('image_height') is not None and request['image_height'] != image.height:
                raise ValueError('roi_height_mismatch')
        prepared = self.frontend.prepare(encoded, square_policy='center-pad-white-v1')
        preprocess_ms = (time.perf_counter() - start) * 1000
        tower, t1 = self.models['tower'].run({'pixel_values': prepared['pixel_values']})
        adapter, t2 = self.models['adapter'].run({'tower_features': tower['tower_features']})
        cpu_start = time.perf_counter()
        feeds = decision_feeds(prepared, adapter['visual_tokens'], self.table, self.heads)
        splice_ms = (time.perf_counter() - cpu_start) * 1000
        decision, t3 = self.models['decision'].run(feeds)
        cpu_start = time.perf_counter()
        logits, acts = self.cpu.score_and_act(decision['hidden_states'], prepared['marker_positions'],
                                              prepared['marker_mask'], self.heads)
        if not np.isfinite(logits).all() or not np.isfinite(acts).all():
            raise RuntimeError('nonfinite_scores')
        scaled = logits[0, :3] / self.frontend.pack['temperature']['value']
        exp = np.exp(scaled - scaled.max()); probs = exp / exp.sum()
        if probs.shape != (3,) or not np.isfinite(probs).all():
            raise RuntimeError('nonfinite_probability')
        ordered = np.sort(probs)
        result = response_base(request)
        result.update({'decision_status': 'unknown', 'reason': 'numerical_and_business_qualification_pending',
                       'ordered_options': prepared['option_labels'], 'probabilities': probs.tolist(),
                       'top1': prepared['option_labels'][int(np.argmax(probs))],
                       'top2_margin': float(ordered[-1] - ordered[-2]),
                       'model_hashes': self.identity, 'input': prepared['metadata'],
                       'raw_option_logits': logits[0, :3].tolist(), 'raw_action_logits': acts[0].tolist(),
                       'timing_ms': {'preprocess': preprocess_ms, 'splice': splice_ms,
                                     'postprocess': (time.perf_counter() - cpu_start) * 1000,
                                     'worker_total': (time.perf_counter() - start) * 1000,
                                     'tower': t1, 'adapter': t2, 'decision': t3}})
        return result

    def close(self):
        self.bridge.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--manifest-sha256', required=True)
    p.add_argument('--socket', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if not a.socket.is_absolute() or len(str(a.socket).encode()) >= 104:
        raise ValueError('invalid_socket_path')
    if a.socket.exists() or a.socket.is_symlink():
        raise FileExistsError('socket_path_already_exists')
    a.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    os.umask(0o077)
    events = (a.output / 'requests.jsonl').open('x', buffering=1)
    server = None; backend = None; stopping = False; socket_inode = None
    failure_type = None
    counts = {'completed': 0, 'failed': 0, 'invalid_requests': 0, 'response_dropped': 0}
    def stop(signum, frame):
        nonlocal stopping
        stopping = True
    def emit(value):
        nonlocal stopping
        line = json.dumps(value, ensure_ascii=False, allow_nan=False) + '\n'
        if events.tell() + len(line.encode('utf-8')) > 20 * 1024 * 1024:
            stopping = True
            return False
        events.write(line)
        return True
    signal.signal(signal.SIGTERM, stop); signal.signal(signal.SIGINT, stop)
    try:
        backend = Backend(a.manifest, a.manifest_sha256)
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(str(a.socket)); socket_inode = a.socket.lstat().st_ino
        server.listen(2); server.settimeout(.5)
        (a.output / 'ready.json').write_text(json.dumps({'status': 'READY_SHADOW_ONLY',
             'manifest_sha256': a.manifest_sha256, 'model_hashes': backend.identity,
             'before_models': backend.before, 'after_models': backend.after}) + '\n')
        while not stopping:
            try:
                conn, _ = server.accept()
            except socket.timeout:
                continue
            request = None
            with conn:
                conn.settimeout(1.5)
                try:
                    request, encoded = read_request(conn)
                    result = backend.infer(request, encoded)
                    counts['completed'] += 1
                except (ValueError, TypeError, KeyError, OSError, RuntimeError) as error:
                    if request is None:
                        counts['invalid_requests'] += 1
                        emit({'event': 'invalid_request', 'error_type': type(error).__name__})
                        continue
                    counts['failed'] += 1
                    result = response_base(request)
                    result.update(decision_status='unavailable', reason=type(error).__name__)
                try:
                    conn.settimeout(.5)
                    write_response(conn, result)
                except OSError:
                    counts['response_dropped'] += 1
                emit({'event': 'result', 'request': request, 'result': result})
            # Bounded diagnostics: fail closed for the worker if its log reaches 20MiB.
            # Cosmo's shadow client continues normal alarms on unavailability.
            if events.tell() >= 20 * 1024 * 1024:
                stopping = True
    except BaseException as error:
        failure_type = type(error).__name__
        raise
    finally:
        if server is not None:
            server.close()
        if backend is not None:
            backend.close()
        if socket_inode is not None:
            try:
                st = a.socket.lstat()
                if stat.S_ISSOCK(st.st_mode) and st.st_ino == socket_inode:
                    a.socket.unlink()
            except FileNotFoundError:
                pass
        events.close()
        (a.output / 'stop.json').write_text(json.dumps({'status': 'FAILED' if failure_type else 'STOPPED', 'failure_type': failure_type, 'counts': counts}) + '\n')


if __name__ == '__main__':
    main()

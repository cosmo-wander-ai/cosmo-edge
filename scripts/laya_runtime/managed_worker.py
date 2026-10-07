#!/usr/bin/env python3
"""Managed single-instance worker. Model decisions remain the engine's responsibility."""
import argparse
import fcntl
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import signal
import socket
import stat
import time
from laya_shadow_worker import Backend, read_request, response_base, write_response


def notify(message):
    address = os.environ.get('NOTIFY_SOCKET')
    if not address:
        return
    if address.startswith('@'):
        address = '\0' + address[1:]
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as channel:
        channel.sendto(message.encode(), address)


def serve(manifest, manifest_sha256, runtime, logs, backend_factory=Backend,
          request_reader=read_request, response_writer=write_response, error_response=None):
    runtime = Path(runtime)
    logs = Path(logs)
    runtime.mkdir(parents=True, exist_ok=True, mode=0o700)
    logs.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.umask(0o077)
    # Lock is held before touching stale sockets or allocating any NPU model.
    lock = (runtime / 'worker.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    path = runtime / 'worker.sock'
    if path.exists() or path.is_symlink():
        info = path.lstat()
        if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
            raise RuntimeError('unsafe_existing_socket')
        path.unlink()
    logger = logging.getLogger('laya-runtime')
    logger.setLevel(logging.INFO)
    handler = RotatingFileHandler(logs / 'requests.jsonl', maxBytes=4 * 1024 * 1024, backupCount=4)
    logger.addHandler(handler)
    backend = None
    server = None
    stopping = False
    inode = None
    completed = 0
    status_path = runtime / 'status.json'
    def stop(_signal, _frame):
        nonlocal stopping
        stopping = True
    def status(state):
        value = {'state': state, 'pid': os.getpid(), 'updated_at': time.time(),
                 'manifest_sha256': manifest_sha256, 'completed': completed,
                 'qualification': 'business_acceptance_pending', 'automatic_filtering': False}
        if backend:
            value['model_hashes'] = backend.identity
        temporary = runtime / 'status.tmp'
        temporary.write_text(json.dumps(value) + '\n')
        temporary.replace(status_path)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        status('loading')
        backend = backend_factory(manifest, manifest_sha256)
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(str(path))
        inode = path.lstat().st_ino
        server.listen(2)
        server.settimeout(.5)
        status('ready')
        notify('READY=1\nSTATUS=Laya worker ready; business qualification pending')
        while not stopping:
            # Only the serving thread sends a watchdog pulse; a hung inference is restarted.
            status('ready')
            notify('WATCHDOG=1')
            try:
                conn, _ = server.accept()
            except socket.timeout:
                continue
            request = None
            with conn:
                try:
                    request, encoded = request_reader(conn)
                    result = backend.infer(request, encoded)
                    completed += 1
                except (ValueError, TypeError, KeyError, OSError, RuntimeError) as error:
                    logger.info(json.dumps({'event': 'request_error', 'type': type(error).__name__}))
                    if request is None:
                        continue
                    if error_response:
                        result = error_response(request, 'worker_error')
                    else:
                        result = response_base(request)
                        result.update(decision_status='unavailable', reason=type(error).__name__)
                try:
                    conn.settimeout(.5)
                    response_writer(conn, result)
                except ValueError:
                    if error_response:
                        result = error_response(request, 'invalid_worker_response')
                        try:
                            response_writer(conn, result)
                        except OSError:
                            pass
                except OSError:
                    pass  # Engine deadline expired; no callback or event mutation is possible.
                logger.info(json.dumps({'event': 'result', 'request': request, 'result': result}, allow_nan=False))
    finally:
        notify('STOPPING=1')
        if server:
            server.close()
        if backend:
            backend.close()
        if inode is not None and path.exists() and path.lstat().st_ino == inode:
            path.unlink()
        status('stopped')
        logger.removeHandler(handler)
        handler.close()
        lock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--runtime', default='/run/cosmo-laya')
    parser.add_argument('--logs', default='/data/cosmo-laya/logs')
    args = parser.parse_args()
    serve(args.manifest, args.manifest_sha256, args.runtime, args.logs)


if __name__ == '__main__':
    main()

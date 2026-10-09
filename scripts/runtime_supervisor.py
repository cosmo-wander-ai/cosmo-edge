#!/usr/bin/env python3
"""Keep the foreground engine and its SRS dependency in one shutdown domain.

SRS alone is restartable. Engine exit terminates the supervisor, allowing the
existing systemd/container policy to act. Logs and child exit signals survive a
media restart; no camera configuration, SIP registration or global setting is
changed here. Requires only the Python standard library already used by the
package installer.
"""
import argparse
import ctypes
import http.client
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import time


class BoundedLog:
    """Single-writer, fixed-size files, including oversized pre-upgrade logs."""
    def __init__(self, path, limit=16 * 1024 * 1024, backups=3):
        if limit <= 0 or backups < 1:
            raise ValueError("media log limits must be positive")
        self.path, self.limit, self.backups = Path(path), limit, backups
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.file = None
        for candidate in [self.path] + [self.archive(i) for i in range(1, backups + 1)]:
            if candidate.is_symlink():
                raise OSError("refusing a symlink as a managed media log")
            if candidate.exists() and candidate.stat().st_size > limit:
                fd = os.open(candidate, os.O_RDWR | getattr(os, "O_NOFOLLOW", 0))
                with os.fdopen(fd, "r+b", buffering=0) as existing:
                    existing.seek(-limit, os.SEEK_END)
                    tail = existing.read(limit)
                    existing.seek(0)
                    existing.write(tail)
                    existing.truncate()
        self.open()

    def archive(self, number):
        return Path(str(self.path) + "." + str(number))

    def open(self):
        fd = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_APPEND |
                     getattr(os, "O_NOFOLLOW", 0), 0o600)
        self.file = os.fdopen(fd, "ab", buffering=0)
        self.size = os.fstat(fd).st_size

    def write(self, data):
        # Chunk rather than accumulate an unbounded line from a faulty child.
        if self.file is None:
            self.open()
        while data:
            if self.size >= self.limit:
                self.close()
                for number in range(self.backups, 0, -1):
                    source = self.path if number == 1 else self.archive(number - 1)
                    if source.exists():
                        os.replace(source, self.archive(number))
                self.open()
            part = data[:self.limit - self.size]
            self.file.write(part)
            self.size += len(part)
            data = data[len(part):]

    def close(self):
        if self.file is not None:
            self.file.close()
            self.file = None


def media_health():
    # Do not inherit HTTP proxy settings for this strictly local probe.
    connection = http.client.HTTPConnection("127.0.0.1", 1985, timeout=1)
    try:
        connection.request("GET", "/api/v1/versions")
        response = connection.getresponse()
        data = response.read(4097)
        if response.status != 200 or len(data) > 4096:
            return False
        body = json.loads(data)
        return isinstance(body, dict) and body.get("code") == 0 and isinstance(body.get("data"), dict)
    except (OSError, ValueError, http.client.HTTPException):
        return False
    finally:
        connection.close()


class Supervisor:
    def __init__(self, engine_command, media_command, log_dir, *, health=media_health,
                 poll_interval=0.2, health_interval=5, startup_grace=15,
                 stop_timeout=10, engine_stop_timeout=15,
                 retry_min=2, retry_max=60, stable_period=60):
        self.engine_command, self.media_command = engine_command, media_command
        self.log_dir = Path(log_dir)
        self.health = health
        self.poll_interval, self.health_interval = poll_interval, health_interval
        self.startup_grace, self.stop_timeout = startup_grace, stop_timeout
        self.engine_stop_timeout = engine_stop_timeout
        self.retry_min, self.retry_max, self.stable_period = retry_min, retry_max, stable_period
        self.retry_delay = retry_min
        self.stopping = False
        self.engine = self.media = None
        self.starts = self.health_failures = 0
        self.restart_at = self.started_at = self.check_at = 0
        self.media_stop_at = None
        self.log_limit = 16 * 1024 * 1024
        self.selector = selectors.DefaultSelector()
        self.media_log = self.events = None

    def request_stop(self, *_):
        self.stopping = True

    def event(self, message):
        # Fixed diagnostic text only: never dump command arguments or environment.
        try:
            self.events.write((time.strftime("%Y-%m-%d %H:%M:%S ") + message + "\n").encode())
        except OSError:
            pass  # A diagnostic storage failure must not break child cleanup.

    def drain(self, timeout=0):
        for key, _ in self.selector.select(timeout):
            try:
                chunk = os.read(key.fd, 65536)
            except BlockingIOError:
                continue
            if chunk:
                try:
                    self.media_log.write(chunk)
                except OSError:
                    # Log disk failure must not block the media pipe or kill the
                    # engine. The next chunk retries the write.
                    pass
            else:
                self.selector.unregister(key.fileobj)

    def close_media_pipe(self):
        if self.media and self.media.stdout:
            self.drain()
            try:
                self.selector.unregister(self.media.stdout)
            except KeyError:
                pass
            self.media.stdout.close()

    def schedule_retry(self, now):
        self.restart_at = now + self.retry_delay
        self.event("media restart scheduled delay=%.2fs" % self.retry_delay)
        self.retry_delay = min(self.retry_delay * 2, self.retry_max)

    def record_exit(self, name, code):
        detail = ""
        if code < 0:
            try:
                detail = " signal=" + signal.Signals(-code).name
            except ValueError:
                detail = " signal=" + str(-code)
        self.event("%s exited code=%d%s" % (name, code, detail))

    def start_media(self, now):
        if self.stopping or self.engine.poll() is not None:
            return
        try:
            process = subprocess.Popen(self.media_command, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        except OSError as error:
            self.event("media start failed errno=%s" % error.errno)
            self.schedule_retry(now)
            return
        try:
            os.set_blocking(process.stdout.fileno(), False)
            self.selector.register(process.stdout, selectors.EVENT_READ)
        except BaseException:
            # Retain ownership even if pipe setup fails: run() must reap this
            # child on its exceptional exit instead of leaving an orphan SRS.
            self.media = process
            raise
        self.started_at, self.check_at = now, now + self.startup_grace
        self.media_stop_at = None
        self.health_failures = 0
        self.starts += 1
        self.event("media started pid=%d attempt=%d" % (process.pid, self.starts))
        self.media = process

    def wait_child(self, process, name):
        if process is None:
            return
        if process.poll() is None:
            process.terminate()
        timeout = self.engine_stop_timeout if name == "engine" else self.stop_timeout
        deadline = time.monotonic() + timeout
        while process.poll() is None and time.monotonic() < deadline:
            self.drain(self.poll_interval)
        if process.poll() is None:
            self.event(name + " stop timeout; sending SIGKILL")
            process.kill()
        process.wait()
        self.record_exit(name, process.returncode)

    def run(self):
        self.media_log = BoundedLog(self.log_dir / "srs.log", self.log_limit)
        self.events = BoundedLog(self.log_dir / "media-supervisor.log", 1024 * 1024, 2)
        try:
            if self.stopping:
                return 0
            self.engine = subprocess.Popen(self.engine_command)
            while not self.stopping and self.engine.poll() is None:
                now = time.monotonic()
                if self.media is not None:
                    code = self.media.poll()
                    if code is not None:
                        self.record_exit("media", code)
                        self.close_media_pipe()
                        self.media = None
                        self.schedule_retry(now)
                    elif self.media_stop_at is not None:
                        if now >= self.media_stop_at:
                            self.media.kill()
                    elif now >= self.check_at:
                        self.check_at = now + self.health_interval
                        if self.health():
                            self.health_failures = 0
                            if now - self.started_at >= self.stable_period:
                                self.retry_delay = self.retry_min
                        else:
                            self.health_failures += 1
                            if self.health_failures >= 3:
                                self.event("media health failed 3 times; restarting dependency")
                                self.media.terminate()
                                self.media_stop_at = now + self.stop_timeout
                elif now >= self.restart_at:
                    self.start_media(now)
                self.drain(self.poll_interval)
            # No more restarts once either the engine or supervisor is stopping.
            self.stopping = True
            code = self.engine.poll()
            return 0 if code is None else (code if code >= 0 else 128 - code)
        finally:
            self.stopping = True
            # Engine workers must drain while media is still available.
            self.wait_child(self.engine, "engine")
            self.wait_child(self.media, "media")
            self.close_media_pipe()
            self.selector.close()
            self.media_log.close()
            self.events.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--media", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--log-dir", required=True)
    args = parser.parse_args()
    # A short Linux comm name lets stop.sh stop and wait for this owner before
    # migrating a release. Never identify the supervisor as generic python3.
    if ctypes.CDLL(None, use_errno=True).prctl(15, b"cosmo-runtime", 0, 0, 0) != 0:
        raise OSError(ctypes.get_errno(), "cannot set runtime process name")
    import fcntl
    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    lock_fd = os.open(log_dir / "media-supervisor.lock", os.O_WRONLY | os.O_CREAT |
                      getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        supervisor = Supervisor([args.engine], [args.media, "-c", args.config], log_dir)
        for signum in (signal.SIGTERM, signal.SIGINT):
            signal.signal(signum, supervisor.request_stop)
        return supervisor.run()
    finally:
        os.close(lock_fd)


if __name__ == "__main__":
    raise SystemExit(main())

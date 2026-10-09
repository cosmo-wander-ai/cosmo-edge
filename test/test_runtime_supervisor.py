#!/usr/bin/env python3
"""Real child-process regression tests; no system services or camera access."""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("runtime_supervisor", ROOT / "scripts/runtime_supervisor.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)


def eventually(predicate, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("condition did not become true")


class MediaHealthTest(unittest.TestCase):
    def probe(self, status, body):
        response = Mock(status=status)
        response.read.return_value = body
        connection = Mock()
        connection.getresponse.return_value = response
        with patch.object(runtime.http.client, "HTTPConnection", return_value=connection) as factory:
            healthy = runtime.media_health()
        factory.assert_called_once_with("127.0.0.1", 1985, timeout=1)
        connection.request.assert_called_once_with("GET", "/api/v1/versions")
        response.read.assert_called_once_with(4097)
        connection.close.assert_called_once_with()
        return healthy

    def test_requires_successful_srs_response_and_bounded_body(self):
        self.assertTrue(self.probe(200, b'{"code":0,"data":{"version":"fixture"}}'))
        for status, body in [(503, b'{"code":0,"data":{}}'),
                             (200, b'{"code":1,"data":{}}'),
                             (200, b'{"code":0,"data":null}'),
                             (200, b'[]'), (200, b'not-json'),
                             (200, b'x' * 4097)]:
            with self.subTest(status=status, body_length=len(body)):
                self.assertFalse(self.probe(status, body))

    def test_connection_failure_is_unhealthy_and_closes_socket(self):
        connection = Mock()
        connection.request.side_effect = ConnectionRefusedError()
        with patch.object(runtime.http.client, "HTTPConnection", return_value=connection):
            self.assertFalse(runtime.media_health())
        connection.close.assert_called_once_with()


class BoundedLogTest(unittest.TestCase):
    def test_chunks_oversized_legacy_logs_and_archives_are_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "srs.log"
            path.write_bytes(b"old" * 1000)
            Path(str(path) + ".1").write_bytes(b"older" * 1000)
            log = runtime.BoundedLog(path, limit=128, backups=2)
            for _ in range(20):
                log.write(b"frame\n" * 40)
            log.close()
            self.assertLessEqual(len(list(Path(directory).iterdir())), 3)
            for item in Path(directory).iterdir():
                self.assertLessEqual(item.stat().st_size, 128)
            self.assertTrue(path.read_bytes().endswith(b"frame\n"))

    @unittest.skipUnless(os.name == "posix", "POSIX symlinks")
    def test_does_not_follow_log_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "unrelated"
            target.write_text("preserve")
            path = Path(directory) / "srs.log"
            path.symlink_to(target)
            with self.assertRaises(OSError):
                runtime.BoundedLog(path, limit=8, backups=1)
            self.assertEqual(target.read_text(), "preserve")


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux process supervision")
class SupervisorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name)
        self.supervisor = runtime.Supervisor(
            [sys.executable, "-c", "import time; time.sleep(300)"],
            [sys.executable, "-c", "import time; print('media-ready', flush=True); time.sleep(300)"],
            self.directory, health=lambda: True, poll_interval=0.02,
            health_interval=0.05, startup_grace=0.05, stop_timeout=0.1,
            retry_min=0.08, retry_max=0.3, stable_period=0.15,
        )
        self.result = []
        self.thread = threading.Thread(target=lambda: self.result.append(self.supervisor.run()))

    def start(self):
        self.thread.start()
        eventually(lambda: self.supervisor.media is not None)
        return self.supervisor.media

    def tearDown(self):
        self.supervisor.request_stop()
        if self.thread.ident:
            self.thread.join(5)
        self.assertFalse(self.thread.is_alive())
        self.tmp.cleanup()

    def test_media_death_restarts_without_restarting_engine_and_logs_signal(self):
        first = self.start()
        engine = self.supervisor.engine
        first.kill()
        eventually(lambda: self.supervisor.media is not None and self.supervisor.media.pid != first.pid)
        self.assertIs(self.supervisor.engine, engine)
        self.assertIsNone(engine.poll())
        self.assertIn("SIGKILL", (self.directory / "media-supervisor.log").read_text())

    def test_repeated_start_failures_back_off(self):
        self.supervisor.media_command = [sys.executable, "-c", "raise SystemExit(42)"]
        self.start()
        eventually(lambda: self.supervisor.starts >= 3)
        self.assertGreaterEqual(self.supervisor.retry_delay, 0.3)
        self.assertIsNone(self.supervisor.engine.poll())

    def test_missing_media_binary_retries_without_killing_engine(self):
        self.supervisor.media_command = [str(self.directory / "missing-srs")]
        self.thread.start()
        eventually(lambda: self.supervisor.retry_delay >= 0.3)
        self.assertIsNone(self.supervisor.engine.poll())
        self.assertIn("media start failed", (self.directory / "media-supervisor.log").read_text())

    def test_stop_during_backoff_does_not_launch_another_media_child(self):
        first = self.start()
        self.supervisor.retry_delay = 1
        first.kill()
        eventually(lambda: self.supervisor.media is None)
        self.supervisor.request_stop()
        self.thread.join(5)
        self.assertFalse(self.thread.is_alive())
        self.assertEqual(self.supervisor.starts, 1)

    def test_consecutive_bad_health_restarts_but_transient_failure_does_not(self):
        first = self.start()
        self.supervisor.health = lambda: False
        eventually(lambda: self.supervisor.health_failures >= 1)
        self.supervisor.health = lambda: True
        eventually(lambda: self.supervisor.health_failures == 0)
        self.assertIs(self.supervisor.media, first)
        self.supervisor.health = lambda: False
        eventually(lambda: self.supervisor.media is not None and self.supervisor.media.pid != first.pid)

    def test_engine_exit_stops_media_without_respawn(self):
        media = self.start()
        self.supervisor.engine.terminate()
        self.thread.join(5)
        self.assertFalse(self.thread.is_alive())
        self.assertIsNotNone(media.poll())
        self.assertEqual(self.supervisor.starts, 1)

    def test_shutdown_drains_engine_before_media_and_cancels_backoff(self):
        # Engine verifies media is still alive while handling TERM.
        marker = self.directory / "drained"
        pidfile = self.directory / "media-pid"
        self.supervisor.engine_command = [sys.executable, "-c", "\n".join([
            "import os, signal, time", "from pathlib import Path",
            "def stop(*_):", f"    os.kill(int(Path({str(pidfile)!r}).read_text()), 0)",
            f"    Path({str(marker)!r}).touch()", "    raise SystemExit(0)",
            "signal.signal(signal.SIGTERM, stop)", "while True: time.sleep(.01)",
        ])]
        media = self.start()
        pidfile.write_text(str(media.pid))
        time.sleep(0.1)
        self.supervisor.request_stop()
        self.thread.join(5)
        self.assertTrue(marker.exists())
        self.assertIsNotNone(media.poll())
        self.assertEqual(self.supervisor.starts, 1)

    def test_hung_child_is_killed_and_no_restart_after_stop(self):
        self.supervisor.media_command = [sys.executable, "-c",
            "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); print('ready',flush=True); time.sleep(300)"]
        media = self.start()
        eventually(lambda: (self.directory / "srs.log").exists() and
                   b"ready" in (self.directory / "srs.log").read_bytes())
        self.supervisor.request_stop()
        self.thread.join(5)
        self.assertEqual(media.returncode, -signal.SIGKILL)
        self.assertEqual(self.supervisor.starts, 1)

    def test_media_output_flood_is_bounded_without_blocking_shutdown(self):
        self.supervisor.media_command = [sys.executable, "-c",
            "import os; data=b'x'*65536\nwhile True: os.write(1,data)"]
        self.supervisor.log_limit = 1024
        self.start()
        eventually(lambda: (self.directory / "srs.log.3").exists())
        self.supervisor.request_stop()
        self.thread.join(5)
        self.assertFalse(self.thread.is_alive())
        for log in self.directory.glob("srs.log*"):
            self.assertLessEqual(log.stat().st_size, 1024)


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux CLI lifecycle")
class CommandLineTest(unittest.TestCase):
    def test_cli_process_identity_lock_and_ordered_term(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            media_pid = root / "media.pid"
            engine_ready, drained = root / "engine.ready", root / "engine.drained"
            engine = root / "engine"
            media = root / "media"
            engine.write_text("\n".join([
                "#!/usr/bin/env python3", "import os,signal,time", "from pathlib import Path",
                "def stop(*_):", f"    os.kill(int(Path({str(media_pid)!r}).read_text()), 0)",
                f"    Path({str(drained)!r}).touch()", "    raise SystemExit(0)",
                "signal.signal(signal.SIGTERM, stop)", f"Path({str(engine_ready)!r}).touch()",
                "while True: time.sleep(.01)",
            ]))
            media.write_text("\n".join([
                "#!/usr/bin/env python3", "import os,time", "from pathlib import Path",
                f"Path({str(media_pid)!r}).write_text(str(os.getpid()))", "time.sleep(300)",
            ]))
            engine.chmod(0o700)
            media.chmod(0o700)
            command = [sys.executable, str(ROOT / "scripts/runtime_supervisor.py"),
                       "--engine", str(engine), "--media", str(media), "--config", "unused-fixture",
                       "--log-dir", str(root / "logs")]
            owner = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                eventually(lambda: media_pid.exists() and engine_ready.exists())
                self.assertEqual(Path(f"/proc/{owner.pid}/comm").read_text().strip(), "cosmo-runtime")
                duplicate = subprocess.run(command, stdout=subprocess.DEVNULL,
                                           stderr=subprocess.DEVNULL, timeout=5)
                self.assertNotEqual(duplicate.returncode, 0)
                self.assertIsNone(owner.poll())
                owner.terminate()
                self.assertEqual(owner.wait(timeout=5), 0)
                self.assertTrue(drained.exists())
                with self.assertRaises(ProcessLookupError):
                    os.kill(int(media_pid.read_text()), 0)
            finally:
                if owner.poll() is None:
                    owner.terminate()
                    owner.wait(timeout=30)


if __name__ == "__main__":
    unittest.main()

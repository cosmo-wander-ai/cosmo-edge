#!/usr/bin/env python3
"""Policy/rollback tests plus an opt-in isolated real-rsyslog integration test."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch


REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "scripts/system-log-retention.py"
spec = importlib.util.spec_from_file_location("log_retention", SCRIPT)
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)

DEFAULT_RULES = """# Distribution routing must be preserved.
auth,authpriv.*                 /var/log/auth.log
*.*;auth,authpriv.none          -/var/log/syslog
kern.*                        -/var/log/kern.log
mail.*                        -/var/log/mail.log
mail.err                       /var/log/mail.err
*.emerg                        :omusrmsg:*
*.*                            @@collector.invalid:514
"""
ROTATE_RULES = """/var/log/syslog
/var/log/kern.log
/var/log/auth.log
/var/log/vendor-unmanaged.log
{
    weekly
    rotate 4
    postrotate
        kill -HUP $(ps -ef | awk '{print $2}')
    endscript
}
"""


class RetentionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cosmo-retention-test-")
        self.root = Path(self.temporary.name).resolve()
        self.addCleanup(self.temporary.cleanup)

    def write(self, name, text):
        path = self.root / name.lstrip("/")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def fixture(self, sophon=False):
        self.write("etc/rsyslog.conf", '$IncludeConfig /etc/rsyslog.d/*.conf\n')
        self.write("etc/rsyslog.d/50-default.conf", DEFAULT_RULES)
        self.write("etc/logrotate.conf", "weekly\ninclude /etc/logrotate.d\n")
        self.write("etc/logrotate.d/rsyslog", ROTATE_RULES.replace("weekly", "size=300M")
                   if sophon else ROTATE_RULES)
        if sophon:
            self.write("etc/rsyslog.d/90-prompt-command.conf", "local0.debug -/var/log/prompt-cmd.log\n")
        return policy.Installer(self.root)

    def test_routing_and_templates_preserved_without_stop(self):
        rewritten, managed = policy.rewrite_rsyslog(DEFAULT_RULES)
        self.assertEqual(len(managed), 5)
        self.assertIn('cosmo_log_syslog,/var/log/syslog,33554432,', rewritten)
        self.assertIn('*.*;auth,authpriv.none :omfile:$cosmo_log_syslog;', rewritten)
        self.assertIn('*.emerg                        :omusrmsg:*', rewritten)
        self.assertIn('@@collector.invalid:514', rewritten)
        self.assertNotIn("stop", rewritten)
        self.assertEqual(policy.rewrite_rsyslog(rewritten), (rewritten, managed))
        formatted, _ = policy.rewrite_rsyslog('kern.* -/var/log/kern.log;CustomTemplate\n')
        self.assertIn(';CustomTemplate', formatted)

    def test_custom_unhandled_action_fails_instead_of_partial_adoption(self):
        with self.assertRaises(policy.PolicyError):
            policy.rewrite_rsyslog('action(type="omfile" file="/var/log/syslog")\n')

    def test_logrotate_preserves_other_logs_and_scripts(self):
        rendered = policy.remove_logrotate_paths(ROTATE_RULES, {
            "/var/log/syslog", "/var/log/kern.log", "/var/log/auth.log"})
        self.assertNotIn("/var/log/syslog", rendered)
        self.assertIn("/var/log/vendor-unmanaged.log", rendered)
        self.assertIn("awk '{print $2}'", rendered)
        self.assertEqual(policy.remove_logrotate_paths(rendered, {"/var/log/syslog"}), rendered)
        self.assertEqual(policy.remove_logrotate_paths('/var/log/syslog {\n rotate 6\n}\n',
                                                       {"/var/log/syslog"}), "")

    def test_conflicting_wildcard_rejected(self):
        with self.assertRaises(policy.PolicyError):
            policy.remove_logrotate_paths('/var/log/* {\n rotate 4\n}\n', {"/var/log/syslog"})

    def test_unrelated_custom_blocks_and_disabled_backups_are_untouched(self):
        text = '/var/log/vendor-custom.log { size 12k rotate 2 }\n'
        self.assertEqual(policy.remove_logrotate_paths(text, {"/var/log/syslog"}), text)
        installer = self.fixture()
        backup = self.write("etc/logrotate.d/rsyslog.dpkg-old", "/var/log/* { invalid old file }")
        installer.apply()
        installer.commit()
        self.assertEqual(backup.read_text(), "/var/log/* { invalid old file }")

    def test_install_repeat_upgrade_and_rollback_on_both_layouts(self):
        for sophon in (False, True):
            with self.subTest(sophon=sophon):
                installer = self.fixture(sophon)
                original = (self.root / "etc/rsyslog.d/50-default.conf").read_bytes()
                installer.apply()
                helper = self.root / policy.HELPER.lstrip("/")
                self.assertTrue(helper.stat().st_mode & 0o001)
                journal = self.root / policy.JOURNAL_CONFIG.lstrip("/")
                self.assertIn("SystemMaxUse=128M", journal.read_text())
                rules = self.root / "etc/rsyslog.d/50-default.conf"
                first = rules.read_bytes()
                self.assertNotEqual(first, original)
                installer.commit()
                installer.apply()
                self.assertEqual(rules.read_bytes(), first)
                installer.rollback()
                self.assertEqual(rules.read_bytes(), first)
                self.assertFalse(installer.state.exists())

    def test_validation_failure_restores_bytes_and_mode(self):
        installer = self.fixture()
        original = self.root / "etc/rsyslog.d/50-default.conf"
        original.chmod(0o640)
        before = original.read_bytes()
        with patch.object(installer, "validate", side_effect=policy.PolicyError("bad config")):
            with self.assertRaisesRegex(policy.PolicyError, "bad config"):
                installer.apply()
        self.assertEqual(original.read_bytes(), before)
        self.assertEqual(original.stat().st_mode & 0o777, 0o640)
        self.assertFalse((self.root / policy.HELPER.lstrip("/")).exists())
        self.assertFalse((self.root / policy.JOURNAL_CONFIG.lstrip("/")).exists())
        self.assertFalse(installer.state.exists())

    def test_duplicate_file_actions_fail_before_mutation(self):
        installer = self.fixture()
        self.write("etc/rsyslog.d/99-duplicate.conf", "*.* /var/log/syslog\n")
        with self.assertRaisesRegex(policy.PolicyError, "multiple"):
            installer.apply()
        self.assertFalse(installer.state.exists())

    def test_log_service_start_failure_restores_configuration_and_services(self):
        installer = self.fixture()
        original = self.root / "etc/rsyslog.d/50-default.conf"
        before = original.read_bytes()
        calls = []
        # Exercise the live transition logic with all service commands stubbed;
        # filesystem operations remain confined to the disposable fixture.
        installer.live = True

        def command(*arguments):
            calls.append(arguments)
            if arguments == ("systemctl", "start", "rsyslog"):
                raise policy.PolicyError("injected logging service start failure")
            if arguments[:2] == ("systemctl", "restart"):
                self.assertEqual(original.read_bytes(), before)
            return subprocess.CompletedProcess(arguments, 0, policy.JOURNAL_TEXT, "")

        with patch.object(installer, "command", side_effect=command), \
                patch.object(policy.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)), \
                patch.object(policy.shutil, "which", return_value="/test/tool"):
            with self.assertRaisesRegex(policy.PolicyError, "injected logging service"):
                installer.apply()
        self.assertIn(("systemctl", "stop", "rsyslog"), calls)
        self.assertIn(("systemctl", "restart", "rsyslog"), calls)
        self.assertIn(("systemctl", "restart", "systemd-journald"), calls)
        self.assertEqual(original.read_bytes(), before)
        self.assertFalse(installer.state.exists())

    def test_oversized_logs_converge_and_rotation_keeps_recent_history(self):
        self.fixture()
        active = self.write("var/log/syslog", "old\n" * 1024 + "newest\n")
        inode = active.stat().st_ino
        self.write("var/log/syslog.6.gz", "legacy compressed history")
        self.write("var/log/syslog.1", "old archive\n" * 1000)
        unrelated = self.write("var/log/syslog.customer.gz", "private application data")
        with patch.dict(policy.LIMITS, {"syslog": (128, 3)}):
            policy.maintain_log(self.root, "syslog", rotate=False)
            self.assertEqual(active.stat().st_ino, inode)
            self.assertLessEqual(active.stat().st_size, 128)
            self.assertTrue(active.read_text().endswith("newest\n"))
            self.assertFalse((active.parent / "syslog.6.gz").exists())
            for sequence in range(8):
                active.write_text(f"generation-{sequence}\n" * 50)
                policy.maintain_log(self.root, "syslog", rotate=True)
                self.assertFalse(active.exists())
            self.assertIn("generation-7", (active.parent / "syslog.1").read_text())
            self.assertIn("generation-5", (active.parent / "syslog.3").read_text())
            self.assertEqual(len(list(active.parent.glob("syslog.[0-9]"))), 3)
            for archive in active.parent.glob("syslog.[0-9]"):
                self.assertLessEqual(archive.stat().st_size, 128)
        self.assertEqual(unrelated.read_text(), "private application data")

    def test_symlinks_and_hardlinks_rejected_before_rotation(self):
        active = self.write("var/log/syslog", "retain\n" * 100)
        outside = self.write("outside", "untouched")
        link = active.parent / "syslog.1"
        link.symlink_to(outside)
        with self.assertRaises(policy.PolicyError):
            policy.maintain_log(self.root, "syslog", rotate=True)
        self.assertEqual(outside.read_text(), "untouched")
        self.assertEqual(active.read_text(), "retain\n" * 100)
        link.unlink()
        os.link(outside, link)
        with self.assertRaises(policy.PolicyError):
            policy.maintain_log(self.root, "syslog", rotate=True)

    def test_configuration_symlink_rejected(self):
        installer = self.fixture()
        path = self.root / "etc/rsyslog.d/50-default.conf"
        original = path.read_text()
        path.unlink()
        outside = self.write("outside.conf", original)
        path.symlink_to(outside)
        with self.assertRaises(policy.PolicyError):
            installer.check()
        self.assertEqual(outside.read_text(), original)

    def test_old_and_new_rsyslog_callback_arguments(self):
        self.write("var/log/syslog", "keep\n")
        for arguments in (["/var/log/syslog"], ["rotate", "/var/log/syslog"],
                          ["/var/log/syslog", "/var/log/syslog"]):
            result = subprocess.run(["/usr/bin/python3", str(SCRIPT), *arguments,
                                     "--root", str(self.root)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        command = ["/usr/bin/python3", str(SCRIPT), "rotate", "/var/log/syslog"]
        result = subprocess.run(command + ["/etc/passwd", "--root", str(self.root)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)


@unittest.skipUnless(os.environ.get("COSMO_TEST_RSYSLOG") == "1" and shutil.which("rsyslogd"),
                     "set COSMO_TEST_RSYSLOG=1 on Linux to exercise an isolated real rsyslog")
class RsyslogIntegrationTests(unittest.TestCase):
    def test_generated_logrotate_rules_parse_without_running_hooks(self):
        with tempfile.TemporaryDirectory(prefix="cosmo-logrotate-parse-") as temporary:
            root = Path(temporary)
            unmanaged = root / "vendor.log"
            unmanaged.write_text("keep\n")
            config = root / "logrotate.conf"
            hook_marker = root / "hook-ran"
            original = ROTATE_RULES.replace("/var/log/vendor-unmanaged.log", str(unmanaged))
            original = original.replace("kill -HUP $(ps -ef | awk '{print $2}')",
                                        f"touch {hook_marker}")
            config.write_text(policy.remove_logrotate_paths(original, {
                "/var/log/syslog", "/var/log/kern.log", "/var/log/auth.log"}))
            result = subprocess.run(["logrotate", "--debug", "--state", "/dev/null", str(config)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(hook_marker.exists())
            self.assertEqual(unmanaged.read_text(), "keep\n")

    def test_generated_distribution_and_vendor_rules_parse(self):
        with tempfile.TemporaryDirectory(prefix="cosmo-rsyslog-parse-") as temporary:
            config = Path(temporary) / "rsyslog.conf"
            original = "\n".join(line for line in DEFAULT_RULES.splitlines() if "@@collector" not in line)
            original += "\nlocal0.debug -/var/log/prompt-cmd.log\n"
            rewritten, _ = policy.rewrite_rsyslog(original)
            config.write_text(rewritten)
            result = subprocess.run(["rsyslogd", "-N1", "-f", str(config)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repeated_writer_triggered_rotation_with_production_limits(self):
        with tempfile.TemporaryDirectory(prefix="cosmo-rsyslog-test-") as temporary:
            root = Path(temporary)
            logs = root / "var/log"
            logs.mkdir(parents=True)
            unix_socket = root / "input.sock"
            active = logs / "syslog"
            config = root / "rsyslog.conf"
            # An isolated socket and pid file: this never touches /dev/log,
            # host services, /etc, or the host's system log files.
            callback = root / "rotate"
            counter = root / "rotations"
            # Run the actual helper with only its filesystem root redirected.
            # Preserve the daemon's argument shape (one path, not a shell).
            callback.write_text(
                "#!/usr/bin/python3\nimport runpy,sys\n"
                f"sys.argv += ['--root', {str(root)!r}]\n"
                "try:\n"
                f" runpy.run_path({str(SCRIPT)!r}, run_name='__main__')\n"
                "finally:\n"
                f" with open({str(counter)!r},'a') as f:f.write('rotation\\n')\n"
            )
            callback.chmod(0o755)
            command = f"{callback} /var/log/syslog"
            config.write_text(
                f'global(workDirectory="{root}" maxMessageSize="16k")\n'
                'module(load="imuxsock" SysSock.Use="off")\n'
                f'input(type="imuxsock" Socket="{unix_socket}" '
                'RateLimit.Interval="0" FlowControl="on")\n'
                'template(name="messageOnly" type="string" string="%msg%\\n")\n'
                f'$outchannel cosmo_test,{active},{32 * policy.MIB},{command}\n'
                '*.* :omfile:$cosmo_test;messageOnly\n'
            )
            validation = subprocess.run(["rsyslogd", "-N1", "-f", str(config)],
                                        capture_output=True, text=True)
            self.assertEqual(validation.returncode, 0, validation.stdout + validation.stderr)
            with (root / "daemon.log").open("w+") as daemon_log:
                process = subprocess.Popen(["rsyslogd", "-n", "-i", str(root / "pid"), "-f", str(config)],
                                           stdout=daemon_log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 10
                    while not unix_socket.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.02)
                    self.assertTrue(unix_socket.exists())
                    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
                        client.settimeout(20)
                        client.connect(str(unix_socket))
                        # Over five full 32 MiB rotations; four retained files.
                        message = b"<14>cosmo-retention-test: " + b"x" * 8000
                        for sequence in range(23000):
                            client.send(message + str(sequence).encode())
                        client.send(b"<14>cosmo-retention-test: RETENTION-END-MARKER")
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if active.exists():
                            with active.open("rb") as stream:
                                stream.seek(max(0, active.stat().st_size - 512))
                                if b"RETENTION-END-MARKER" in stream.read():
                                    break
                        time.sleep(0.05)
                    else:
                        sizes = {p.name: p.stat().st_size for p in logs.glob("*")}
                        self.fail(f"rsyslog did not drain the test stream: {sizes}; "
                                  + (root / "daemon.log").read_text())
                finally:
                    process.terminate()
                    process.wait(timeout=10)
                daemon_log.seek(0)
                diagnostics = daemon_log.read()
                self.assertNotIn("suspended", diagnostics.lower(), diagnostics)
                self.assertNotIn("error", diagnostics.lower(), diagnostics)
            files = sorted(logs.glob("syslog*"))
            self.assertGreaterEqual(len(counter.read_text().splitlines()), 5)
            self.assertEqual([p.name for p in files], ["syslog", "syslog.1", "syslog.2", "syslog.3"])
            # rsyslog may finish its current batch before invoking the helper.
            self.assertLess(sum(p.stat().st_size for p in files), 130 * policy.MIB)
            print("Rsyslog rotation evidence:", {
                "sent_messages": 23001, "rotations": len(counter.read_text().splitlines()),
                "retained_bytes": {p.name: p.stat().st_size for p in files},
            })


if __name__ == "__main__":
    unittest.main()

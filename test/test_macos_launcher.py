"""Execute the unchanged launcher with isolated command boundaries, never Docker."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LauncherContracts(unittest.TestCase):
    def run_launcher(self, *args, helper="fixture_run", **overrides):
        with tempfile.TemporaryDirectory(prefix='cosmo launcher ') as directory:
            root = Path(directory)
            (root / 'scripts').mkdir()
            launcher = ROOT / 'scripts/macos-docker-preview.sh'
            shutil.copy2(launcher, root / 'scripts')
            shutil.copy2(ROOT / 'docker-compose.x86.macos.yml', root)
            commands = root / 'commands'
            commands.mkdir()
            log = root / 'argv.jsonl'
            docker = commands / 'docker'
            docker.write_text('''#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
if os.getenv("MUTATE_STOP") == "1" and "down" in args: args.append("-v")
with open(os.environ['COMMAND_LOG'], 'a') as f: f.write(json.dumps(args) + '\\n')
if args == ['--version']: print('Docker fake')
elif args[:2] == ['compose', 'version']: sys.exit(int(os.getenv('COMPOSE_FAIL', '0')))
elif args[0] == 'info':
 if '--format' in args: print('8589934592')
 sys.exit(int(os.getenv('DAEMON_FAIL', '0')))
elif args[0] == 'version': print(os.getenv('SERVER', 'linux/arm64'))
elif args[:2] == ['image', 'inspect']: print(os.getenv('IMAGE', 'linux/amd64'))
elif args[0] == 'inspect': print(os.getenv('RUNNING', 'false') if '.State.Running' in args[2] else os.getenv('HEALTH', 'healthy'))
elif args[0] == 'port': print('127.0.0.1:' + os.getenv('CURRENT_PORT', '8080'))
elif args[0] == 'compose' and 'up' in args: sys.exit(int(os.getenv('UP_FAIL', '0')))
''')
            docker.chmod(0o755)
            # Bash functions intercept absolute macOS probes without rewriting the script.
            envfile = root / 'environment.sh'
            envfile.write_text('''uname() { if [[ "$1" == -s ]]; then printf '%s\\n' "${HOST_OS:-Darwin}"; else echo arm64; fi; }
/usr/sbin/lsof() { [[ "${PORT_BUSY:-0}" == 1 ]] && echo 123; }
/usr/sbin/pkgutil() { return 0; }
sleep() { SECONDS=$((SECONDS + 241)); }
''')
            env = {**os.environ, 'PATH': f'{commands}:/usr/bin:/bin',
                   'BASH_ENV': str(envfile), 'COMMAND_LOG': str(log), **overrides}
            wrapper = helper + '() { bash "$@"; }; ' + helper + ' "$@"'
            result = subprocess.run(['bash', '-c', wrapper, 'fixture', str(root / 'scripts/macos-docker-preview.sh'), *args],
                                    env=env, text=True, capture_output=True, timeout=10)
            calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            return result, calls

    def test_admission(self):
        for env in ({'HOST_OS': 'Linux'}, {'COMPOSE_FAIL': '1'}, {'DAEMON_FAIL': '1'},
                    {'SERVER': 'windows/amd64'}, {'COSMO_X86_WEB_PORT': '1936'},
                    {'COSMO_X86_WEB_PORT': 'no'}, {'COSMO_X86_BUILD_JOBS': '0'}, {'PORT_BUSY': '1'}):
            with self.subTest(env=env):
                result, calls = self.run_launcher('up', **env)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(any('up' in call for call in calls))

    def test_image_reuse_rebuild_and_health(self):
        for args, env, mode in [(('up',), {}, '--no-build'), (('up', '--build'), {}, '--build'),
                                (('up',), {'IMAGE': ''}, '--build'),
                                (('up',), {'IMAGE': 'linux/arm64'}, '--build')]:
            with self.subTest(args=args, env=env):
                result, calls = self.run_launcher(*args, **env)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual([call[3:] for call in calls if 'up' in call],
                                 [['up', '-d', mode, 'cosmo-x86-macos']])
        for health in ('unhealthy', 'exited', 'dead', 'starting'):
            result, calls = self.run_launcher('up', HEALTH=health)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(any('--tail=200' in call for call in calls))
        result, _ = self.run_launcher('up', UP_FAIL='9')
        self.assertEqual(result.returncode, 9)

    def test_existing_port_and_overrides(self):
        result, _ = self.run_launcher('up', RUNNING='true', PORT_BUSY='1')
        self.assertEqual(result.returncode, 0, result.stderr)
        result, _ = self.run_launcher('up', RUNNING='true', PORT_BUSY='1', COSMO_X86_WEB_PORT='8090')
        self.assertNotEqual(result.returncode, 0)
        result, _ = self.run_launcher('url', COSMO_X86_WEB_PORT='8090')
        self.assertEqual(result.stdout.strip(), 'http://127.0.0.1:8090')

    def test_equivalent_name_and_destructive_stop_variants(self):
        result, calls = self.run_launcher('up', helper='fixture_equivalent_name')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(any('--no-build' in call for call in calls))
        result, calls = self.run_launcher('down', MUTATE_STOP='1')
        self.assertEqual(result.returncode, 0)
        self.assertTrue(any('-v' in call for call in calls), 'boundary mutation must take effect')
        with self.assertRaisesRegex(AssertionError, 'persistent stop contract'):
            self.assertEqual([call[3:] for call in calls if call[0] == 'compose'], [['down']], 'persistent stop contract')

    def test_logs_status_and_persistent_stop(self):
        for args, expected in [(('down',), ['down']), (('status',), ['ps']),
                               (('logs',), ['logs', '--tail=200', 'cosmo-x86-macos']),
                               (('logs', '--follow'), ['logs', '--tail=200', '--follow', 'cosmo-x86-macos'])]:
            result, calls = self.run_launcher(*args)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual([call[3:] for call in calls if call[0] == 'compose'], [expected])


if __name__ == '__main__':
    unittest.main()

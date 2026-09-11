"""V1/V2 command boundaries remain supported without a real Docker socket."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ComposeWrapper(unittest.TestCase):
    def test_implementations_preserve_argv_and_exit(self):
        for version in ('v1', 'v2'):
            for status in (0, 17):
                with self.subTest(version=version, status=status), tempfile.TemporaryDirectory() as tmp:
                    directory = Path(tmp)
                    log = directory / 'commands.jsonl'
                    command = '''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['COMMAND_LOG'], 'a') as f: f.write(json.dumps([name, *args]) + '\\n')
if name == 'docker' and args == ['info']: sys.exit(0)
if 'version' in args: sys.exit(0 if name == 'docker-compose' or os.environ['IMPLEMENTATION'] == 'v2' else 1)
sys.exit(int(os.environ['EXIT_STATUS']))
'''
                    for name in ('docker', 'docker-compose'):
                        path = directory / name
                        path.write_text(command)
                        path.chmod(0o755)
                    args = ['-f', '/path with space/compose.yml', 'run', '--rm', 'service', 'a"b', 'c\\d']
                    result = subprocess.run(['bash', str(ROOT / 'scripts/docker-compose.sh'), *args],
                                            env={**os.environ, 'PATH': f'{tmp}:/usr/bin:/bin', 'COMMAND_LOG': str(log),
                                                 'IMPLEMENTATION': version, 'EXIT_STATUS': str(status)},
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, status, result.stderr)
                    calls = [json.loads(line) for line in log.read_text().splitlines()]
                    expected = ['docker', 'compose'] if version == 'v2' else ['docker-compose']
                    self.assertEqual(calls[-1], expected + args)


if __name__ == '__main__':
    unittest.main()

"""Run original npm/CMake entries with deterministic npm, without compiling products."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FAKE_NPM = r'''#!/usr/bin/env python3
import json, os, pathlib, sys
log = pathlib.Path(os.environ['NPM_TEST_LOG'])
records = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
args = sys.argv[1:]
with log.open('a') as stream:
    stream.write(json.dumps({'argv': args, 'cwd': os.getcwd(), 'resource': os.environ.get('AIBOX_RESOURCE_DIR'), 'root': os.environ.get('COSMO_REPO_ROOT')}) + '\n')
mode = os.environ.get('NPM_TEST_MODE', 'hit')
if args[0] == 'ci':
    previous = sum(item['argv'][0] == 'ci' for item in records)
    if mode in ('miss', 'cache-fail', 'install-fail') and previous == 0: sys.exit(1)
    if mode == 'install-fail': sys.exit(9)
if 'cache' in args and mode == 'cache-fail': sys.exit(8)
if args == ['run', 'build']:
    if mode == 'build-fail': sys.exit(7)
    pathlib.Path('dist').mkdir(exist_ok=True)
    pathlib.Path('dist/index.html').write_text('generated frontend')
'''


class WebBuildDependencies(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cosmo web contracts ')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        npm = self.bin / 'npm'
        npm.write_text(FAKE_NPM)
        npm.chmod(0o755)
        self.log = self.root / 'npm.jsonl'
        self.env = dict(os.environ, PATH=f'{self.bin}:{os.environ["PATH"]}', NPM_TEST_LOG=str(self.log))
        self.web = self.root / 'workspace with spaces'
        self.web.mkdir()
        self.lock = {'lockfileVersion': 3, 'packages': {'node_modules/example': {'resolved': 'https://cdn.npmmirror.com/packages/example/1.0.0/example-1.0.0.tgz', 'integrity': 'sha512-fixture'}}}
        self.write_lock()

    def write_lock(self):
        (self.web / 'package-lock.json').write_text(json.dumps(self.lock))

    def records(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def install(self, mode='hit'):
        return subprocess.run(['bash', str(ROOT / 'scripts/build_npm_dependencies.sh'), str(self.web)], env=dict(self.env, NPM_TEST_MODE=mode), text=True, capture_output=True)

    def test_cache_hit_is_offline(self):
        self.assertEqual(self.install().returncode, 0)
        self.assertEqual(len(self.records()), 1)
        self.assertEqual(self.records()[0]['argv'], ['ci', '--offline', '--include=dev', '--loglevel=error', '--no-audit', '--no-fund'])
        self.assertEqual(self.records()[0]['cwd'], str(self.web))

    def test_cache_fill_then_offline_install(self):
        self.assertEqual(self.install('miss').returncode, 0)
        calls = self.records()
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[0]['argv'], calls[2]['argv'])
        self.assertEqual(calls[1]['argv'], ['--loglevel=error', '--no-audit', '--no-fund', 'cache', 'add', self.lock['packages']['node_modules/example']['resolved']])

    def test_incomplete_lock_fails_before_fetch(self):
        del self.lock['packages']['node_modules/example']['integrity']
        self.write_lock()
        result = self.install('miss')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('incomplete entries', result.stderr)
        self.assertEqual(len(self.records()), 1)

    def test_cache_failure_propagates(self):
        self.assertNotEqual(self.install('cache-fail').returncode, 0)
        self.assertEqual(len(self.records()), 2)

    def test_final_install_failure_propagates(self):
        self.assertNotEqual(self.install('install-fail').returncode, 0)
        self.assertEqual(len(self.records()), 3)

    def cmake_fixture(self):
        project = self.root / 'project with spaces'
        web = project / 'src/web'
        for folder in ['src', 'public', 'scripts']:
            (web / folder).mkdir(parents=True)
        for name in ['.npmrc', 'index.html', 'package.json', 'vite.config.js', 'src/app.js', 'public/asset.txt', 'scripts/check.js']:
            (web / name).write_text('fixture')
        shutil.copy2(self.web / 'package-lock.json', web / 'package-lock.json')
        for name in ['GLOSSARY.md', 'SHORT-SCOPES.md']:
            path = project / 'docs/i18n' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture')
        resources = project / 'custom resources'
        for name in ['resource.en-US.json', 'resource.zh-CN.json']:
            path = resources / 'i18n' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{}')
        (project / 'scripts').mkdir()
        shutil.copy2(ROOT / 'scripts/build_npm_dependencies.sh', project / 'scripts/build_npm_dependencies.sh')
        (project / 'CMakeLists.txt').write_text(f'cmake_minimum_required(VERSION 3.16)\nproject(WebContract NONE)\nset(EXECUTABLE_NAME engine_fixture)\nadd_custom_target(engine_fixture)\ninclude("{ROOT}/cmake/web_frontend.cmake")\n')
        build = self.root / 'build'
        result = subprocess.run(['cmake', '-S', str(project), '-B', str(build), f'-DRESOURCE_DIR={resources}'], env=self.env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return project, build, resources

    def build(self, build, mode='hit'):
        return subprocess.run(['cmake', '--build', str(build), '--target', 'web_frontend'], env=dict(self.env, NPM_TEST_MODE=mode), text=True, capture_output=True)

    def test_cmake_tracks_output_dependencies_and_resource_environment(self):
        project, build, resources = self.cmake_fixture()
        result = self.build(build)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        entry = build / 'web/web_unified/dist/index.html'
        self.assertTrue(entry.is_file())
        self.assertEqual(len(self.records()), 3)
        self.assertEqual(self.records()[1]['argv'], ['run', 'resource-i18n:check'])
        self.assertEqual(self.records()[1]['resource'], str(resources))
        self.assertEqual(self.records()[2]['root'], str(project))
        self.assertEqual(self.build(build).returncode, 0)
        self.assertEqual(len(self.records()), 3, 'unchanged inputs must not rebuild')
        entry.unlink()
        self.assertEqual(self.build(build).returncode, 0)
        self.assertEqual(len(self.records()), 6, 'missing output must rebuild')
        dependency = project / 'src/web/src/app.js'
        dependency.write_text('changed input')
        # Give this input an unambiguously newer timestamp without sleeping.
        newer = entry.stat().st_mtime_ns + 2_000_000_000
        os.utime(dependency, ns=(newer, newer))
        self.assertEqual(self.build(build).returncode, 0)
        self.assertEqual(len(self.records()), 9, 'changed input must rebuild')

    def test_cmake_npm_failure_propagates(self):
        _, build, _ = self.cmake_fixture()
        self.assertNotEqual(self.build(build, 'build-fail').returncode, 0)
        self.assertFalse((build / 'web/web_unified/dist/index.html').exists())


if __name__ == '__main__':
    unittest.main()

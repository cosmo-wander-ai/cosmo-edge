#!/usr/bin/env python3
"""Run original build entrypoints with JSON-recording tool doubles.

Compiler/tool boundaries, including the ABI gate, are faked. Package content
verification consumes real tar archives; these fixtures do not demonstrate a
compiled or ABI-admitted SDK. The ABI gate has separate ELF fixture tests.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

import test_package_profile as package_tests

REPOSITORY = package_tests.REPOSITORY
verifier = package_tests.verifier

FAKE_CMAKE = r'''#!/usr/bin/env python3
import json, os, pathlib, shutil, sys
args = sys.argv[1:]
root = pathlib.Path(os.environ['PROJECT_ROOT_PATH'])
with open(os.environ['CALL_LOG'], 'a') as log:
    log.write(json.dumps({'tool':'cmake', 'argv':args, 'env':{k:v for k,v in os.environ.items() if k.startswith(('COSMO_', 'RKNN_', 'RKLLM_', 'ROCKCHIP_'))}})+'\n')
stage = 'configure' if '--build' not in args else ('package' if 'package_all' in args else ('tests' if 'cosmo-tests' in args else 'compile'))
if os.environ.get('FAIL_STAGE') == stage: sys.exit(41)
build = root / os.environ.get('BUILD_DIRECTORY', 'build')
(build / 'install').mkdir(parents=True, exist_ok=True)
if stage == 'package':
    output = build / 'packages'
    output.mkdir(parents=True, exist_ok=True)
    fixture = pathlib.Path(os.environ['ARCHIVE_FIXTURE'])
    mode = os.environ.get('ARCHIVE_MODE', 'one')
    if mode == 'one': shutil.copy2(fixture, output / fixture.name)
    elif mode == 'multiple':
        shutil.copy2(fixture, output / fixture.name)
        shutil.copy2(fixture, output / ('other-' + fixture.name))
    elif mode == 'directory': (output / fixture.name).mkdir()
    elif mode == 'symlink': (output / fixture.name).symlink_to(fixture)
'''

FAKE_ABI_GATE = r'''import hashlib, json, os, pathlib, sys
args = sys.argv[1:]
package = pathlib.Path(args[args.index('--package-root') + 1])
engine_sha256 = hashlib.sha256((package / 'bin/cosmo-engine').read_bytes()).hexdigest()
package_chip = (package / 'share/cosmo/target-chip.txt').read_text().strip()
with open(os.environ['CALL_LOG'], 'a') as log:
    log.write(json.dumps({'tool':'elf_abi_gate.py', 'argv':args, 'package_chip':package_chip,
                         'engine_sha256':engine_sha256})+'\n')
if os.environ.get('FAIL_STAGE') == 'abi': sys.exit(43)
report = pathlib.Path(args[args.index('--report') + 1])
report.write_text(json.dumps({'status':'PASS', 'fixture':True})+'\n')
'''


class BuildEntrypointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='f01 build space ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ('scripts', 'fake-bin', 'config/rknn/platforms',
                          'config/rockchip-build', 'tools/rknn', 'tools/package'):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        for script in ('build.sh', 'build_sophon_package.sh', 'build_rknn.sh',
                       'build_rockchip_package.sh', 'verify_package_contents.py',
                       'verify_model_guard_v2_sdk.py'):
            shutil.copy2(REPOSITORY / 'scripts' / script, self.root / 'scripts' / script)
        for chip in ('bm1688', 'cv186x', 'x86'):
            (self.root / 'data/resource' / ('aiboxresource_' + chip)).mkdir(parents=True)
        for chip in ('rk3576', 'rk3588', 'rv1126b'):
            shutil.copy2(REPOSITORY / f'config/rknn/platforms/{chip}.json',
                         self.root / f'config/rknn/platforms/{chip}.json')
        for name in ('media_sysroot_lock.py', 'stage_platform_resources.py'):
            (self.root / 'tools/rknn' / name).write_text(
                "import json,os,sys\nwith open(os.environ['CALL_LOG'],'a') as f: f.write(json.dumps({'tool':sys.argv[0].split('/')[-1],'argv':sys.argv[1:]})+'\\n')\n")
        self.write_executable('fake-bin/cmake', FAKE_CMAKE)
        (self.root / 'tools/package/elf_abi_gate.py').write_text(FAKE_ABI_GATE)
        # SDK admission uses real ELF inspection, even though CMake is a double.
        for tool in ('readelf', 'nm'):
            executable = shutil.which('aarch64-linux-gnu-' + tool) or shutil.which(tool)
            self.assertIsNotNone(executable, 'ELF inspection tool is required: ' + tool)
            (self.root / ('fake-bin/aarch64-linux-gnu-' + tool)).symlink_to(executable)
        bundled_sdk = REPOSITORY / 'prebuild/model-guard-v2'
        for destination in ('prebuild/model-guard-v2', 'SDK with spaces'):
            shutil.copytree(bundled_sdk, self.root / destination, symlinks=True)
        self.log = self.root / 'calls.jsonl'
        self.env = dict(os.environ, PROJECT_ROOT_PATH=str(self.root),
                        PATH=str(self.root / 'fake-bin') + os.pathsep + os.environ['PATH'],
                        CALL_LOG=str(self.log), COSMO_BUILD_OUTPUT_ROOT=str(self.root / 'export'),
                        COSMO_PACKAGE_MODELS='preserve',
                        COSMO_MODEL_GUARD_SDK_ROOT=str(self.root / 'SDK with spaces'),
                        COSMO_MODEL_GUARD_BUILD_PROFILE='public-runtime')
        self.fixture_case = package_tests.PackageProfileTests()
        self.addCleanup(self.fixture_case.doCleanups)
        self.fixture_case.setUp()

    def write_executable(self, relative, text):
        path = self.root / relative
        path.write_text(text)
        path.chmod(0o755)

    def fixture(self, chip, profile='public-runtime', mismatch=None):
        rockchip = chip in ('rk3576', 'rk3588', 'rv1126b')
        lock = json.loads((REPOSITORY / self.lock_path(chip)).read_text())
        policy = lock['targets'].get(chip)
        package = self.fixture_case.make_package(
            profile, b'CEMCfixture' if profile == 'production-release' else b'plain',
            target_chip=mismatch or chip, platform_chip=chip if rockchip else None,
            platform_runtime=policy['media_runtime_profile'] if policy else None)
        if policy:
            with tarfile.open(package) as source:
                members = [(m, source.extractfile(m).read() if m.isreg() else None) for m in source]
            temporary = package.parent / 'policy.tar.gz'
            with tarfile.open(temporary, 'w:gz') as target:
                for member, data in members:
                    target.addfile(member, io.BytesIO(data) if data is not None else None)
                existing = {member.name for member, _ in members}
                for name in policy['required_package_paths']:
                    if 'cosmo-V1.5.0/' + name in existing:
                        continue
                    data = (json.dumps({'runtime_profile': policy['media_runtime_profile']}).encode()
                            if name.endswith('.json') else b'policy fixture')
                    member = tarfile.TarInfo('cosmo-V1.5.0/' + name)
                    member.size = len(data)
                    target.addfile(member, io.BytesIO(data))
            digest = hashlib.md5(temporary.read_bytes(), usedforsecurity=False).hexdigest()
            package = temporary.parent / f'cosmo-V1.5.0-{digest}.tar.gz'
            temporary.rename(package)
        self.env['ARCHIVE_FIXTURE'] = str(package)
        return package

    @staticmethod
    def lock_path(chip):
        filename = ('builder-lock.rk3588-glibc231.json' if chip == 'rk3588'
                    else 'builder-lock.json')
        return Path('config/rockchip-build') / filename

    def configure_rockchip(self, chip='rk3576'):
        lock = json.loads((REPOSITORY / self.lock_path(chip)).read_text())
        for key in ('rknn_root', 'rkllm_root'):
            lock['common'][key] = str(self.root / key)
        for chip, target in lock['targets'].items():
            target['media_root'] = str(self.root / ('media ' + chip))
        for root, paths in (
            (lock['common']['rknn_root'], ['include/rknn_api.h', 'lib/librknnrt.so']),
            (lock['common']['rkllm_root'], ['include/rkllm.h', 'lib/librkllmrt.so', 'LICENSE']),
            *[(v['media_root'], ['include/rockchip/rk_mpi.h', 'include/rga/im2d.h',
                                 'lib/librockchip_mpp.so', 'lib/librga.so']) for v in lock['targets'].values()],
        ):
            for name in paths:
                file = Path(root) / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.touch()
        path = self.root / self.lock_path(chip)
        path.write_text(json.dumps(lock))
        image_lock = self.root / 'image-lock.json'
        shutil.copy2(path, image_lock)
        self.env.update(COSMO_ROCKCHIP_BUILDER_LOCK=str(image_lock),
                        RKNN_ROOT=lock['common']['rknn_root'],
                        BUILD_DIRECTORY='build_rknn')
        return lock

    def run_script(self, name, *args, success=True):
        result = subprocess.run(['bash', str(self.root / 'scripts' / name), *args],
                                env=self.env, cwd=self.root, text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def reset_outputs(self):
        for name in ('build', 'build_rknn', 'export'):
            shutil.rmtree(self.root / name, ignore_errors=True)
        self.log.unlink(missing_ok=True)

    def assert_export(self, chip, profile):
        prefix = Path(profile) / chip if (chip in ('bm1688', 'cv186x') or profile == 'production-release') else Path(chip)
        output = self.root / 'export' / prefix
        archives = list(output.glob('*.tar.gz'))
        self.assertEqual(len(archives), 1)
        self.assertEqual((output / 'TARGET_CHIP').read_text(), chip + '\n')
        checksum, name = (output / 'SHA256SUMS').read_text().strip().split('  ', 1)
        self.assertEqual(name, archives[0].name)
        self.assertEqual(checksum, hashlib.sha256(archives[0].read_bytes()).hexdigest())

    def test_sophon_targets_profiles_arguments_and_exports(self):
        for chip in ('bm1688', 'cv186x'):
            for profile in ('public-runtime', 'production-release'):
                with self.subTest(chip=chip, profile=profile):
                    self.reset_outputs()
                    self.fixture(chip, profile)
                    self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = profile
                    # A conflicting inherited target must not change explicit selection.
                    self.env['TARGET_CHIP'] = 'rk3576'
                    self.run_script('build_sophon_package.sh', '--chip', chip)
                    configuration = self.calls()[0]['argv']
                    for value in ('-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTS=ON',
                                  '-DCOSMO_DEV_MODE=OFF', '-DCOSMO_TARGET_CHIP=' + chip,
                                  '-DCOSMO_MODEL_GUARD_BUILD_PROFILE=' + profile,
                                  '-DCOSMO_MODEL_GUARD_SDK_ROOT=' + self.env['COSMO_MODEL_GUARD_SDK_ROOT'],
                                  '-DRESOURCE_DIR=' + str(self.root / 'data/resource' / ('aiboxresource_' + chip)),
                                  '-DCOSMO_PACKAGE_MODELS=preserve'):
                        self.assertIn(value, configuration)
                    self.assert_export(chip, profile)

    def test_sophon_default_sdk_ignores_implicit_production_cache(self):
        self.env.pop('COSMO_MODEL_GUARD_SDK_ROOT')
        self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = 'production-release'
        cache_root = Path('/build_output')
        if not cache_root.is_dir():
            self.skipTest('/build_output is not mounted; implicit cache regression requires it')
        cache = cache_root / 'model-guard-sdk-production'
        if not cache.exists():
            cache.mkdir()
            # Never modify an existing cache or remove content created by another process.
            def remove_empty_created_cache():
                try:
                    cache.rmdir()
                except OSError:
                    pass
            self.addCleanup(remove_empty_created_cache)
        self.assertTrue(cache.is_dir(), 'implicit production cache must be a directory')
        self.fixture('bm1688', 'production-release')
        result = self.run_script('build_sophon_package.sh', '--chip', 'bm1688')
        expected = str(self.root / 'prebuild/model-guard-v2')
        self.assertIn('verified_sdk_root=' + expected, result.stdout)
        configuration = next(call['argv'] for call in self.calls() if call['tool'] == 'cmake')
        self.assertIn('-DCOSMO_MODEL_GUARD_SDK_ROOT=' + expected, configuration)
        self.assert_export('bm1688', 'production-release')

    def test_sophon_invalid_override_preserves_install_and_never_invokes_cmake(self):
        self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = 'production-release'
        sdk = Path(self.env['COSMO_MODEL_GUARD_SDK_ROOT'])
        # Start from the genuine release, then invalidate one input at a time.
        for name in ('bin/cosmo-model-provision', 'SDK-MANIFEST.json',
                     'lib/libcosmo_model_guard.so.2.0.0'):
            with self.subTest(component=name):
                self.reset_outputs()
                sentinel = self.root / 'build/install/previous-install'
                sentinel.parent.mkdir(parents=True)
                sentinel.write_bytes(b'preserve previous successful installation')
                component = sdk / name
                original = component.read_bytes()
                try:
                    component.write_bytes(b'invalid release component')
                    result = self.run_script('build_sophon_package.sh', '--chip', 'bm1688',
                                             success=False)
                finally:
                    component.write_bytes(original)
                self.assertIn('SDK verification failed', result.stderr)
                self.assertEqual(self.calls(), [])
                self.assertEqual(sentinel.read_bytes(), b'preserve previous successful installation')
                self.assertFalse((self.root / 'export').exists())

    def test_sophon_compatibility_and_development(self):
        resources = self.root / 'custom resource bm1688 name'
        resources.mkdir()
        for profile in ('public-runtime', 'production-release'):
            self.reset_outputs()
            self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = profile
            self.fixture('unspecified', profile)
            self.run_script('build.sh', '-m', str(resources), '-t')
            args = self.calls()[0]['argv']
            self.assertIn('-DCOSMO_TARGET_CHIP=unspecified', args)
            self.assertIn('-DCOSMO_DEV_MODE=ON', args)
            self.assertIn('-DBUILD_TESTS=OFF', args)
            self.assertIn('-DRESOURCE_DIR=' + str(resources), args)

    def test_sophon_failures_never_export_success(self):
        self.fixture('bm1688')
        for stage in ('configure', 'tests', 'package'):
            with self.subTest(stage=stage):
                self.reset_outputs()
                self.env['FAIL_STAGE'] = stage
                self.run_script('build_sophon_package.sh', success=False)
                self.assertFalse((self.root / 'export').exists())
        self.env.pop('FAIL_STAGE')
        self.reset_outputs()
        self.env['FAIL_STAGE'] = 'compile'
        self.run_script('build.sh', success=False)
        self.env.pop('FAIL_STAGE')
        for mode in ('none', 'multiple', 'directory', 'symlink'):
            with self.subTest(mode=mode):
                self.reset_outputs()
                self.env['ARCHIVE_MODE'] = mode
                self.run_script('build_sophon_package.sh', success=False)
                self.assertFalse((self.root / 'export').exists())
        self.env.pop('ARCHIVE_MODE')
        self.reset_outputs()
        self.fixture('bm1688', mismatch='cv186x')
        result = self.run_script('build_sophon_package.sh', success=False)
        self.assertIn('target chip mismatch', result.stderr)
        self.assertFalse((self.root / 'export').exists())

    def test_rockchip_wrapper_audits_dependency_output_before_export(self):
        self.configure_rockchip()
        # Rockchip audits in the wrapper; Sophon audits in build.sh, exercised above.
        self.write_executable('scripts/build_rknn.sh',
                              '#!/bin/bash\nset -euo pipefail\ncmake --build . --target package_all\n')
        for mode in ('one', 'none', 'multiple', 'directory', 'symlink'):
            with self.subTest(mode=mode):
                self.reset_outputs()
                self.fixture('rk3576')
                self.env['ARCHIVE_MODE'] = mode
                self.run_script('build_rockchip_package.sh', success=mode == 'one')
                if mode == 'one':
                    self.assert_export('rk3576', 'public-runtime')
                else:
                    self.assertFalse((self.root / 'export').exists())
        self.env.pop('ARCHIVE_MODE')
        self.reset_outputs()
        self.fixture('rk3576', mismatch='rv1126b')
        result = self.run_script('build_rockchip_package.sh', success=False)
        self.assertIn('target chip mismatch', result.stderr)
        self.assertFalse((self.root / 'export').exists())

    def test_invalid_arguments_fail_before_tools(self):
        for script, args in (
            ('build.sh', ['-c']), ('build.sh', ['-m']), ('build.sh', ['-c', 'rk3576']),
            ('build.sh', ['-c', 'bm1688', '-m', 'resources']),
            ('build_sophon_package.sh', ['--chip']), ('build_sophon_package.sh', ['--chip', 'rv1126b']),
            ('build_rockchip_package.sh', ['--chip']), ('build_rockchip_package.sh', ['--chip', 'bm1688']),
            ('build_rockchip_package.sh', ['--models']), ('build_rockchip_package.sh', ['--models', 'wrong']),
            ('build_rknn.sh', ['-c']), ('build_rknn.sh', ['-c', 'bm1688']),
        ):
            with self.subTest(script=script, args=args):
                self.run_script(script, *args, success=False)
                self.assertEqual(self.calls(), [])
        self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = 'invalid'
        for script in ('build.sh', 'build_sophon_package.sh', 'build_rknn.sh', 'build_rockchip_package.sh'):
            self.run_script(script, success=False)
            self.assertEqual(self.calls(), [])

    def test_rockchip_targets_profiles_and_exports(self):
        lock = self.configure_rockchip()
        for chip, profile in (('rk3576', 'public-runtime'), ('rk3576', 'production-release'),
                              ('rv1126b', 'public-runtime')):
            for models in ('include', 'preserve'):
                with self.subTest(chip=chip, profile=profile, models=models):
                    self.reset_outputs()
                    self.fixture(chip, profile)
                    self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = profile
                    models_dir = self.root / 'model files'
                    models_dir.mkdir(exist_ok=True)
                    self.env['COSMO_RKNN_MODELS_DIR'] = str(models_dir)
                    self.env['COSMO_RKNN_RESOURCE_OVERLAY_DIR'] = str(self.root / 'overlay files')
                    self.env['COSMO_RKNN_ARTIFACT_MANIFEST'] = str(self.root / 'artifact manifest.json')
                    self.run_script('build_rockchip_package.sh', '--chip', chip, '--models', models)
                    args = next(c['argv'] for c in self.calls() if c['tool'] == 'cmake')
                    for value in ('-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTS=ON',
                                  '-DCOSMO_TARGET_CHIP=' + chip, '-DCOSMO_PACKAGE_MODELS=' + models,
                                  '-DRESOURCE_MODELS_DIR=' + str(models_dir),
                                  '-DRESOURCE_OVERLAY_DIR=' + self.env['COSMO_RKNN_RESOURCE_OVERLAY_DIR'],
                                  '-DRESOURCE_DIR=' + str(self.root / 'data/resource/aiboxresource_x86'),
                                  '-DCOSMO_MODEL_GUARD_BUILD_PROFILE=' + profile,
                                  '-DCOSMO_RKNN_ROOT=' + lock['common']['rknn_root'],
                                  '-DCOSMO_ROCKCHIP_MEDIA_ROOT=' + lock['targets'][chip]['media_root']):
                        self.assertIn(value, args)
                    stages = [c for c in self.calls() if c['tool'] == 'stage_platform_resources.py']
                    if models == 'include':
                        self.assertEqual(len(stages), 1)
                        self.assertEqual(stages[0]['argv'], [
                            '--platform-profile', str(self.root / f'config/rknn/platforms/{chip}.json'),
                            '--artifact-manifest', self.env['COSMO_RKNN_ARTIFACT_MANIFEST'],
                            '--output-dir', self.env['COSMO_RKNN_RESOURCE_OVERLAY_DIR'], '--force'])
                    else:
                        self.assertEqual(stages, [])
                    if profile == 'production-release':
                        self.assertIn('-DCOSMO_MODEL_GUARD_SDK_ROOT=' + self.env['COSMO_MODEL_GUARD_SDK_ROOT'], args)
                    self.assert_export(chip, profile)
                    prefix = Path(profile) / chip if profile == 'production-release' else Path(chip)
                    self.assertEqual((self.root / 'export' / prefix / 'MEDIA_RUNTIME_PROFILE').read_text().strip(),
                                     lock['targets'][chip]['media_runtime_profile'])
                    self.assertFalse(any(c['tool'] == 'elf_abi_gate.py' for c in self.calls()))

    def test_rk3588_isolated_builder_and_abi_gate_export(self):
        lock = self.configure_rockchip('rk3588')
        self.fixture('rk3588')
        abi_sysroot = self.root / 'target ABI sysroot'
        abi_sysroot.mkdir()
        self.env['COSMO_ABI_TARGET_SYSROOT'] = str(abi_sysroot)
        for name in ('COSMO_FFMPEG_ROOT', 'COSMO_TOOLCHAIN_ROOT', 'COSMO_TARGET_SYSROOT'):
            self.env[name] = str(self.root / (name.lower() + ' with spaces'))
        self.env['COSMO_BUILD_JOBS'] = '3'
        self.run_script('build_rockchip_package.sh', '--chip', 'rk3588', '--models', 'preserve')
        calls = self.calls()
        configure = next(c for c in calls if c['tool'] == 'cmake')
        for name in ('COSMO_FFMPEG_ROOT', 'COSMO_TOOLCHAIN_ROOT', 'COSMO_TARGET_SYSROOT'):
            self.assertIn('-D' + name + '=' + self.env[name], configure['argv'])
        for value in ('-DCOSMO_TARGET_CHIP=rk3588', '-DCOSMO_MODEL_GUARD_BUILD_PROFILE=public-runtime',
                      '-DCOSMO_RKLLM_REQUIRED=OFF', '-DBUILD_VLM_EVAL=OFF',
                      '-DCOSMO_PACKAGE_MODELS=preserve', '-DBUILD_TESTS=ON',
                      '-DCOSMO_RKNN_ROOT=' + lock['common']['rknn_root']):
            self.assertIn(value, configure['argv'])
        self.assertEqual(configure['env']['COSMO_ABI_TARGET_SYSROOT'], str(abi_sysroot))
        self.assertFalse(any(a.startswith('-DCOSMO_MODEL_GUARD_SDK_ROOT=') for a in configure['argv']))
        compile_calls = [c for c in calls if c['tool'] == 'cmake' and '--build' in c['argv']]
        self.assertTrue(any('-j3' in c['argv'] for c in compile_calls))
        gates = [c for c in calls if c['tool'] == 'elf_abi_gate.py']
        self.assertEqual(len(gates), 1)
        audited_root = Path(gates[0]['argv'][1])
        self.assertEqual(audited_root.name, 'cosmo-V1.5.0')
        self.assertTrue(audited_root.parent.name.startswith('abi-audit.'))
        self.assertEqual(audited_root.parent.parent, self.root / 'build_rknn')
        self.assertEqual(gates[0]['package_chip'], 'rk3588')
        self.assertEqual(gates[0]['engine_sha256'], hashlib.sha256(b'#!/bin/sh\n').hexdigest())
        self.assertEqual(gates[0]['argv'], [
            '--package-root', str(audited_root),
            '--target-arch', 'aarch64', '--max-glibc', '2.31',
            '--target-sysroot', str(abi_sysroot),
            '--report', str(self.root / 'build_rknn/elf-abi-report.json'),
        ])
        package_index = next(i for i, c in enumerate(calls)
                             if c['tool'] == 'cmake' and 'package_all' in c['argv'])
        self.assertGreater(calls.index(gates[0]), package_index)
        self.assert_export('rk3588', 'public-runtime')
        output = self.root / 'export/rk3588'
        self.assertEqual((output / 'elf-abi-report.json').read_bytes(),
                         (self.root / 'build_rknn/elf-abi-report.json').read_bytes())
        self.assertEqual((output / 'MEDIA_RUNTIME_PROFILE').read_text().strip(), 'rk3588-glibc231-v1')
        self.assertFalse(audited_root.parent.exists(), 'the temporary ABI audit extraction must be removed')

    def test_rk3588_does_not_fall_back_to_legacy_builder_lock(self):
        self.configure_rockchip('rk3588')
        legacy = self.root / self.lock_path('rk3576')
        shutil.copy2(REPOSITORY / self.lock_path('rk3576'), legacy)
        shutil.copy2(legacy, self.env['COSMO_ROCKCHIP_BUILDER_LOCK'])
        result = self.run_script('build_rockchip_package.sh', '--chip', 'rk3588', success=False)
        self.assertIn('builder image lock does not match', result.stderr)
        self.assertEqual(self.calls(), [])
        (self.root / self.lock_path('rk3588')).unlink()
        result = self.run_script('build_rockchip_package.sh', '--chip', 'rk3588', success=False)
        self.assertIn('builder-lock.rk3588-glibc231.json', result.stderr)
        self.assertEqual(self.calls(), [])

    def test_rk3588_protected_request_rejected_before_build_cleanup(self):
        self.env['COSMO_MODEL_GUARD_BUILD_PROFILE'] = 'production-release'
        sentinel = self.root / 'build_rknn/install/previous-install'
        sentinel.parent.mkdir(parents=True)
        sentinel.write_text('keep previous candidate')
        result = self.run_script('build_rockchip_package.sh', '--chip', 'rk3588', success=False)
        self.assertIn('only the public-runtime CV candidate profile', result.stderr)
        self.assertEqual(self.calls(), [])
        self.assertEqual(sentinel.read_text(), 'keep previous candidate')

    def test_rk3588_missing_abi_target_or_failed_gate_cannot_export(self):
        self.configure_rockchip('rk3588')
        self.fixture('rk3588')
        self.env.pop('COSMO_ABI_TARGET_SYSROOT', None)
        result = self.run_script('build_rockchip_package.sh', '--chip', 'rk3588',
                                 '--models', 'preserve', success=False)
        self.assertIn('requires COSMO_ABI_TARGET_SYSROOT', result.stderr)
        self.assertEqual(self.calls(), [])
        self.assertFalse(any(c['tool'] == 'elf_abi_gate.py' for c in self.calls()))
        self.assertFalse((self.root / 'export').exists())
        self.reset_outputs()
        self.env['COSMO_ABI_TARGET_SYSROOT'] = str(self.root / 'ABI target')
        Path(self.env['COSMO_ABI_TARGET_SYSROOT']).mkdir()
        self.env['FAIL_STAGE'] = 'abi'
        self.run_script('build_rockchip_package.sh', '--chip', 'rk3588',
                        '--models', 'preserve', success=False)
        self.assertEqual(len([c for c in self.calls() if c['tool'] == 'elf_abi_gate.py']), 1)
        self.assertFalse((self.root / 'export').exists())
        self.assertEqual(list((self.root / 'build_rknn').glob('abi-audit.*')), [])

    def test_rockchip_development_resource_overrides(self):
        self.configure_rockchip()
        self.fixture('rk3576')
        self.env['COSMO_RKNN_MODELS_DIR'] = str(self.root / 'models override')
        self.env['COSMO_RKNN_RESOURCE_OVERLAY_DIR'] = str(self.root / 'overlay override')
        self.run_script('build_rknn.sh', '-c', 'rk3576', '-m', 'custom resources', '-t')
        args = next(c['argv'] for c in self.calls() if c['tool'] == 'cmake')
        for value in ('-DCOSMO_DEV_MODE=ON', '-DBUILD_TESTS=OFF',
                      '-DRESOURCE_DIR=' + str(self.root / 'custom resources'),
                      '-DRESOURCE_MODELS_DIR=' + self.env['COSMO_RKNN_MODELS_DIR'],
                      '-DRESOURCE_OVERLAY_DIR=' + self.env['COSMO_RKNN_RESOURCE_OVERLAY_DIR']):
            self.assertIn(value, args)

    def test_rockchip_failures_and_artifact_types(self):
        self.configure_rockchip()
        self.fixture('rk3576')
        for stage in ('configure', 'compile', 'tests', 'package'):
            self.reset_outputs()
            self.env['FAIL_STAGE'] = stage
            self.run_script('build_rockchip_package.sh', '--models', 'preserve', success=False)
            self.assertFalse((self.root / 'export').exists())
        self.env.pop('FAIL_STAGE')
        for mode in ('none', 'multiple', 'directory', 'symlink'):
            self.reset_outputs()
            self.env['ARCHIVE_MODE'] = mode
            self.run_script('build_rockchip_package.sh', '--models', 'preserve', success=False)
            self.assertFalse((self.root / 'export').exists())
        self.env.pop('ARCHIVE_MODE')
        self.reset_outputs()
        self.fixture('rk3576', mismatch='rv1126b')
        result = self.run_script('build_rockchip_package.sh', '--models', 'preserve', success=False)
        self.assertIn('target chip mismatch', result.stderr)
        self.assertFalse((self.root / 'export').exists())


if __name__ == '__main__':
    unittest.main()

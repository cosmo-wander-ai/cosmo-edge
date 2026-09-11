#!/usr/bin/env python3
"""Run original build entrypoints with JSON-recording tool doubles.

Only compiler/tool boundaries are faked. Package verification consumes real tar
archives; these fixtures do not demonstrate a compiled or admitted SDK.
"""
import hashlib
import io
import json
import os
import re
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


class BuildEntrypointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='f01 build space ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ('scripts', 'test', 'fake-bin', 'config/rknn/platforms',
                          'config/rockchip-build', 'tools/rknn'):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        for script in ('build.sh', 'build_sophon_package.sh', 'build_rknn.sh',
                       'build_rockchip_package.sh', 'verify_package_contents.py'):
            shutil.copy2(REPOSITORY / 'scripts' / script, self.root / 'scripts' / script)
        for chip in ('bm1688', 'cv186x', 'x86'):
            (self.root / 'data/resource' / ('aiboxresource_' + chip)).mkdir(parents=True)
        for chip in ('rk3576', 'rv1126b'):
            shutil.copy2(REPOSITORY / f'config/rknn/platforms/{chip}.json',
                         self.root / f'config/rknn/platforms/{chip}.json')
        # Regression executables are a tool boundary, not the entrypoint under test.
        for name in ('test_package_profile.py', 'test_verify_model_guard_v2_sdk.py'):
            (self.root / 'test' / name).write_text(
                "import os,sys; sys.exit(42 if os.environ.get('FAIL_STAGE') == 'regression' else 0)\n")
        for name in ('media_sysroot_lock.py', 'stage_platform_resources.py'):
            (self.root / 'tools/rknn' / name).write_text(
                "import json,os,sys\nwith open(os.environ['CALL_LOG'],'a') as f: f.write(json.dumps({'tool':sys.argv[0].split('/')[-1],'argv':sys.argv[1:]})+'\\n')\n")
        self.write_executable('fake-bin/cmake', FAKE_CMAKE)
        self.write_executable('fake-bin/aarch64-linux-gnu-readelf', '#!/bin/sh\nexit 1\n')
        self.log = self.root / 'calls.jsonl'
        self.env = dict(os.environ, PROJECT_ROOT_PATH=str(self.root),
                        PATH=str(self.root / 'fake-bin') + os.pathsep + os.environ['PATH'],
                        CALL_LOG=str(self.log), COSMO_BUILD_OUTPUT_ROOT=str(self.root / 'export'),
                        COSMO_PACKAGE_MODELS='preserve',
                        COSMO_MODEL_GUARD_SDK_ROOT=str(self.root / 'SDK with spaces'),
                        COSMO_MODEL_GUARD_BUILD_PROFILE='public-runtime')
        self.fixture_case = package_tests.PackageProfileTests()
        self.addCleanup(self.fixture_case.doCleanups)

    def write_executable(self, relative, text):
        path = self.root / relative
        path.write_text(text)
        path.chmod(0o755)

    def fixture(self, chip, profile='public-runtime', mismatch=None):
        rockchip = chip in ('rk3576', 'rv1126b')
        lock = json.loads((REPOSITORY / 'config/rockchip-build/builder-lock.json').read_text())
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
                for name in policy['required_package_paths']:
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

    def configure_rockchip(self):
        lock = json.loads((REPOSITORY / 'config/rockchip-build/builder-lock.json').read_text())
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
        path = self.root / 'config/rockchip-build/builder-lock.json'
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
        for stage in ('configure', 'tests', 'package', 'regression'):
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

    def test_package_wrappers_audit_dependency_output_before_export(self):
        self.configure_rockchip()
        for kind, chip, dependency, directory in (
            ('sophon', 'bm1688', 'build.sh', 'build'),
            ('rockchip', 'rk3576', 'build_rknn.sh', 'build_rknn'),
        ):
            # Replace only the called builder boundary, leaving the wrapper unchanged.
            self.write_executable('scripts/' + dependency,
                                  '#!/bin/bash\nset -euo pipefail\ncmake --build . --target package_all\n')
            self.env['BUILD_DIRECTORY'] = directory
            for mode in ('one', 'none', 'multiple', 'directory', 'symlink'):
                with self.subTest(kind=kind, mode=mode):
                    self.reset_outputs()
                    self.fixture(chip)
                    self.env['ARCHIVE_MODE'] = mode
                    self.run_script('build_' + kind + '_package.sh', success=mode == 'one')
                    if mode == 'one':
                        self.assert_export(chip, 'public-runtime')
                    else:
                        self.assertFalse((self.root / 'export').exists())
            self.env.pop('ARCHIVE_MODE')
            self.reset_outputs()
            self.fixture(chip, mismatch='cv186x' if kind == 'sophon' else 'rv1126b')
            result = self.run_script('build_' + kind + '_package.sh', success=False)
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

    def test_shell_variable_and_command_equivalents(self):
        path = self.root / 'scripts/build.sh'
        original = path.read_text()
        # Pick an actual local declaration, without assuming its internal name.
        local = re.search(r'^([a-zA-Z_][a-zA-Z_0-9]*)=""$', original, re.MULTILINE)
        # The equivalent program may have no local empty-string declarations.
        # In that case add a fixture-owned local before exercising its rename.
        local_source = original if local else original.replace('\n', '\nf01_fixture_local=""\n', 1)
        local_name = local[1] if local else 'f01_fixture_local'
        name_pattern = re.escape(local_name)
        renamed = re.sub(r'(?<![\w-])' + name_pattern + r'(?==)', 'f01_local_target', local_source)
        renamed = re.sub(r'(?<=\$\{)' + name_pattern + r'\b', 'f01_local_target', renamed)
        renamed = re.sub(r'\$' + name_pattern + r'\b', '$f01_local_target', renamed)
        # A shell function preserves the public cmake argv contract while splitting
        # tool selection from invocation; no build-command spelling is assumed.
        forwarded = original.replace('\n', '\ncmake() { local selected_tool=cmake; command "$selected_tool" "$@"; }\n', 1)
        for changed in (renamed, forwarded):
            self.assertNotEqual(changed, original, 'mutation must apply')
            path.write_text(changed)
            self.reset_outputs()
            self.fixture('cv186x')
            self.run_script('build_sophon_package.sh', '--chip', 'cv186x')
            self.assertIn('-DCOSMO_TARGET_CHIP=cv186x', self.calls()[0]['argv'])
            self.assert_export('cv186x', 'public-runtime')

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

#!/usr/bin/env python3
"""Guard verifier regression against bundled ELF files and targeted negatives."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('rknn_sdk', ROOT / 'scripts/verify_model_guard_rknn_v1_sdk.py')
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class RealSdkChecks(unittest.TestCase):
    def tools(self):
        # GNU tools inspect cross-architecture ELF without executing target code.
        readelf, nm = shutil.which('readelf'), shutil.which('nm')
        self.assertIsNotNone(readelf, 'readelf is required')
        self.assertIsNotNone(nm, 'nm is required')
        return ['--readelf', readelf, '--nm', nm]

    def verify(self, kind, root, success=True):
        script, profile = (('verify_model_guard_v2_sdk.py', 'public-runtime')
                           if kind == 'sophon' else
                           ('verify_model_guard_rknn_v1_sdk.py', 'production-release'))
        result = subprocess.run([sys.executable, str(ROOT / 'scripts' / script),
                                 '--sdk-root', str(root), '--admission-profile', profile,
                                 *self.tools()], text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_bundled_sophon_and_rknn_with_real_elf_tools(self):
        self.verify('sophon', ROOT / 'prebuild/model-guard-v2')
        self.verify('rknn', ROOT / 'prebuild/model-guard-v2-rknn-abi1')

    def test_rknn_manifest_equivalent_format_and_rejecting_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary) / 'sdk'
            shutil.copytree(ROOT / 'prebuild/model-guard-v2-rknn-abi1', sdk, symlinks=True)
            path = sdk / 'SDK-MANIFEST.json'
            manifest = json.loads(path.read_text())
            path.write_text(json.dumps(manifest, indent=4, sort_keys=True))
            self.verify('rknn', sdk)
            for field, value, reason in (
                ('release_id', 'wrong', 'contract mismatch'),
                ('certificate_storage', 'fixed-directory', 'contract mismatch'),
                ('build', {'image': 'wrong'}, 'build image mismatch'),
                ('source', {'repository': 'wrong', 'tree': '0' * 40}, 'source identity mismatch'),
            ):
                changed = dict(manifest, **{field: value})
                self.assertNotEqual(changed, manifest)
                path.write_text(json.dumps(changed))
                self.assertIn(reason, self.verify('rknn', sdk, False).stderr)

    def test_real_elf_corruption_and_fixture_marker_rejected(self):
        for kind, directory, library in (
            ('sophon', 'model-guard-v2', 'libcosmo_model_guard.so.2.0.0'),
            ('rknn', 'model-guard-v2-rknn-abi1', 'libcosmo_model_guard_rknn.so.1.0.0'),
        ):
            with tempfile.TemporaryDirectory() as temporary:
                sdk = Path(temporary) / 'sdk'
                shutil.copytree(ROOT / 'prebuild' / directory, sdk, symlinks=True)
                target = sdk / 'lib' / library
                target.write_bytes(b'not an ELF')
                self.assertIn('inspection tool failed', self.verify(kind, sdk, False).stderr)
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary) / 'sdk'
            shutil.copytree(ROOT / 'prebuild/model-guard-v2-rknn-abi1', sdk, symlinks=True)
            marker = sdk / 'share/cosmo-model-guard/TEST_FIXTURE_DO_NOT_DEPLOY'
            marker.parent.mkdir(parents=True, exist_ok=True)
            marker.write_text('fixture')
            self.assertIn('test fixture', self.verify('rknn', sdk, False).stderr)


if __name__ == '__main__':
    unittest.main()

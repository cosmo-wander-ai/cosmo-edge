#!/usr/bin/env python3
"""Configure/install checks for explicit FFmpeg dependency roots (no compilation)."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required')
class ExternalFfmpegRootTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='cosmo-ffmpeg-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.prefix = self.root / 'external prefix'
        (self.prefix / 'include/libavcodec').mkdir(parents=True)
        (self.prefix / 'include/libavcodec/avcodec.h').touch()
        (self.prefix / 'lib').mkdir()
        for library in ('avcodec', 'avdevice', 'avfilter', 'avformat', 'avutil',
                        'swresample', 'swscale'):
            (self.prefix / f'lib/lib{library}.so.1').write_bytes(b'fixture')
            (self.prefix / f'lib/lib{library}.so').symlink_to(f'lib{library}.so.1')
        (self.source / 'cmake').mkdir()
        shutil.copy2(REPOSITORY / 'cmake/patch_ffmpeg_flv_hevc.cmake', self.source / 'cmake')
        (self.source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\n'
            'project(ffmpeg_prefix_contract NONE)\n'
            'set(COSMO_TARGET_ARCH aarch64)\n'
            f'include("{REPOSITORY}/cmake/ffmpeg.cmake")\n'
            'get_target_property(selected ffmpeg_avcodec IMPORTED_LOCATION)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${selected}")\n')

    def configure(self):
        return subprocess.run([
            'cmake', '-S', str(self.source), '-B', str(self.root / 'build'),
            '-DCOSMO_FFMPEG_ROOT=' + str(self.prefix),
            '-DCMAKE_INSTALL_PREFIX=' + str(self.root / 'installed'),
        ], text=True, capture_output=True)

    def test_external_root_installs_its_own_manifest_and_licenses(self):
        (self.prefix / 'share/cosmo').mkdir(parents=True)
        identity = {'source': 'external-fixture', 'artifacts': []}
        (self.prefix / 'share/cosmo/ffmpeg-manifest.json').write_text(json.dumps(identity))
        (self.prefix / 'share/licenses/ffmpeg').mkdir(parents=True)
        (self.prefix / 'share/licenses/ffmpeg/NOTICE').write_text('external license\n')
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'build/selected.txt').read_text(),
                         str(self.prefix / 'lib/libavcodec.so'))
        subprocess.run(['cmake', '--install', str(self.root / 'build')], check=True,
                       capture_output=True, text=True)
        installed = self.root / 'installed'
        self.assertEqual(json.loads((installed / 'licenses/ffmpeg/manifest.json').read_text()), identity)
        self.assertEqual((installed / 'licenses/ffmpeg/NOTICE').read_text(), 'external license\n')
        self.assertTrue((installed / 'lib/libavcodec.so').is_symlink())
        self.assertEqual((installed / 'lib/libavcodec.so').read_bytes(), b'fixture')

    def test_missing_external_provenance_fails_without_falling_back(self):
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('External FFmpeg requires its own build manifest', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()

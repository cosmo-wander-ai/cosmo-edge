import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'prepare_visual_release', Path(__file__).parents[1] / 'scripts/laya_runtime/prepare_visual_release.py')
release_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release_tools)


class VisualReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ['cpu', 'runtime', 'web', 'deps', 'models']:
            (self.root / name).mkdir()
        def asset(name, content):
            path = self.root / name
            path.write_bytes(content)
            return {'path': str(path), 'sha256': release_tools.digest(path)}
        models = {name: asset('models/' + name + '.bmodel', name.encode())
                  for name in ['tower', 'adapter', 'decision']}
        cpu = asset('cpu/table.npy', b'fixed CPU table')
        self.engine = self.root / 'cosmo-engine'
        self.engine.write_bytes(b'fixed engine')
        (self.root / 'web/index.html').write_text('fixed web')
        (self.root / 'deps/package.py').write_text('fixed dependency')
        self.manifest = self.root / 'manifest.json'
        self.manifest.write_text(json.dumps({
            'schema': 2, 'qualification': 'business-acceptance-pending',
            'models': models, 'cpu_directory': str(self.root / 'cpu'),
            'cpu_files': {'table.npy': cpu['sha256']},
            'implementation': {'worker.py': asset('runtime/worker.py', b'fixed worker')},
            'tokenizer': asset('tokenizer.json', b'tokenizer'),
            'cpu_helper': asset('cpu/helper.py', b'helper'),
            'library': asset('bridge.so', b'bridge')}))

    def prepare(self, **overrides):
        args = dict(manifest_path=self.manifest, manifest_sha=release_tools.digest(self.manifest),
                    engine=self.engine, engine_sha=release_tools.digest(self.engine), engine_commit='a' * 40,
                    web=self.root / 'web', dependency_roots=[self.root / 'deps'], release=self.root / 'release')
        args.update(overrides)
        return release_tools.prepare(**args)

    def test_self_contained_release_survives_source_removal(self):
        receipt = self.prepare()
        release = self.root / 'release'
        manifest = json.loads((release / 'manifest.json').read_text())
        for path in [self.root / 'models/tower.bmodel', self.root / 'cpu/table.npy']:
            path.unlink()
        for entry in receipt['files']:
            self.assertEqual(release_tools.digest(release / entry['path']), entry['sha256'])
        self.assertEqual(manifest['cpu_directory'], 'cpu')
        for entry in [*manifest['models'].values(), *manifest['implementation'].values(),
                      manifest['library'], manifest['tokenizer'], manifest['cpu_helper']]:
            self.assertFalse(Path(entry['path']).is_absolute())
            self.assertTrue((release / entry['path']).is_file())
        self.assertFalse(receipt['automatic_filtering'])

    def test_modified_asset_cannot_be_packaged_as_frozen_release(self):
        (self.root / 'models/tower.bmodel').write_bytes(b'changed model')
        with self.assertRaisesRegex(ValueError, 'asset_identity_mismatch'):
            self.prepare()
        self.assertFalse((self.root / 'release').exists())

    def test_two_dependency_roots_cannot_silently_replace_a_package(self):
        other = self.root / 'other'
        other.mkdir()
        (other / 'package.py').write_text('incompatible dependency')
        with self.assertRaisesRegex(ValueError, 'conflicting_dependency'):
            self.prepare(dependency_roots=[self.root / 'deps', other])


if __name__ == '__main__':
    unittest.main()

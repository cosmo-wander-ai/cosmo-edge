#!/usr/bin/env python3
"""Assemble a self-contained typed Laya runtime and Cosmo engine/web release.

Preparation does not change installed files or services. Large immutable model
and NumPy assets may be hardlinked; manifests contain only relative asset paths.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def prepare(manifest_path, manifest_sha, engine, engine_sha, engine_commit, web,
            dependency_roots, release):
    manifest_path, engine, web, release = map(Path, (manifest_path, engine, web, release))
    if digest(manifest_path) != manifest_sha or digest(engine) != engine_sha:
        raise ValueError('source_identity_mismatch')
    manifest = json.loads(manifest_path.read_text())
    if manifest.get('schema') != 2 or manifest.get('qualification') != 'business-acceptance-pending':
        raise ValueError('requires_typed_review_manifest')
    if not release.is_absolute() or release.exists() or not web.is_dir():
        raise ValueError('new_absolute_release_and_existing_web_required')
    if len(engine_commit) != 40 or any(x not in '0123456789abcdef' for x in engine_commit):
        raise ValueError('engine_source_commit_required')
    base = manifest_path.parent
    files = {}

    def add(source, destination, expected=None):
        source = Path(source).resolve()
        actual = digest(source)
        if expected is not None and actual != expected:
            raise ValueError('asset_identity_mismatch:' + destination)
        if destination in files and files[destination]['sha256'] != actual:
            raise ValueError('conflicting_dependency:' + destination)
        files[destination] = {'source': source, 'sha256': actual, 'bytes': source.stat().st_size}

    def relocate(spec, destination):
        add(base / spec['path'], destination, spec['sha256'])
        return dict(spec, path=destination)

    provenance = {'source_manifest_sha256': manifest_sha, 'model_metadata': {}}
    manifest = copy.deepcopy(manifest)
    for role, spec in manifest['models'].items():
        manifest['models'][role] = relocate(spec, 'models/' + role + '.bmodel')
        # Compiler metadata identifies provenance, not a serving-time file.
        if 'metadata_source' in manifest['models'][role]:
            provenance['model_metadata'][role] = manifest['models'][role].pop('metadata_source')
    for name, expected in manifest['cpu_files'].items():
        if Path(name).name != name:
            raise ValueError('invalid_cpu_asset_name')
        add(base / manifest['cpu_directory'] / name, 'cpu/' + name, expected)
    manifest['cpu_directory'] = 'cpu'
    for name, spec in manifest['implementation'].items():
        if Path(name).name != name:
            raise ValueError('invalid_implementation_name')
        manifest['implementation'][name] = relocate(spec, 'runtime/' + name)
    for key, destination in [('tokenizer', 'tokenizer/tokenizer.json'),
                             ('cpu_helper', 'cpu/laya_cpu.py'),
                             ('library', 'lib/liblaya_bmrt_bridge.so')]:
        manifest[key] = relocate(manifest[key], destination)
    add(engine, 'bin/cosmo-engine', engine_sha)
    for root, prefix in [(web, 'web')] + [(Path(p), 'pydeps') for p in dependency_roots]:
        if not root.is_dir():
            raise ValueError('missing_source_directory:' + str(root))
        for source in root.rglob('*'):
            if '__pycache__' in source.parts or source.suffix == '.pyc':
                continue
            if source.is_symlink():
                raise ValueError('symlink_in_release_input:' + str(source))
            if source.is_file():
                add(source, prefix + '/' + str(source.relative_to(root)))
    # Only copying costs new space when immutable assets share a filesystem.
    release.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    device = release.parent.stat().st_dev
    large = {'.bmodel', '.npy', '.npz'}
    copied = sum(v['bytes'] for v in files.values()
                 if v['source'].stat().st_dev != device or v['source'].suffix not in large)
    if shutil.disk_usage(release.parent).free < copied + 512 * 1024 * 1024:
        raise RuntimeError('insufficient_release_storage')
    os.umask(0o077)
    release.mkdir(mode=0o700)
    for name, spec in files.items():
        target = release / name
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        if spec['source'].stat().st_dev == device and spec['source'].suffix in large:
            os.link(spec['source'], target)
        else:
            shutil.copyfile(spec['source'], target)
    (release / 'bin/cosmo-engine').chmod(0o755)
    (release / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (release / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    inventory = [{'path': str(p.relative_to(release)), 'sha256': digest(p), 'bytes': p.stat().st_size}
                 for p in sorted(release.rglob('*')) if p.is_file()]
    receipt = {'schema': 1, 'backend': 'typed-visual-v1', 'release': release.name,
               'engine_sha256': engine_sha, 'engine_source_commit': engine_commit,
               'manifest_sha256': digest(release / 'manifest.json'),
               'source_manifest_sha256': manifest_sha, 'files': inventory,
               'automatic_filtering': False, 'database_journal_mode': 'WAL',
               'standard_ota': False}
    (release / 'release.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-manifest', required=True, type=Path)
    parser.add_argument('--source-sha256', required=True)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--engine-sha256', required=True)
    parser.add_argument('--engine-commit', required=True)
    parser.add_argument('--web', required=True, type=Path)
    parser.add_argument('--python-deps', action='append', required=True, type=Path)
    parser.add_argument('--release', required=True, type=Path)
    args = parser.parse_args()
    result = prepare(args.source_manifest, args.source_sha256, args.engine, args.engine_sha256,
                     args.engine_commit, args.web, args.python_deps, args.release)
    print(json.dumps({k: result[k] for k in ['release', 'manifest_sha256', 'engine_sha256']}))


if __name__ == '__main__':
    main()

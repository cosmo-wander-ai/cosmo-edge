#!/usr/bin/env python3
"""Package converted Laya assets for Cosmo's ordinary model repository import.

This is an offline packaging tool. Device inference uses the C++ Laya pipeline
and has no Python, socket, or separate service dependency.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tarfile

import numpy as np


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--cpu', type=Path, required=True)
    parser.add_argument('--tokenizer', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--model-code', default='7200001')
    parser.add_argument('--version', default='V1')
    parser.add_argument('--archive', action='store_true')
    args = parser.parse_args()
    if not args.model_code.isdigit() or not args.version.isalnum():
        raise ValueError('invalid model code/version')
    output = args.output / ('prod_BM1688_' + args.model_code + '_LayaVNative_' + args.version)
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    template = Path(__file__).resolve().parents[2] / 'data/resource/aiboxresource_bm1688/model_template/laya_v.json'
    config = json.loads(template.read_text())
    config.update(algorithm_code=args.model_code, version=args.version, name='Laya-V Native')
    paths = [args.models / (role + '.bmodel') for role in ('tower', 'adapter', 'decision')]
    sizes = [p.stat().st_size for p in paths]
    if not all(sizes):
        raise ValueError('empty submodel')
    # Same CENN v1 layout as BuildPlainNnHeader / BmodelTool::ConvertToNn.
    with (output / 'model.nn').open('wb') as target:
        target.write(struct.pack('<4sHHII8Q', b'CENN', 1, 80, 3, 0, *(sizes + [0] * 5)))
        for path in paths:
            with path.open('rb') as source:
                shutil.copyfileobj(source, target, 4 * 1024 * 1024)
    shutil.copyfile(args.tokenizer, output / 'tokenizer.json')
    shutil.copyfile(args.cpu / 'token_embeddings.f16.npy', output / 'token_embeddings.f16.npy')
    shutil.copyfile(args.cpu / 'rl_agent_config.json', output / 'laya_config.json')
    (output / 'heads').mkdir(mode=0o700)
    with np.load(args.cpu / 'cpu_heads.npz', allow_pickle=False) as heads:
        for name in heads.files:
            np.save(output / 'heads' / (name + '.npy'), np.asarray(heads[name], dtype='<f4'), allow_pickle=False)
    (output / 'config.json').write_text(json.dumps(config, ensure_ascii=False, indent=2) + '\n')
    inventory = {str(p.relative_to(output)): {'bytes': p.stat().st_size, 'sha256': digest(p)}
                 for p in sorted(output.rglob('*')) if p.is_file()}
    (output / 'native-assets.json').write_text(json.dumps({'schema': 1, 'runtime': 'cosmo-engine/laya_v',
                                                         'files': inventory}, indent=2) + '\n')
    if args.archive:
        archive = output.with_suffix('.tar.gz')
        with tarfile.open(archive, 'w:gz', compresslevel=1) as target:
            target.add(output, arcname=output.name)
        print(json.dumps({'model_directory': str(output), 'archive': str(archive), 'sha256': digest(archive)}))
    else:
        print(json.dumps({'model_directory': str(output)}))


if __name__ == '__main__':
    main()

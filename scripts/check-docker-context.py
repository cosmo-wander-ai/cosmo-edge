#!/usr/bin/env python3
"""Measure real Docker ignore behavior with scratch-only context fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def check_context(dockerfile, included, excluded):
    with tempfile.TemporaryDirectory(prefix='cosmo-context-') as directory:
        context = Path(directory) / 'context'
        output = Path(directory) / 'export'
        context.mkdir()
        shutil.copy2(root / '.dockerignore', context / '.dockerignore')
        if dockerfile != 'Dockerfile':
            shutil.copy2(root / f'{dockerfile}.dockerignore', context / f'{dockerfile}.dockerignore')
        for name in (*included, *excluded):
            path = context / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture\n')
        (context / dockerfile).write_text('FROM scratch\nCOPY . /\n')
        subprocess.run(['docker', 'buildx', 'build', '--network=none', '-f', str(context / dockerfile),
                        '--output', f'type=local,dest={output}', str(context)], check=True)
        for name in included:
            assert (output / name).is_file(), f'required context input excluded: {name}'
        for name in excluded:
            assert not (output / name).exists(), f'private/unrelated context input leaked: {name}'


check_context('Dockerfile', ['src/public.txt'], [
    'output/agent-runs/private.txt', 'build_output/private.txt', 'build_rknn/object.o',
    '3rd/srs-6.0-r0/trunk/objs/private.o', '3rd/srs-next/trunk/objs/private.o',
])
check_context('Dockerfile.rockchip', [
    'config/rknn/platforms/rk3576.json', 'config/rockchip-build/builder-lock.json',
    'config/rockchip-media/runtime-lock.json', 'scripts/install_rkllm_sdk.py',
    'tools/rknn/media_sysroot_lock.py',
], [
    'output/agent-runs/private.txt', 'build_output/private.txt', 'build_rknn/object.o',
    '3rd/srs-6.0-r0/trunk/objs/private.o', 'src/private.cc', 'config/unrelated.json',
    'scripts/unrelated.sh', 'tools/rknn/unrelated.py', 'README.md',
])
print('Real Docker contexts preserve required inputs and exclude private/unrelated outputs')

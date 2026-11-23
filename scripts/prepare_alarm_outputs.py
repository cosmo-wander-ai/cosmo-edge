#!/usr/bin/env python3
"""Prepare board-local AlarmOut mapping after the engine has stopped.

Never infer wiring from the SoC alone. Unknown boards require an administrator's
explicit alarmOutputs.json. This helper does not export pins or change pinmux.
"""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile


def safe_destination(path):
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError("refusing symlink configuration destination")
    if path.exists() and not path.is_file():
        raise ValueError("configuration destination is not a regular file")


def atomic_write(path, content, mode=0o600):
    safe_destination(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def validated_outputs(content):
    if len(content) > 65536:
        raise ValueError('board mapping exceeds size limit')
    outputs = json.loads(content)['outputs']
    if not isinstance(outputs, list) or len(outputs) > 64:
        raise ValueError('outputs must be an array of at most 64 channels')
    ids, pins = set(), set()
    for item in outputs:
        ident, gpio, low = item['id'], item['gpio'], item['activeLow']
        if (type(ident) is not int or not 1 <= ident <= 64 or
                type(gpio) is not int or not 0 <= gpio <= 65535 or type(low) is not bool or
                ident in ids or gpio in pins):
            raise ValueError('invalid or duplicate output mapping')
        ids.add(ident)
        pins.add(gpio)
    return outputs


def known_board(root):
    """Recognize the four-PhotoMOS GPIO3_A0..A3 BSP, not generic RK3576."""
    compatible = root / 'proc/device-tree/compatible'
    startup = root / 'etc/rc.local'
    if not compatible.is_file() or not startup.is_file():
        return None
    tokens = compatible.read_bytes().split(b'\0')
    if not {b'rockchip,rk3576-evb1-v10', b'rockchip,rk3576'}.issubset(tokens):
        return None
    original = startup.read_bytes()
    updated = original
    for i in range(4):
        # Require exact BSP relay labels and one executable value assignment.
        if not re.search(rb'(?m)^\s*#\s*RLY_CTL' + str(i).encode() +
                         rb'_GPIO3_A' + str(i).encode() + rb'\s*$', original):
            return None
        pattern = (rb'(?m)^(\s*echo\s+)[01](\s*>\s*/sys/class/gpio/gpio' +
                   str(96 + i).encode() + rb'/value[ \t]*)$')
        updated, count = re.subn(pattern, rb'\g<1>0\g<2>', updated)
        if count != 1:
            return None
    return startup, original, updated


def prepare(data_dir, root=Path('/')):
    mapping = data_dir / 'conf/linkAge/alarmOutputs.json'
    safe_destination(mapping)
    board = None
    create = not mapping.exists()
    if create:
        board = known_board(root)
        if board is None:
            return 'AlarmOut: no board mapping; automatic wiring detection unavailable'
        outputs = [{'id': i + 1, 'gpio': 96 + i, 'activeLow': False} for i in range(4)]
    else:
        # Preserve explicit mappings, including an intentionally empty list.
        with mapping.open('rb') as stream:
            outputs = validated_outputs(stream.read(65537))

    # Complete all checks before the first write. GPIO sysfs directory links are
    # kernel-owned and expected; never write export/direction/active_low.
    checks = []
    for item in outputs:
        pin = root / f"sys/class/gpio/gpio{item['gpio']}"
        direction = (pin / 'direction').read_text().strip()
        inverted = (pin / 'active_low').read_text().strip()
        if direction != 'out' or inverted not in ('0', '1'):
            raise ValueError(f"AlarmOut {item['id']}: GPIO not initialized as output")
        if create and inverted != '0':
            raise ValueError('automatic board profile requires kernel active_low=0')
        if (pin / 'value').is_symlink():
            raise ValueError('refusing symlink GPIO value')
        # Inactive physical level is HIGH only for active-low wiring.
        value = str(int(item['activeLow'] != (inverted == '1')))
        checks.append((pin, inverted, value))

    if board:
        safe_destination(board[0])
    for pin, inverted, value in checks:
        if ((pin / 'direction').read_text().strip() != 'out' or
                (pin / 'active_low').read_text().strip() != inverted):
            raise ValueError('GPIO mode changed during initialization')
        (pin / 'value').write_text(value)
        if (pin / 'value').read_text().strip() != value:
            raise ValueError('GPIO idle readback failed')
    if create:
        startup, original, updated = board
        mode = startup.stat().st_mode & 0o777
        try:
            atomic_write(startup, updated, mode)
            atomic_write(mapping, (json.dumps({'outputs': outputs}, indent=2) + '\n').encode())
        except Exception:
            atomic_write(startup, original, mode)
            raise
    return f'AlarmOut: {len(outputs)} configured outputs initialized inactive'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(prepare(args.data_dir))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'AlarmOut unavailable: {error}\n')

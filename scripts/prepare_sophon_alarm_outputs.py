#!/usr/bin/env python3
"""Initialize an explicitly selected four-PhotoMOS carrier, never a generic SoC.

Only GPIO export, inactive output initialization and missing application mapping
are written. Pinmux is inspected, never changed. Run after stopping the engine.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

PROFILE = 'sophon-four-relay-v1'
# Linux GPIO, controller label/base, native pad name, SoC GPIO function.
PINS = ((389, '27013000.gpio', 384, 'IIC4_SDA', 101),
        (390, '27013000.gpio', 384, 'IIC4_SCL', 102),
        (480, '27010000.gpio', 480, 'I2S0_SCLK', 0),
        (347, '27015000.gpio', 320, 'PAD_MIPI1_TX1P', 187))
OUTPUTS = [{'id': i + 1, 'gpio': p[0], 'activeLow': False}
           for i, p in enumerate(PINS)]


class Board:
    def __init__(self, root=Path('/'), pin_reader=None):
        self.root = root
        self.sysfs = root / 'sys/class/gpio'
        self.pin_reader = pin_reader or self.read_pin

    def read_pin(self, name):
        return subprocess.run(['/usr/sbin/cvi_pinmux', '-r', name], check=True,
                              capture_output=True, text=True, timeout=5).stdout

    def preflight(self):
        compatible = (self.root / 'proc/device-tree/compatible').read_bytes().split(b'\0')
        if b'cvitek,cv181x' not in compatible:
            raise ValueError('incompatible platform for selected carrier profile')
        occupancy = (self.root / 'sys/kernel/debug/gpio').read_text()
        for gpio, label, base, pad, function in PINS:
            controller = self.sysfs / f'gpiochip{base}'
            if ((controller / 'label').read_text().strip() != label or
                    int((controller / 'base').read_text()) != base or
                    int((controller / 'ngpio').read_text()) != 32):
                raise ValueError(f'GPIO controller layout mismatch for {gpio}')
            current = re.findall(r'^\[v\]\s+(\S+)\s*$', self.pin_reader(pad), re.M)
            if current != [f'GPIO{function}']:
                raise ValueError(f'{pad} is not in GPIO mode; board firmware setup required')
            pin = self.sysfs / f'gpio{gpio}'
            if pin.exists():
                if ((pin / 'direction').read_text().strip() != 'out' or
                        (pin / 'active_low').read_text().strip() != '0'):
                    raise ValueError(f'existing GPIO{gpio} has incompatible direction/polarity')
            elif re.search(rf'\bgpio-{gpio}\s+\(', occupancy):
                raise ValueError(f'GPIO{gpio} is already owned by another consumer')

    def export(self, gpio):
        (self.sysfs / 'export').write_text(str(gpio))

    def wait_ready(self, gpio):
        pin = self.sysfs / f'gpio{gpio}'
        deadline = time.monotonic() + 1
        while not (pin / 'direction').exists():
            if time.monotonic() >= deadline:
                raise TimeoutError(f'GPIO{gpio} export timeout')
            time.sleep(0.01)

    def inactive(self, gpio, new=False):
        pin = self.sysfs / f'gpio{gpio}'
        if (pin / 'active_low').read_text().strip() != '0':
            raise ValueError(f'GPIO{gpio} kernel polarity changed')
        # direction=low atomically configures a newly exported output inactive.
        (pin / ('direction' if new else 'value')).write_text('low' if new else '0')
        if ((pin / 'direction').read_text().strip() != 'out' or
                (pin / 'value').read_text().strip() != '0'):
            raise ValueError(f'GPIO{gpio} idle readback failed')

    def release(self, gpio):
        direction = self.sysfs / f'gpio{gpio}/direction'
        if direction.exists() and direction.read_text().strip() == 'out':
            self.inactive(gpio)
        (self.sysfs / 'unexport').write_text(str(gpio))


def mapping_content(mapping):
    if mapping.is_symlink():
        raise ValueError('refusing symlink mapping')
    if not mapping.exists():
        return None
    with mapping.open('rb') as stream:
        raw = stream.read(65537)
    if len(raw) > 65536:
        raise ValueError('mapping too large')
    outputs = json.loads(raw)['outputs']
    if outputs == []:
        return []  # Explicit administrative disable must survive upgrades.
    if not isinstance(outputs, list) or len(outputs) != len(OUTPUTS):
        raise ValueError('existing mapping differs; preserving it without GPIO writes')
    for item in outputs:
        if (not isinstance(item, dict) or type(item.get('id')) is not int or
                type(item.get('gpio')) is not int or type(item.get('activeLow')) is not bool):
            raise ValueError('invalid existing mapping')
    if sorted(outputs, key=lambda item: item['id']) != OUTPUTS:
        raise ValueError('existing mapping differs; preserving it without GPIO writes')
    return outputs


def prepare(data_dir, profile, board=None, check=False):
    if profile != PROFILE:
        raise ValueError('unknown board profile')
    mapping = data_dir / 'conf/linkAge/alarmOutputs.json'
    for parent in (mapping, mapping.parent, mapping.parent.parent):
        if parent.is_symlink():
            raise ValueError('refusing symlink configuration path')
    existing = mapping_content(mapping)
    if existing == []:
        return 'AlarmOut: explicitly disabled; preserved'
    board = board or Board()
    board.preflight()  # Validate every pin before the first mutation.
    if check:
        return 'AlarmOut: four-output carrier preflight passed (no writes)'
    created, initialized = [], []
    temporary = None
    try:
        for item in OUTPUTS:
            gpio = item['gpio']
            new = not (board.sysfs / f'gpio{gpio}').exists()
            if new:
                board.export(gpio)
                created.append(gpio)
                board.wait_ready(gpio)
            board.inactive(gpio, new)
            initialized.append(gpio)
        if existing is None:
            mapping.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix='.alarmOutputs-', dir=mapping.parent)
            with os.fdopen(fd, 'w') as stream:
                json.dump({'outputs': OUTPUTS}, stream, indent=2)
                stream.write('\n')
                stream.flush()
                os.fsync(stream.fileno())
            # Publish without overwriting a concurrently created configuration.
            os.link(temporary, mapping)
        return 'AlarmOut: channels 1-4 initialized inactive (GPIO389,390,480,347; active-high)'
    except Exception:
        for gpio in reversed(initialized):
            try:
                board.inactive(gpio)
            except Exception:
                pass
        failures = []
        for gpio in reversed(created):
            try:
                board.release(gpio)
            except Exception as error:
                failures.append(f'{gpio}: {error}')
        if failures:
            print('AlarmOut cleanup incomplete: ' + '; '.join(failures), flush=True)
        raise
    finally:
        if temporary is not None:
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--profile', required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        if args.check:
            print(prepare(args.data_dir, args.profile, check=True))
        else:
            with open('/run/lock/cosmo-alarmout-init.lock', 'a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                print(prepare(args.data_dir, args.profile))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'AlarmOut initialization failed: {error}\n')


if __name__ == '__main__':
    main()

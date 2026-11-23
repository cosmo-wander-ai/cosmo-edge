#!/usr/bin/env python3
"""Carrier initialization contracts without physical GPIO access."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'startup', Path(__file__).resolve().parents[1] / 'scripts/prepare_sophon_alarm_outputs.py')
startup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(startup)


class FakeBoard(startup.Board):
    def __init__(self, root):
        self.calls = []
        self.fail_gpio = None
        self.busy_gpio = None
        super().__init__(root, lambda pad: '[v] GPIO' + str(
            next(p[4] for p in startup.PINS if p[3] == pad)))
        (root / 'proc/device-tree').mkdir(parents=True)
        (root / 'proc/device-tree/compatible').write_bytes(b'cvitek,cv181x\0')
        (root / 'sys/kernel/debug').mkdir(parents=True)
        (root / 'sys/kernel/debug/gpio').write_text('')
        for _, label, base, _, _ in startup.PINS:
            chip = self.sysfs / f'gpiochip{base}'
            chip.mkdir(parents=True, exist_ok=True)
            for name, value in [('label', label), ('base', str(base)), ('ngpio', '32')]:
                (chip / name).write_text(value)

    def export(self, gpio):
        self.calls.append(('export', gpio))
        if gpio == self.busy_gpio:
            raise OSError('claimed concurrently')
        pin = self.sysfs / f'gpio{gpio}'
        pin.mkdir()
        for name, value in [('direction', 'in'), ('active_low', '0'), ('value', '0')]:
            (pin / name).write_text(value)

    def inactive(self, gpio, new=False):
        self.calls.append(('inactive', gpio))
        if gpio == self.fail_gpio:
            raise OSError('injected initialization failure')
        pin = self.sysfs / f'gpio{gpio}'
        if new:
            (pin / 'direction').write_text('out')
        super().inactive(gpio, False)

    def release(self, gpio):
        self.calls.append(('release', gpio))
        pin = self.sysfs / f'gpio{gpio}'
        for item in pin.iterdir():
            item.unlink()
        pin.rmdir()


class StartupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / 'data'
        self.mapping = self.data / 'conf/linkAge/alarmOutputs.json'
        self.board = FakeBoard(self.root)

    def run_prepare(self, **kwargs):
        return startup.prepare(self.data, startup.PROFILE, self.board, **kwargs)

    def write_mapping(self, outputs):
        self.mapping.parent.mkdir(parents=True)
        self.mapping.write_text(json.dumps({'outputs': outputs}))

    def test_create_and_restart_preserve_mapping(self):
        self.run_prepare()
        self.assertEqual(json.loads(self.mapping.read_text())['outputs'], startup.OUTPUTS)
        before = self.mapping.read_bytes()
        self.board.calls.clear()
        self.run_prepare()
        self.assertEqual(before, self.mapping.read_bytes())
        self.assertEqual(self.board.calls, [('inactive', x) for x in (389, 390, 480, 347)])
        self.assertFalse((self.board.sysfs / 'gpio484').exists())

    def test_read_only_check(self):
        self.run_prepare(check=True)
        self.assertEqual(self.board.calls, [])
        self.assertFalse(self.mapping.exists())

    def test_custom_mapping_preserved_without_writes(self):
        self.write_mapping([{'id': 1, 'gpio': 1, 'activeLow': True}])
        before = self.mapping.read_bytes()
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertEqual(before, self.mapping.read_bytes())
        self.assertEqual(self.board.calls, [])

    def test_disabled_mapping_preserved(self):
        self.write_mapping([])
        self.assertIn('disabled', self.run_prepare())
        self.assertEqual(self.board.calls, [])

    def test_wrong_mux_rejects_before_any_export(self):
        self.board.pin_reader = lambda pad: '[v] SPI0_SDI'
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertEqual(self.board.calls, [])

    def test_occupied_last_pin_rejects_before_any_export(self):
        (self.root / 'sys/kernel/debug/gpio').write_text(' gpio-347 (other |busy) out hi')
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertEqual(self.board.calls, [])

    def test_controller_layout_rejected(self):
        (self.board.sysfs / 'gpiochip384/base').write_text('0')
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertEqual(self.board.calls, [])

    def test_partial_failure_rolls_back_and_does_not_publish(self):
        self.board.fail_gpio = 390
        with self.assertRaises(OSError):
            self.run_prepare()
        self.assertFalse(self.mapping.exists())
        self.assertFalse((self.board.sysfs / 'gpio389').exists())
        self.assertFalse((self.board.sysfs / 'gpio390').exists())

    def test_export_race_does_not_release_another_consumer(self):
        self.board.busy_gpio = 390
        with self.assertRaises(OSError):
            self.run_prepare()
        self.assertNotIn(('release', 390), self.board.calls)
        self.assertIn(('release', 389), self.board.calls)

    def test_existing_input_not_reconfigured(self):
        self.board.export(389)
        self.board.calls.clear()
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertEqual(self.board.calls, [])


if __name__ == '__main__':
    unittest.main()

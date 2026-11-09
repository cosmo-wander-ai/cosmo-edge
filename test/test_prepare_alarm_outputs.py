import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'prepare_alarm_outputs', Path(__file__).resolve().parents[1] / 'scripts/prepare_alarm_outputs.py')
board = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board)


class AlarmOutputStartupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / 'userdata'
        self.mapping = self.data / 'conf/linkAge/alarmOutputs.json'
        self.rc = self.root / 'etc/rc.local'
        self.rc.parent.mkdir()
        self.rc.write_bytes(('#!/bin/sh\n' + ''.join(
            f'# RLY_CTL{i}_GPIO3_A{i}\necho 1 > /sys/class/gpio/gpio{96+i}/value\n'
            for i in range(4))).encode())
        compatible = self.root / 'proc/device-tree/compatible'
        compatible.parent.mkdir(parents=True)
        compatible.write_bytes(b'rockchip,rk3576-evb1-v10\0rockchip,rk3576\0')
        for gpio in range(96, 100):
            pin = self.root / f'sys/class/gpio/gpio{gpio}'
            pin.mkdir(parents=True)
            for name, value in [('direction', 'out'), ('active_low', '0'), ('value', '1')]:
                (pin / name).write_text(value)

    def test_known_board_initializes_high_active_outputs_and_boot_idle(self):
        board.prepare(self.data, self.root)
        outputs = json.loads(self.mapping.read_text())['outputs']
        self.assertEqual(outputs, [{'id': i+1, 'gpio': i+96, 'activeLow': False} for i in range(4)])
        self.assertNotIn('echo 1', self.rc.read_text())
        for gpio in range(96, 100):
            self.assertEqual((self.root / f'sys/class/gpio/gpio{gpio}/value').read_text(), '0')

    def test_chip_name_alone_does_not_authorize_wiring(self):
        self.rc.write_text('#!/bin/sh\n')
        self.assertIn('no board mapping', board.prepare(self.data, self.root))
        self.assertFalse(self.mapping.exists())
        self.assertEqual((self.root / 'sys/class/gpio/gpio96/value').read_text(), '1')

    def test_all_pins_checked_before_any_write(self):
        (self.root / 'sys/class/gpio/gpio99/direction').write_text('in')
        with self.assertRaises(ValueError):
            board.prepare(self.data, self.root)
        self.assertFalse(self.mapping.exists())
        self.assertEqual((self.root / 'sys/class/gpio/gpio96/value').read_text(), '1')

    def test_automatic_profile_rejects_inverted_kernel_gpio(self):
        (self.root / 'sys/class/gpio/gpio98/active_low').write_text('1')
        with self.assertRaises(ValueError):
            board.prepare(self.data, self.root)
        self.assertFalse(self.mapping.exists())
        self.assertIn('echo 1', self.rc.read_text())

    def test_partial_bsp_signature_does_not_select_a_profile(self):
        self.rc.write_bytes(self.rc.read_bytes().replace(b'RLY_CTL3_GPIO3_A3', b'OTHER_OUTPUT'))
        self.assertIn('no board mapping', board.prepare(self.data, self.root))
        self.assertFalse(self.mapping.exists())

    def test_existing_custom_mapping_and_kernel_polarity_preserved(self):
        self.mapping.parent.mkdir(parents=True)
        content = b'{"outputs":[{"id":7,"gpio":96,"activeLow":true}]}'
        self.mapping.write_bytes(content)
        (self.root / 'sys/class/gpio/gpio96/active_low').write_text('1')
        original_rc = self.rc.read_bytes()
        board.prepare(self.data, self.root)
        self.assertEqual(self.mapping.read_bytes(), content)
        self.assertEqual(self.rc.read_bytes(), original_rc)
        self.assertEqual((self.root / 'sys/class/gpio/gpio96/value').read_text(), '0')

    def test_empty_mapping_is_not_replaced(self):
        self.mapping.parent.mkdir(parents=True)
        self.mapping.write_text('{"outputs":[]}')
        board.prepare(self.data, self.root)
        self.assertEqual(self.mapping.read_text(), '{"outputs":[]}')

    def test_invalid_mapping_does_not_touch_outputs(self):
        self.mapping.parent.mkdir(parents=True)
        self.mapping.write_text('{"outputs":[{"id":1,"gpio":96,"activeLow":"false"}]}')
        with self.assertRaises(ValueError):
            board.prepare(self.data, self.root)
        self.assertEqual((self.root / 'sys/class/gpio/gpio96/value').read_text(), '1')

    def test_failed_mapping_publish_restores_boot_configuration(self):
        original = self.rc.read_bytes()
        write = board.atomic_write
        def fail_mapping(path, *args):
            if path == self.mapping:
                raise OSError('fixture publish failure')
            return write(path, *args)
        with patch.object(board, 'atomic_write', fail_mapping), self.assertRaises(OSError):
            board.prepare(self.data, self.root)
        self.assertEqual(self.rc.read_bytes(), original)
        self.assertFalse(self.mapping.exists())


if __name__ == '__main__':
    unittest.main()

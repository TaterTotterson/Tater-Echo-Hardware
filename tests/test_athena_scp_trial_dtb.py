import importlib.util
from pathlib import Path
import struct
import unittest

SCRIPT = Path(__file__).resolve().parents[1]/'scripts/athena_scp_trial_dtb.py'
SPEC = importlib.util.spec_from_file_location('athena_trial_dtb', SCRIPT)
trial = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(trial)


class AthenaTrialDTTests(unittest.TestCase):
    def fixture(self):
        before = {('/', 'compatible'): b'amzn,athena\0mediatek,mt8183\0',
                  ('/reserved-memory', '#address-cells'): struct.pack('>I', 2),
                  ('/reserved-memory', '#size-cells'): struct.pack('>I', 2),
                  (trial.SCP, 'status'): b'disabled\0',
                  ('/speaker', 'private-calibration'): b'private-fixture'}
        after = dict(before)
        handle = struct.pack('>I', 0x100)
        after.update({(trial.POOL, 'compatible'): b'shared-dma-pool\0',
                      (trial.POOL, 'reg'): struct.pack('>4I', 0, trial.BASE, 0, trial.SIZE),
                      (trial.POOL, 'no-map'): b'',
                      (trial.POOL, 'phandle'): handle,
                      (trial.SCP, 'status'): b'okay\0',
                      (trial.SCP, 'memory-region'): handle,
                      (trial.SCP, 'firmware-name'): b'tater-athena-scp.img\0',
                      ('/__symbols__', 'tater_scp_trial_pool'): trial.POOL.encode()+b'\0'})
        return before, after

    def test_valid_scope_is_not_hardware_qualification(self):
        report = trial.audit(*self.fixture())
        self.assertTrue(report['scope_valid'])
        self.assertFalse(report['hardware_qualified'])
        self.assertNotIn('private-fixture', str(report))

    def test_rejects_unrelated_changes(self):
        before, after = self.fixture()
        after['/speaker', 'private-calibration'] = b'changed'
        with self.assertRaisesRegex(ValueError, 'unexpected existing'):
            trial.audit(before, after)

    def test_rejects_wrong_board_and_incomplete_trial(self):
        before, after = self.fixture()
        before['/', 'compatible'] = b'amzn,crown\0'
        with self.assertRaises(ValueError):
            trial.audit(before, after)
        before, after = self.fixture()
        del after[trial.POOL, 'no-map']
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            trial.audit(before, after)

    def test_rejects_pool_overlap(self):
        before, after = self.fixture()
        key = '/reserved-memory/existing', 'reg'
        before[key] = after[key] = struct.pack('>QQ', trial.BASE-4096, 8192)
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            trial.audit(before, after)

    def test_rejects_phandle_collision(self):
        before, after = self.fixture()
        before['/existing', 'phandle'] = after['/existing', 'phandle'] = after[trial.POOL, 'phandle']
        with self.assertRaisesRegex(ValueError, 'collides'):
            trial.audit(before, after)

    def test_rejects_malformed_dtb(self):
        for data in (b'', bytes(40), b'\xd0\x0d\xfe\xed'+bytes(36)):
            with self.subTest(data=data[:4]), self.assertRaises(ValueError):
                trial.properties(data)


if __name__ == '__main__':
    unittest.main()

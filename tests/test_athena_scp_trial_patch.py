import hashlib
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1]/'kernel-patches/athena-codec/patch_scp_trial.py'
spec = importlib.util.spec_from_file_location('athena_scp_trial_patch', SCRIPT)
trial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trial)


class AthenaSCPTrialPatchTests(unittest.TestCase):
    def setUp(self):
        self.source = '\n'.join(old for old, _ in trial.REPLACEMENTS)
        self.identity = hashlib.sha256(self.source.encode()).hexdigest()

    def test_unknown_source_rejected(self):
        with self.assertRaisesRegex(ValueError, 'not the reviewed'):
            trial.patch_text(self.source)

    def test_patch_is_complete_and_idempotent(self):
        with patch.object(trial, 'ORIGINAL_SHA256', self.identity):
            result = trial.patch_text(self.source)
            self.assertEqual(result, trial.patch_text(result))
        self.assertIn('rproc->auto_boot = false;', result)
        self.assertIn('rproc->recovery_disabled = true;', result)
        self.assertIn('static bool athena_trial;', result)
        self.assertIn('module_param(athena_trial, bool, 0444);', result)
        self.assertLess(result.index('scp->data->scp_reset_assert(scp);'),
                        result.index('ret = devm_request_threaded_irq'))

    def test_modified_or_partial_patch_rejected(self):
        with patch.object(trial, 'ORIGINAL_SHA256', self.identity):
            result = trial.patch_text(self.source)
            for invalid in (result.replace('auto_boot = false', 'auto_boot = true'), result+'\nchanged'):
                with self.subTest(invalid=invalid[-40:]), self.assertRaises(ValueError):
                    trial.patch_text(invalid)

    def test_quiescing_is_board_specific_and_releases_clock(self):
        self.assertIn('if (scp_is_athena(dev))', trial.HOLD_RESET)
        self.assertIn('clk_prepare_enable(scp->clk)', trial.HOLD_RESET)
        self.assertIn('clk_disable_unprepare(scp->clk)', trial.HOLD_RESET)
        self.assertIn('"amzn,athena"', trial.PARAMETERS)
        self.assertIn('"mediatek,mt8183-scp"', trial.PARAMETERS)


if __name__ == '__main__':
    unittest.main()

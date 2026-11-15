import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROBES = ROOT / "probes/mt8163-audio"
sys.path.insert(0, str(PROBES))
import adc3101_filter_lab as lab
import prepare_adc3101_hpf_trial as prepare


class ADC3101TrialTests(unittest.TestCase):
    def test_helper_uses_the_independent_offline_design(self):
        source = (PROBES / "tater_adc3101_hpf_trial.h").read_text()
        match = re.search(r"candidate\[6\] = \{([^}]+)\}", source)
        self.assertIsNotNone(match)
        actual = bytes(int(word.strip(), 16) for word in match[1].split(","))
        self.assertEqual(actual, lab.coefficient_bytes(lab.quantized_highpass()))
        self.assertIn("#define TATER_ADC_HPF80_DEFAULT 0", source)
        self.assertIn("tater_hpf80, bool, 0444", source)
        self.assertNotIn("request_firmware", source)

    def test_generator_rejects_missing_or_duplicate_anchors(self):
        with self.assertRaisesRegex(ValueError, "unexpected driver anchor"):
            prepare.patched_source("changed source")
        with self.assertRaises(ValueError):
            prepare.replace_once("anchor anchor", "anchor", "new")

    def test_generated_patch_clears_status_before_reset_and_propagates_failure(self):
        # Minimal public-interface anchor fixture, not a vendor source copy.
        source = '\n'.join((
            '#include "tlv320aic3101.h"',
            'static const struct snd_kcontrol_new aic31xx_snd_controls[] = {',
            '};',
            'static void aic31xx_apply_tuning(struct snd_soc_codec *codec)',
            '{', '\t\tsnd_soc_write(codec, ADC_PRB_SELECT(i), 0x2);', '}',
            '\t/* Backup gains and mode settings before reset */',
            '\t/* Program DSP */', '\taic31xx_apply_tuning(codec);', '',
        ))
        result = prepare.patched_source(source)
        self.assertIn('.access = SNDRV_CTL_ELEM_ACCESS_READ', result)
        self.assertIn('return tater_hpf_apply(codec, IDC_enabled != 0);', result)
        self.assertIn('i = aic31xx_apply_tuning(codec);\n\tif (i)\n\t\treturn i;', result)
        self.assertLess(result.index('atomic_set(&tater_hpf_verified, 0);'),
                        result.index('/* Backup gains'))

    def test_changed_official_source_produces_no_output_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            codec = root / "sound/soc/codecs"
            codec.mkdir(parents=True)
            (codec / "tlv320aic3101.c").write_text("wrong")
            (codec / "tlv320aic3101.h").write_text("wrong")
            output = root / "overlay"
            result = subprocess.run([
                sys.executable, str(PROBES / "prepare_adc3101_hpf_trial.py"),
                "--target", "biscuit", "--source-root", str(root),
                "--output", str(output), "--prepare-offline-trial",
            ], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unexpected official codec source hash", result.stderr)
            self.assertFalse(output.exists())

    @unittest.skipUnless(shutil.which("cc"), "host C compiler unavailable")
    def test_exact_kernel_helper_passes_strict_fake_bus_tests(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "selftest"
            subprocess.run([
                "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(PROBES), str(PROBES / "adc3101_hpf_selftest.c"),
                "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("ADC3101 HPF fake-bus tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()

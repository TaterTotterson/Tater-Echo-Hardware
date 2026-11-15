import unittest
import re
from pathlib import Path


class CheckersAudioProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (
            Path(__file__).resolve().parents[1]
            / "probes"
            / "mt8163-audio"
            / "status_probe.sh"
        ).read_text(encoding="utf-8")

    def test_probe_is_target_gated_and_metadata_only(self):
        self.assertIn("--target", self.source)
        self.assertIn("--read-only-status", self.source)
        self.assertIn('project" = "checkers', self.source)
        self.assertIn("/proc/asound/card0/pcm22c/sub0", self.source)
        self.assertIn('"pcm_opened":false', self.source)
        self.assertIn('"samples_read":false', self.source)
        self.assertIn('"audio_saved":false', self.source)
        self.assertNotIn("/dev/snd", self.source)
        self.assertNotIn("arecord ", self.source)
        self.assertNotIn("aplay ", self.source)


class BiscuitMixerProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (
            Path(__file__).resolve().parents[1]
            / "probes/mt8163-audio/biscuit_mixer_probe.c"
        ).read_text(encoding="utf-8")

    def test_explicit_mode_and_exact_target_guard_precede_open(self):
        opened = self.source.index('open("/dev/snd/controlC0"')
        for guard in (
            'strcmp(argv[1], "--read-only-mixer")',
            'strcmp(hostname, "tater-biscuit")',
            'strcmp(kernel.release, "3.18.19-bt")',
        ):
            self.assertLess(self.source.index(guard), opened)
        self.assertIn('strcmp((char *)card.id, "mtsndcard")', self.source)

    def test_only_control_device_and_metadata_ioctls_are_allowed(self):
        self.assertEqual(
            [('"/dev/snd/controlC0"', 'O_RDONLY | O_CLOEXEC')],
            re.findall(r'\bopen\(([^,]+),\s*([^\)]+)\)', self.source),
        )
        self.assertEqual(
            {"SNDRV_CTL_IOCTL_CARD_INFO", "SNDRV_CTL_IOCTL_ELEM_LIST",
             "SNDRV_CTL_IOCTL_ELEM_INFO", "SNDRV_CTL_IOCTL_ELEM_READ"},
            set(re.findall(r'\bioctl\(fd,\s*(\w+)', self.source)),
        )
        self.assertNotRegex(self.source, r'\b(?:write|pwrite|system|popen|mmap)\s*\(')

    def test_inventory_and_values_are_bounded(self):
        self.assertIn("list.count > 4096", self.source)
        self.assertIn("list.used > count", self.source)
        self.assertIn("info.count > 128", self.source)
        self.assertIn("if (!read_value) { putchar('}'); continue; }", self.source)
        self.assertIn("bool read_value = selected(name);", self.source)

    def test_allowlist_has_negative_self_tests(self):
        self.assertIn('selected("ADC_E MICPGA Volume Ctrl")', self.source)
        self.assertIn('selected("power_button_state")', self.source)
        self.assertIn('selected("biquad coefficients")', self.source)
        self.assertIn('strcmp(argv[1], "--self-test")', self.source)


class BiscuitGainTrialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (
            Path(__file__).resolve().parents[1]
            / "probes/mt8163-audio/biscuit_gain_trial.c"
        ).read_text(encoding="utf-8")

    def test_explicit_authorization_and_target_guard_precede_control_open(self):
        opened = self.source.index('open("/dev/snd/controlC0"')
        for guard in ('strcmp(argv[1], "--owner-approved-10db-trial")',
                      'strcmp(hostname, "tater-biscuit")',
                      'strcmp(kernel.release, "3.18.19-bt")'):
            self.assertLess(self.source.index(guard), opened)
        self.assertIn('strcmp((char *)card.id, "mtsndcard")', self.source)
        self.assertIn('O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC', self.source)

    def test_only_gain_pairs_can_be_changed(self):
        self.assertIn('"Digital Volume Control" : "MICPGA Volume Ctrl"', self.source)
        self.assertIn('info.type != SNDRV_CTL_ELEM_TYPE_INTEGER || info.count != 2', self.source)
        self.assertIn('controls[i].original[0] != baseline_value(i)', self.source)
        self.assertIn('controls[i].original[1] != baseline_value(i)', self.source)
        self.assertIn('info.value.integer.max != (i % 2 == DIGITAL ? 64 : 80)', self.source)
        self.assertNotIn('"/dev/i2c', self.source)
        self.assertNotIn('"/dev/snd/pcm', self.source)
        self.assertNotRegex(self.source, r'\b(?:system|popen|mmap|execve)\s*\(')

    def test_deadline_and_catchable_signals_restore_originals(self):
        self.assertIn('TRIAL_SECONDS = 300', self.source)
        self.assertIn('alarm(TRIAL_SECONDS)', self.source)
        for signal in ("SIGTERM", "SIGINT", "SIGHUP", "SIGALRM"):
            self.assertIn(f'sigaction({signal}, &action, NULL)', self.source)
        self.assertIn('int restored = restore(fd);', self.source)
        self.assertIn('write_pair(fd, i, controls[i].original)', self.source)
        self.assertIn('if (!restored) unlink(', self.source)
        self.assertLess(self.source.index('int failed = verify_restore_api(fd);'),
                        self.source.index('if (!failed) failed = apply_trial(fd);'))

    def test_fake_backend_exercises_partial_writes_and_ordering(self):
        self.assertIn('for (int failure = 0; failure < CONTROLS; ++failure)', self.source)
        self.assertIn('fake_values[index][0] = pair[0];', self.source)
        self.assertIn('order[i] != i * 2 || order[i + 4] != i * 2 + 1', self.source)
        self.assertIn('order[i] != i * 2 + 1 || order[i + 4] != i * 2', self.source)
        self.assertIn('stop_requested = 1;', self.source)

    def test_hardware40_requires_a_live_zero_software_gain_trial_daemon(self):
        self.assertIn('"--owner-approved-hardware40-trial"', self.source)
        self.assertIn('if (hardware40 && !hardware40_daemon_ready())', self.source)
        self.assertIn('"MIC_GAIN_DB=0"', self.source)
        self.assertIn('"TATER_BISCUIT_HW_GAIN_TRIAL=40"', self.source)
        self.assertIn('"/tater-echo-hw40-trial"', self.source)
        self.assertIn('if (hardware40) return index % 2 == DIGITAL ? 88 : 80;', self.source)


if __name__ == "__main__":
    unittest.main()

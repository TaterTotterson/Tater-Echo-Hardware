import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ISPProbeSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = ROOT / "probes/mt8163-camera"
        cls.source = (root / "isp_synthetic_probe_android.c").read_text()
        cls.guard = (root / "isp_runtime_probe_android.c").read_text()

    def test_failed_hardware_job_is_quarantined_before_runtime_load(self):
        self.assertIn("#define TATER_ISP_UNSAFE_LAB_JOB 0", self.source)
        refusal = self.source.index("if (process && !TATER_ISP_UNSAFE_LAB_JOB)")
        self.assertLess(refusal, self.source.index("dlopen("))
        self.assertLess(refusal, self.source.index("memory_open(&input"))

    def test_exact_target_kernel_and_word_size_are_required(self):
        self.assertIn('strcmp(token, "androidboot.product=cronos")', self.guard)
        self.assertIn('strcmp(kernel.release, "4.9.337-g8d928c5176cc")', self.guard)
        self.assertIn("sizeof(void *) == 4", self.guard)
        self.assertIn("sizeof(struct port_id)==16", self.source)

    def test_no_sensor_or_handwritten_register_write(self):
        self.assertNotIn('open("/dev/kd_camera', self.source)
        self.assertNotIn('open("/dev/mem', self.source)
        self.assertNotIn("write_register", self.source)
        self.assertNotIn("read_register", self.source)
        self.assertNotIn("_IOW('g',11", self.source)

    def test_shadow_reader_excludes_physical_register_mappings(self):
        self.assertIn("shadow_mapping_allowed(shadow)", self.source)
        self.assertIn('!strstr(line,"/dev/camera-isp")', self.source)
        self.assertIn('!strstr(line,"/dev/mem")', self.source)
        self.assertIn("start+0x4244U<=high", self.source)

    def test_crop_and_dequeue_selectors_do_not_mix_timeout_with_burst(self):
        self.assertIn("words[0x8c/4]=W; words[0x90/4]=H;", self.source)
        self.assertIn("dequeue_input(pipe,input_id,completed_input,0,3,0,0,1000)", self.source)
        self.assertIn("dequeue(pipe,output_id,completed_buffer,0,3,0,0,1000)", self.source)
        self.assertNotIn("completed_buffer,0,3,0,1000,0)", self.source)

    def test_changed_bytes_cannot_be_a_qualification_pass(self):
        self.assertIn("color_and_dma_qualification_pending=true", self.source)
        self.assertIn('puts("TATER_ISP_SYNTHETIC color_and_dma_qualification_pending=true");\n        goto cleanup;', self.source)


if __name__ == "__main__":
    unittest.main()

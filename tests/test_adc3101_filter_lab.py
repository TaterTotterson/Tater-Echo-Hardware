import importlib.util
import math
from pathlib import Path
import unittest


LAB = Path(__file__).resolve().parents[1] / "probes/mt8163-audio/adc3101_filter_lab.py"
SPEC = importlib.util.spec_from_file_location("adc3101_filter_lab", LAB)
lab = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lab)


class ADC3101FilterLabTests(unittest.TestCase):
    def test_candidate_is_stable_exact_dc_zero_and_never_boosts(self):
        words = lab.quantized_highpass()
        self.assertTrue(0 < words[2] < lab.Q15)
        self.assertEqual(words[0], -words[1])
        self.assertEqual(0, lab.one_pole_response(words, 0))
        for hz in range(1, 8001):
            self.assertLessEqual(abs(lab.one_pole_response(words, hz)), 1)

    def test_quantization_tracks_existing_software_filter(self):
        result = lab.candidate()
        self.assertLess(result["maximum_response_error_db_1hz_to_nyquist"], 0.01)
        corner_db = lab.db(lab.one_pole_response(lab.quantized_highpass(), 80))
        self.assertLess(abs(corner_db + 3.01), 0.01)

    def test_coefficients_round_trip_signed_big_endian(self):
        words = lab.quantized_highpass()
        payload = lab.coefficient_bytes(words)
        self.assertEqual(6, len(payload))
        self.assertEqual(words, tuple(lab.signed16(int.from_bytes(payload[i:i+2], "big"))
                                     for i in (0, 2, 4)))
        with self.assertRaises(ValueError):
            lab.coefficient_bytes((32768,))

    def test_streamed_impulse_and_dc_decay_are_bounded(self):
        n0, n1, d1 = (v / lab.Q15 for v in lab.quantized_highpass())
        previous_x, previous_y = 0, 0
        impulse_sum = 0
        for i in range(16000):
            x = 1 if i == 0 else 0
            y = n0 * x + n1 * previous_x + d1 * previous_y
            self.assertTrue(math.isfinite(y))
            self.assertLessEqual(abs(y), 1)
            previous_x, previous_y = x, y
            impulse_sum += y
        self.assertLess(abs(impulse_sum), 1e-12)
        previous_x, previous_y = 0, 0
        for _ in range(16000):
            y = n0 + n1 * previous_x + d1 * previous_y
            previous_x, previous_y = 1, y
        self.assertLess(abs(previous_y), 1e-12)

    def test_source_hash_mismatch_refuses_instead_of_guessing(self):
        with self.assertRaisesRegex(ValueError, "unexpected driver/header"):
            lab.audit_source(b"changed source", b"changed header", "biscuit")
        with self.assertRaisesRegex(ValueError, "unqualified target"):
            lab.audit_source(b"", b"", "rook")

    def test_offline_tool_has_no_hardware_or_network_access(self):
        source = LAB.read_text()
        for forbidden in ("/dev/", "subprocess", "ctypes", "socket", "i2c_transfer", "write_bytes", "write_text"):
            self.assertNotIn(forbidden, source)
        self.assertEqual(0, lab.candidate()["hardware_writes"])


if __name__ == "__main__":
    unittest.main()

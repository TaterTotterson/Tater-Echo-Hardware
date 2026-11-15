import importlib.util
import re
import stat
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("serial_trace_capture", ROOT / "scripts/serial_trace_capture.py")
MODULE = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeSerial:
    def __init__(self, completed=True, payload=b"TATER_ISP_TEST harmless=true\n"):
        self.completed = completed
        self.payload = payload
        self.chunks = []
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        match = re.search(rb"b=([^;]+); e=([^;]+);", data)
        self.chunks = [data, match[1] + b"\r\n", self.payload]
        if self.completed:
            self.chunks += [match[2][:8], match[2][8:] + b"\r\n"]

    def read(self):
        return self.chunks.pop(0) if self.chunks else b""


class ISPTraceTests(unittest.TestCase):
    def test_partial_output_survives_timeout_without_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.log"
            serial = FakeSerial(completed=False)
            result = MODULE.capture_exchange(serial, "echo harmless", path, timeout=0.05)
            self.assertFalse(result.completed)
            self.assertTrue(result.timed_out)
            self.assertIn("harmless=true", result.output)
            self.assertIn(b"harmless=true", path.read_bytes())
            self.assertEqual(len(serial.writes), 1)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

    def test_split_end_marker_and_echo_do_not_confuse_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            result = MODULE.capture_exchange(FakeSerial(), "echo harmless", Path(directory) / "trace.log")
            self.assertTrue(result.completed)
            self.assertFalse(result.timed_out)
            self.assertEqual(result.output, "TATER_ISP_TEST harmless=true")

    def test_log_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.log"
            path.touch()
            serial = FakeSerial()
            with self.assertRaises(FileExistsError):
                MODULE.capture_exchange(serial, "echo harmless", path)
            self.assertEqual(serial.writes, [])

    def test_log_limit_cannot_be_a_success(self):
        with tempfile.TemporaryDirectory() as directory:
            serial = FakeSerial(completed=False, payload=b"x" * 2048)
            path = Path(directory) / "trace.log"
            result = MODULE.capture_exchange(serial, "echo harmless", path, max_bytes=256)
            self.assertTrue(result.limit_reached)
            self.assertFalse(result.completed)
            self.assertEqual(path.stat().st_size, 256)

    def test_kernel_reader_has_no_camera_or_kernel_write_path(self):
        source = (ROOT / "probes/mt8163-camera/isp_kmsg_trace.c").read_text()
        self.assertIn('open("/dev/kmsg",O_RDONLY|O_NONBLOCK|O_CLOEXEC)', source)
        self.assertIn("lseek(fd,0,SEEK_END)", source)
        self.assertNotIn("ioctl(", source)
        self.assertNotIn("/dev/camera", source)
        self.assertNotIn("/dev/mem", source)
        self.assertIn("emitted>=128", source)


if __name__ == "__main__":
    unittest.main()

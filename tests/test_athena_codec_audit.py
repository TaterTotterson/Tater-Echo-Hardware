import gzip
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1]/'scripts/audit_athena_codec.py'
spec = importlib.util.spec_from_file_location('athena_codec_audit', SCRIPT)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def elf32(paddr=0x50000000, filesz=4, memsz=4):
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    header = struct.pack('<HHIIIIIHHHHHH', 2, 40, 1, 0, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    program = struct.pack('<8I', 1, 84, 0x10000000, paddr, filesz, memsz, 5, 0x10000)
    return ident + header + program + bytes(4)


class AthenaCodecAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, relative, data):
        path = self.root/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fixture(self, ready=False):
        (self.root/'sys/bus/platform/devices').mkdir(parents=True)
        dt = 'sys/firmware/devicetree/base/'
        self.write(dt+'compatible', b'amzn,athena\0mediatek,mt8183\0')
        for name, address, compatible in (
            ('scp', '10500000', 'mediatek,mt8183-scp'),
            ('vcodec', '17020000', 'mediatek,mt8183-vcodec-enc'),
            ('video-codec', '16020000', 'mediatek,mt8183-vcodec-dec'),
        ):
            self.write(dt+'soc/'+name+'@'+address+'/compatible', compatible.encode()+b'\0')
            if ready:
                driver = self.root/'sys/bus/platform/devices'/(address+'.'+name)/'driver'
                driver.parent.mkdir(parents=True)
                driver.symlink_to('/sys/bus/platform/drivers/example')
        if ready:
            self.write(dt+'soc/scp@10500000/memory-region', struct.pack('>I', 10))
        else:
            self.write(dt+'soc/scp@10500000/status', b'disabled\0')
        config = '\n'.join(key+'=y' for key in audit.CONFIG_KEYS if ready or key != 'CONFIG_VIDEO_MEDIATEK_VCODEC')
        if not ready:
            config += '\n# CONFIG_VIDEO_MEDIATEK_VCODEC is not set\n'
        self.write('proc/config.gz', gzip.compress(config.encode()))

    def test_current_kernel_does_not_claim_codec_support(self):
        self.fixture()
        report = audit.audit(self.root)
        self.assertFalse(report['hardware_codec_qualified'])
        self.assertIn('SCP disabled in device tree', report['blockers'])
        self.assertIn('kernel configuration lacks CONFIG_VIDEO_MEDIATEK_VCODEC', report['blockers'])
        self.assertIn('SCP has no declared reserved-memory region', report['blockers'])

    def test_prerequisites_never_imply_qualification(self):
        self.fixture(ready=True)
        report = audit.audit(self.root)
        self.assertEqual(report['blockers'], [])
        self.assertFalse(report['hardware_codec_qualified'])

    def test_wrong_device_is_rejected(self):
        self.fixture()
        self.write('sys/firmware/devicetree/base/compatible', b'amzn,crown\0mediatek,mt8163\0')
        with self.assertRaisesRegex(ValueError, 'not an Athena'):
            audit.audit(self.root)

    def test_unknown_config_is_not_treated_as_enabled(self):
        self.fixture()
        self.write('proc/config.gz', b'CONFIG_VIDEO_DEV=y\n')
        report = audit.audit(self.root)
        self.assertIn('kernel configuration lacks CONFIG_MTK_SCP', report['blockers'])

    def test_module_counts_as_configured_but_not_bound(self):
        self.fixture()
        self.write('proc/config.gz', b'CONFIG_VIDEO_MEDIATEK_VCODEC=m\n')
        report = audit.audit(self.root)
        self.assertNotIn('kernel configuration lacks CONFIG_VIDEO_MEDIATEK_VCODEC', report['blockers'])
        self.assertIn('encoder driver is not bound', report['blockers'])

    def test_duplicate_node_rejected(self):
        self.fixture()
        self.write('sys/firmware/devicetree/base/soc/another@10500000/compatible', b'unknown\0')
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            audit.audit(self.root)

    def test_incomplete_runtime_snapshot_is_rejected(self):
        self.fixture()
        (self.root/'sys/bus/platform/devices').rmdir()
        with self.assertRaisesRegex(ValueError, 'lacks platform driver'):
            audit.audit(self.root)

    def test_elf_reports_physical_not_virtual_load_address(self):
        path = self.write('candidate.img', elf32())
        report = audit.scp_elf_contract(path)
        self.assertEqual(report['load_segments'][0]['physical_address'], 0x50000000)
        self.assertEqual(report['load_segments'][0]['virtual_address'], 0x10000000)
        self.assertEqual(report['load_segments'][0]['storage'], 'dram')
        self.assertFalse(report['safe_to_load'])
        self.assertEqual(report['driver_dram_allocation_bytes'], 5*1024*1024)
        self.assertEqual(report['coherent_pool_minimum_bytes'], 8*1024*1024)
        self.assertEqual(path.read_bytes(), elf32())

    def test_rejects_truncated_and_wrong_abi(self):
        for data in (b'', elf32()[:60], b'\x7fELF\x02'+elf32()[5:]):
            with self.subTest(data=data[:8]), self.assertRaises(ValueError):
                audit.scp_elf_contract(self.write('bad.img', data))

    def test_rejects_invalid_load_range(self):
        for data in (elf32(filesz=5), elf32(memsz=3), elf32(paddr=0xfffffff0, memsz=32),
                     elf32(paddr=0x7fffe, memsz=4)):
            with self.subTest(data=data[64:80]), self.assertRaises(ValueError):
                audit.scp_elf_contract(self.write('bad.img', data))

    def test_empty_status_does_not_enable_scp(self):
        self.fixture(ready=True)
        self.write('sys/firmware/devicetree/base/soc/scp@10500000/status', b'')
        self.assertFalse(audit.audit(self.root)['nodes']['scp']['dt_enabled'])


if __name__ == '__main__':
    unittest.main()

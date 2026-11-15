"""Offline safety-contract regression checks; never opens hardware."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]/'kernel-patches/athena-codec/ap'

class AthenaAPSourceTests(unittest.TestCase):
    def sources(self):
        return [(ROOT/name).read_text() for name in ('athena_ap.c','athena_ap_vdec.c')]

    def test_modules_remain_explicit_exact_target_trials(self):
        for source in self.sources():
            for guard in ('!allow_ap_trial','of_machine_is_compatible("amzn,athena")',
                          'of_machine_is_compatible("mediatek,mt8183")','init_utsname()->version',
                          'CAP_SYS_RAWIO','.suppress_bind_attrs=true'):
                self.assertIn(guard,source)
            self.assertNotIn('MODULE_DEVICE_TABLE',source) # no modalias autoload

    def test_bounded_owned_dma_and_exclusive_control(self):
        for source in self.sources():
            for guard in ('#define MAX_BUFFER (8UL * 1024 * 1024)',
                          '#define MAX_TOTAL (64UL * 1024 * 1024)',
                          'dma_alloc_coherent','dma_free_coherent','anon_inode_getfile',
                          'atomic_cmpxchg(&ap->opened, 0, 1)','upper_32_bits(buffer->dma)',
                          'atomic_long_read(&ap->allocated)'):
                self.assertIn(guard,source)

    def test_power_off_precedes_retired_dma_free(self):
        for source in self.sources():
            off=source.index('clk_disable_unprepare(ap->clk)')
            self.assertGreater(source.index('free_retired(ap);',off),off)
            self.assertIn('list_add_tail(&buffer->node, &ap->retired)',source)
            self.assertIn('module_put(THIS_MODULE)',source)

    def test_mappings_cannot_become_executable(self):
        for source in self.sources():
            self.assertGreaterEqual(source.count('vma->vm_flags & VM_EXEC'),2)
            self.assertGreaterEqual(source.count('vm_flags_clear(vma, VM_MAYEXEC)'),2)
        decoder=self.sources()[1]
        self.assertIn('(physical >> PAGE_SHIFT) != vma->vm_pgoff',decoder)
        self.assertIn('vm_flags_clear(vma, VM_MAYWRITE)',decoder)

    def test_waits_and_decoder_dcm_are_narrow(self):
        for source in self.sources():
            self.assertIn('arg.timeout_ms > 1000',source)
            self.assertIn('wait_event_interruptible_timeout',source)
            self.assertNotIn('ioremap(',source) # owned platform resources only
        decoder=self.sources()[1]
        self.assertIn('readl_poll_timeout',decoder)
        self.assertIn('regmap_update_bits(ap->vdecsys, 0x18, BIT(0)',decoder)
        header=(ROOT/'athena_ap.h').read_text()
        self.assertIn('#define AP_DCM _IO(AP_MAGIC, 5)',header) # no address/value

    def test_no_proprietary_or_owner_material_in_module_source(self):
        for source in self.sources():
            for forbidden in ('/Users/','/Volumes/','serialno=','vendor-codecs','arm_smccc',
                              'filp_open','request_firmware','rproc_boot'):
                self.assertNotIn(forbidden,source)

if __name__=='__main__':unittest.main()

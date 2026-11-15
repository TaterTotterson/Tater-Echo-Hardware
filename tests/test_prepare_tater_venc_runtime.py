import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from scripts.prepare_tater_venc_runtime import prepare


class EncoderRuntimeTests(unittest.TestCase):
    def test_exact_extension_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            helper,component=root/"helper",root/"component"
            helper.write_bytes(b"original helper");component.write_bytes(b"owner library")
            options=dict(helper_hash=hashlib.sha256(helper.read_bytes()).hexdigest(),component_hash=hashlib.sha256(component.read_bytes()).hexdigest())
            output=root/"runtime"
            manifest=prepare(helper,component,output,**options)
            self.assertEqual(json.loads((output/"runtime.json").read_text()),manifest)
            self.assertEqual((output/"libMtkOmxVenc.so").read_bytes(),b"owner library")
            with self.assertRaises(ValueError):prepare(helper,component,output,**options)

    def test_wrong_bytes_and_symlinks_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            helper,component=root/"helper",root/"component"
            helper.write_bytes(b"a");component.write_bytes(b"b")
            with self.assertRaises(ValueError):prepare(helper,component,root/"bad")
            link=root/"link";link.symlink_to(component)
            with self.assertRaises(ValueError):prepare(helper,link,root/"bad",helper_hash=hashlib.sha256(b"a").hexdigest(),component_hash=hashlib.sha256(b"b").hexdigest())

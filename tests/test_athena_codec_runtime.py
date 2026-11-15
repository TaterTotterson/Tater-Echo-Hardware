"""Owner-local runtime preparation contracts, using only fake test files."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('athena_runtime',ROOT/'scripts/prepare_athena_codec_runtime.py')
runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)

class AthenaCodecRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='athena-runtime-test-')
        self.root=Path(self.temporary.name);self.source=self.root/'source';self.source.mkdir()
        (self.source/'lib').mkdir();(self.source/'linker').write_bytes(b'fake-linker')
        (self.source/'lib/codec.so').write_bytes(b'fake-codec')
        self.output=self.root/'output';files={};tree=hashlib.sha256();total=0
        for path in sorted(p for p in self.source.rglob('*') if p.is_file()):
            name=path.relative_to(self.source).as_posix();value=runtime.digest(path);files[name]=value
            tree.update(name.encode()+b'\0'+value.encode()+b'\n');total+=path.stat().st_size
        self.lock={'schema':1,'target':'athena','runtime_id':'athena-mt8183-ap-vdec-1','experimental':True,
                   'tree_sha256':tree.hexdigest(),'file_count':2,'runtime_bytes':total,'files':files}
    def tearDown(self):self.temporary.cleanup()
    def test_exact_tree_copied_without_helper_or_owner_metadata(self):
        result=runtime.prepare(self.source,self.output,self.lock)
        self.assertNotIn('files',result)
        self.assertEqual((self.output/'lib/codec.so').read_bytes(),b'fake-codec')
        self.assertEqual((self.output/'linker').stat().st_mode&0o777,0o555)
        self.assertEqual((self.output/'lib/codec.so').stat().st_mode&0o777,0o444)
        self.assertEqual(json.loads((self.output/'runtime.json').read_text()),result)
    def test_extra_or_changed_file_rejected_without_output(self):
        (self.source/'extra').write_bytes(b'extra')
        with self.assertRaises(ValueError):runtime.prepare(self.source,self.output,self.lock)
        (self.source/'extra').unlink();(self.source/'linker').write_bytes(b'changed')
        with self.assertRaises(ValueError):runtime.prepare(self.source,self.output,self.lock)
        self.assertFalse(self.output.exists())
    def test_symlink_rejected_without_following(self):
        (self.source/'link').symlink_to(self.source/'linker')
        with self.assertRaises(ValueError):runtime.prepare(self.source,self.output,self.lock)
        self.assertFalse(self.output.exists())
    def test_existing_output_and_nested_output_rejected(self):
        self.output.mkdir()
        with self.assertRaises(ValueError):runtime.prepare(self.source,self.output,self.lock)
        with self.assertRaises(ValueError):runtime.prepare(self.source,self.source/'nested',self.lock)
    def test_aggregate_mismatch_rejected_before_copy(self):
        for name in ('file_count','runtime_bytes'):
            changed=dict(self.lock);changed[name]+=1
            with self.assertRaises(ValueError):runtime.prepare(self.source,self.output,changed)
        self.assertFalse(self.output.exists())
    def test_encoder_namespace_is_separate(self):
        lock=dict(self.lock);lock['runtime_id']='athena-mt8183-ap-venc-1'
        result=runtime.prepare(self.source,self.output,lock)
        self.assertEqual(result['runtime_id'],'athena-mt8183-ap-venc-1')
        self.assertEqual(result['tree_sha256'],self.lock['tree_sha256'])

if __name__=='__main__':unittest.main()

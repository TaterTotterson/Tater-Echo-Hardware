from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from scripts import prepare_athena_mesa_runtime as mesa


class MesaOwnershipTests(unittest.TestCase):
    def test_only_the_temporary_tree_is_chowned_without_following_links(self):
        script = mesa.install_script(123, 456)
        self.assertIn('chown -hR 123:456 /out/root', script)
        self.assertIn('[ ! -L /out/root ]', script)
        self.assertIn('--no-network', script)
        self.assertIn('--no-scripts', script)
        self.assertNotIn('--allow-untrusted', script)
        self.assertNotIn('chown -hR 123:456 /out;', script)
        for value in (-1, 2**32, '0; false', True):
            with self.assertRaises(ValueError):
                mesa.install_script(value, 0)

    def test_ownership_cleanup_preserves_apk_exit_status(self):
        for status in (0, 7):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root/'bin').mkdir()
                (root/'root').mkdir()
                (root/'bin/apk').write_text('#!/bin/sh\nexit '+str(status)+'\n')
                (root/'bin/chown').write_text('#!/bin/sh\nprintf "%s\\n" "$*" > "$MESA_CALL_LOG"\n')
                for path in (root/'bin').iterdir():path.chmod(0o755)
                script = mesa.install_script(123,456).replace('/out/root', str(root/'root'))
                environment = dict(os.environ, PATH=str(root/'bin')+':'+os.environ['PATH'],
                                   MESA_CALL_LOG=str(root/'calls'))
                result = subprocess.run(['sh','-ec',script],env=environment)
                self.assertEqual(result.returncode,status)
                self.assertEqual((root/'calls').read_text().strip(),'-hR 123:456 '+str(root/'root'))


if __name__ == '__main__':unittest.main()

"""Rook uses its observed LCM/kernel/geometry, not a guessed product token."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RookGuardTests(unittest.TestCase):
    def test_exact_hardware_lcm_and_geometry(self):
        good="androidboot.hardware=mt8163 techo5=linux lcm=1-hx8379c_wvga_dsi_vdo"
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary)
            executable=directory/"guard"
            source='#include "rook_target.h"\nint main(int argc,char **argv){(void)rook_gpu_target_allowed;return argc==4 && rook_gpu_metadata_allowed(argv[1],argv[2],argv[3]) ? 0:2;}\n'
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"probes/mt8163-gpu"),"-x","c","-","-o",str(executable)],input=source.encode(),check=True)
            command,size,depth=[directory/n for n in ("cmdline","size","depth")]
            for text,geometry,bits,allowed in ((good,"480,1440","32",True),(good+" androidboot.product=rook","480,1440","32",True),(good+" androidboot.product=crown","480,1440","32",False),(good.replace("mt8163","mt8183"),"480,1440","32",False),(good.replace("hx8379c","other"),"480,1440","32",False),(good,"480,2880","32",False),(good,"480,1440","16",False),("x"*4096+good,"480,1440","32",False),(good+" androidboot.hardware=mt8163","480,1440","32",False)):
                command.write_text(text);size.write_text(geometry);depth.write_text(bits)
                self.assertEqual(subprocess.run([str(executable),str(command),str(size),str(depth)]).returncode==0,allowed)

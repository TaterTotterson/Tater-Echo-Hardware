import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class AthenaGPUGuardTests(unittest.TestCase):
    def test_metadata_guard_is_exact_without_device_access(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            source=root/'test.c'
            source.write_text('''#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
static char **inputs;
static FILE *fixture_open(const char *path,const char *mode) {
 const char *paths[]={"/proc/device-tree/compatible","/sys/class/graphics/fb0/virtual_size","/sys/class/graphics/fb0/bits_per_pixel","/sys/class/drm/renderD128/device/uevent"};
 for(int i=0;i<4;i++) if(!strcmp(path,paths[i])) return fopen(inputs[i+1],mode);
 return NULL;
}
static int fixture_uname(struct utsname *value) { memset(value,0,sizeof(*value)); snprintf(value->release,sizeof(value->release),"%s",inputs[5]);return 0; }
#define fopen fixture_open
#define uname fixture_uname
#include "athena_target.h"
#undef fopen
#undef uname
int main(int argc,char **argv) {inputs=argv;return argc==6 && athena_gpu_target_allowed() ? 0 : 2;}
''')
            binary=root/'test'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-std=c11','-I',str(ROOT/'probes/mt8163-gpu'),str(source),'-o',str(binary)],check=True)
            paths=[root/name for name in ('compatible','size','depth','driver')]
            defaults=[b'amzn,athena-pvt\0amzn,athena\0mediatek,mt8183\0',b'800,2560\n',b'32\n',b'DRIVER=panfrost\nOF_NAME=gpu\n']
            for mutation in (None,'dt','panel','depth','driver','overflow','kernel'):
                values=defaults.copy()
                if mutation=='dt':values[0]=b'amzn,athena-extra\0'
                if mutation=='panel':values[1]=b'800,3840\n'
                if mutation=='depth':values[2]=b'16\n'
                if mutation=='driver':values[3]=b'DRIVER=mali\n'
                if mutation=='overflow':values[0]=defaults[0]+b'x'*256
                for path,value in zip(paths,values):path.write_bytes(value)
                kernel='4.9.337-g8d928c5176cc' if mutation=='kernel' else '6.18.56-techo5-athena'
                self.assertEqual(subprocess.run([str(binary),*map(str,paths),kernel]).returncode==0,mutation is None,mutation)

    def test_native_gpu_gate_and_software_rejection(self):
        source=(ROOT/'probes/mt8163-gpu/rgba_rotate_stream_android.c').read_text()
        self.assertLess(source.index('if (!athena_gpu_target_allowed())'),source.index('if (stat(MALI_NODE'))
        for required in ('Mali-G72','Panfrost','llvmpipe','softpipe','libEGL.so.1','libGLESv2.so.2'):
            self.assertIn(required,source)

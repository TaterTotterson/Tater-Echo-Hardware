#!/usr/bin/env python3
"""Build Athena's original Mesa GPU adapter; no vendor inputs or device writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def build(lock, image, output):
    if lock.get('schema') != 1 or lock.get('target') != 'athena' or set(lock.get('helpers',{})) != {'tater-hw-gpu'}:
        raise ValueError('only the Athena GPU-only contract is supported')
    if output.exists() or output.is_symlink(): raise ValueError('output already exists')
    for relative, expected in lock['sources'].items():
        path = ROOT/relative
        if Path(relative).is_absolute() or '..' in Path(relative).parts or path.is_symlink() or digest(path) != expected:
            raise ValueError('source differs from the firmware lock: '+relative)
    identity = subprocess.check_output(['docker','image','inspect','--format','{{.Id}}',image],text=True).strip()
    if identity != lock['builder_image_id']: raise ValueError('not the reviewed ARMv7 builder')
    with tempfile.TemporaryDirectory(prefix='tater-athena-gpu-') as temporary:
        stage = Path(temporary)
        subprocess.run(['docker','run','--rm','--network','none','--platform','linux/arm/v7',
            '-v',str(ROOT)+':/src:ro','-v',str(stage)+':/out',image,
            'gcc','-O2','-Wall','-Wextra','-Werror','-std=c11','-D_POSIX_C_SOURCE=200809L',
            '-DTATER_GPU_ATHENA','-Wl,--build-id=none','-o','/out/tater-hw-gpu',
            '/src/probes/mt8163-gpu/rgba_rotate_stream_android.c','-ldl'],check=True)
        if digest(stage/'tater-hw-gpu') != lock['helpers']['tater-hw-gpu']:
            raise ValueError('rebuilt helper differs from the qualified executable')
        output.mkdir(parents=True)
        shutil.copyfile(stage/'tater-hw-gpu',output/'tater-hw-gpu')
        (output/'tater-hw-gpu').chmod(0o755)
        lock_hash = hashlib.sha256(json.dumps(lock,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        manifest = {'schema':1,'target':'athena','bundle_id':lock['bundle_id'],
                    'files':lock['helpers'],'source_lock_sha256':lock_hash}
        (output/'tater-hardware.json').write_text(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lock',required=True,type=Path)
    parser.add_argument('--image',required=True)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    try: build(json.loads(args.lock.read_text()),args.image,args.output)
    except (OSError,ValueError,subprocess.CalledProcessError) as error: parser.exit(1,f'refusing: {error}\n')

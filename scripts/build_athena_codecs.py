#!/usr/bin/env python3
"""Locked, source-only Athena AP helper build. No owner runtime is linked/copied."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError('expected an ordinary input file: '+str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(directory, files):
    for relative, expected in files.items():
        if Path(relative).is_absolute() or '..' in Path(relative).parts:
            raise ValueError('unsafe source/input path')
        if digest(directory/relative) != expected:
            raise ValueError('input differs from lock: '+relative)

def build(lock, ndk, val_headers, vdec_headers, omx_headers, output):
    if lock.get('schema') != 1 or lock.get('target') != 'athena' or lock.get('experimental') is not True:
        raise ValueError('only the experimental Athena AP contract is supported')
    if set(lock.get('helpers',{})) != {'tater-hw-vdec','tater-hw-venc'}:
        raise ValueError('only the experimental Athena AP helper pair is supported')
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists; refusing to merge/replace')
    verify(ROOT,lock['sources'])
    verify(val_headers,lock['val_headers'])
    verify(vdec_headers,lock['vdec_headers'])
    verify(omx_headers,lock['omx_headers'])
    if digest(ndk/'source.properties') != lock['ndk_properties_sha256']:
        raise ValueError('not the pinned NDK r21e')
    host={'Darwin':'darwin-x86_64','Linux':'linux-x86_64'}.get(platform.system())
    if host is None: raise ValueError('build on macOS or Linux')
    compiler=ndk/'toolchains/llvm/prebuilt'/host/'bin/armv7a-linux-androideabi21-clang'
    environment={k:v for k,v in os.environ.items() if not k.startswith(('ANDROID_','LD_','DYLD_','CFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','LIBRARY_PATH'))}
    environment['SOURCE_DATE_EPOCH']='0'
    with tempfile.TemporaryDirectory(prefix='tater-athena-codec-') as temporary:
        stage=Path(temporary)
        for relative in lock['sources']:
            destination=stage/relative;destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/relative,destination)
        products={}
        for name,source in (('tater-hw-vdec','athena_vdec.c'),('tater-hw-venc','athena_venc.c')):
            product=stage/name
            arguments=[str(compiler),'-O2','-Wall','-Wextra','-Werror','-std=c11','-mfpu=neon',
            '-I',str(val_headers),'-I',str(vdec_headers),'-I',str(stage/'probes/mt8163-vcodec'),
            '-I',str(omx_headers),'-I',str(omx_headers/'media/openmax'),
            '-Wl,--build-id=none','-Wl,--dynamic-linker,/data/tater-athena-codec/linker','-Wl,-E',
            str(stage/'probes/mt8183-codec'/source)]
            if name=='tater-hw-vdec': arguments.append(str(stage/'probes/mt8163-vcodec/mtk_yuv420_block.c'))
            subprocess.run(arguments+['-ldl','-o',str(product)],env=environment,check=True)
            if digest(product) != lock['helpers'][name]:
                raise ValueError('helper output differs from the reviewed build: '+name)
            products[name]=product
        output.mkdir(parents=True)
        for name,product in products.items():
            shutil.copyfile(product,output/name);(output/name).chmod(0o555)
        manifest={'schema':1,'target':'athena','experimental':True,
                  'decode':{'codec':'h264','profiles':['baseline'],'single_slice_only':True,
                            'live_only':True,'max_width':1280,'max_height':960},
                  'encode':{'codec':'h264','input':'nv12','max_width':640,'max_height':480,'max_fps':30},
                  'files':lock['helpers'],'source_lock_sha256':hashlib.sha256(json.dumps(lock,sort_keys=True,separators=(',',':')).encode()).hexdigest()}
        (output/'athena-codec-build.json').write_text(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n')
    return manifest

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('lock','ndk','val-headers','vdec-headers','omx-headers','output'):
        parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args()
    try:
        result=build(json.loads(args.lock.read_text()),args.ndk,args.val_headers,args.vdec_headers,args.omx_headers,args.output)
        print(json.dumps(result,sort_keys=True))
    except (OSError,ValueError,KeyError,subprocess.CalledProcessError) as error:
        parser.exit(1,'refusing: '+str(error)+'\n')

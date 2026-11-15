#!/usr/bin/env python3
"""Prepare isolated Athena Mesa from exact signed Alpine packages, off-device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BUILDER='sha256:8f2fba1b9274e512524c572deb4dbb5abd92e15bb88b8620ea1e2cd24fe57a74'
MANIFEST='ab77ccdd446064c327bf3fe150b3eeb73d65cc8f9bf3c5aeb79f7376725a5ef8'

def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
    return value.hexdigest()

def prepare(packages,image,output):
    pins=json.loads((ROOT/'probes/mt8163-gpu/athena-mesa-packages.json').read_text())
    if output.exists() or output.is_symlink():raise ValueError('output exists')
    if packages.is_symlink() or not packages.is_dir() or {p.name for p in packages.iterdir()}!=set(pins):
        raise ValueError('package set differs from the reviewed Alpine v3.21 ARMv7 set')
    for name,expected in pins.items():
        path=packages/name
        if path.is_symlink() or not path.is_file() or digest(path)!=expected:
            raise ValueError('APK hash mismatch: '+name)
    identity=subprocess.check_output(['docker','image','inspect','--format','{{.Id}}',image],text=True).strip()
    if identity!=BUILDER:raise ValueError('not the reviewed ARMv7 builder')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.athena-mesa-',dir=output.parent) as temporary:
        stage=Path(temporary)
        # Verification uses the builder's trusted Alpine keys; never --allow-untrusted.
        # Installation is isolated, offline, and has package scripts disabled.
        subprocess.run(['docker','run','--rm','--network','none','--platform','linux/arm/v7',
            '-v',str(packages.resolve())+':/packages:ro','-v',str(stage)+':/out',image,'sh','-ec',
            'apk add --no-network --root /out/root --initdb --no-scripts --keys-dir /etc/apk/keys --repositories-file /etc/apk/repositories /packages/*.apk'],check=True)
        source=stage/'root'
        runtime=stage/'runtime'
        (runtime/'lib').mkdir(parents=True)
        (runtime/'dri').mkdir()
        for directory in ('lib','usr/lib'):
            for path in sorted((source/directory).iterdir()):
                if path.is_file() and '.so' in path.name:
                    resolved=path.resolve(strict=True)
                    if not resolved.is_relative_to(source):raise ValueError('escaping package link')
                    shutil.copyfile(resolved,runtime/'lib'/path.name)
        for name in ('libdril_dri.so','panfrost_dri.so'):
            path=source/'usr/lib/xorg/modules/dri'/name
            if not path.resolve().is_relative_to(source):raise ValueError('escaping DRI link')
            shutil.copyfile(path,runtime/'dri'/name)
        files={p.relative_to(runtime).as_posix():{'sha256':digest(p),'size':p.stat().st_size}
               for p in sorted(runtime.rglob('*')) if p.is_file()}
        tree=hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        manifest={'schema':3,'runtime_id':'athena-mt8183-panfrost-mesa-24.2.8-1','tree_sha256':tree,'files':files}
        path=runtime/'runtime.json'
        path.write_text(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n')
        if digest(path)!=MANIFEST:raise ValueError('prepared runtime differs from hardware-qualified files')
        (runtime/'lib/ld-musl-armhf.so.1').chmod(0o755)
        runtime.rename(output)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages',required=True,type=Path)
    parser.add_argument('--image',default=BUILDER)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    try:prepare(args.packages,args.image,args.output)
    except (OSError,ValueError,subprocess.CalledProcessError) as error:parser.exit(1,f'refusing: {error}\n')

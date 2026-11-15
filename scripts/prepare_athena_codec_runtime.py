#!/usr/bin/env python3
"""Verify/copy a strictly owner-local Athena codec runtime. Never downloads it."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    if path.is_symlink() or not path.is_file(): raise ValueError('runtime requires ordinary files')
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare(source, output, lock):
    if (lock.get('schema'),lock.get('target'),lock.get('experimental')) != (1,'athena',True) or lock.get('runtime_id') not in ('athena-mt8183-ap-vdec-1','athena-mt8183-ap-venc-1'):
        raise ValueError('not an experimental Athena codec runtime lock')
    if output.exists() or output.is_symlink(): raise ValueError('output already exists')
    if source.is_symlink() or not source.is_dir(): raise ValueError('source must be an ordinary directory')
    source=source.resolve();output=output.resolve()
    if output==source or source in output.parents: raise ValueError('output cannot be inside source')
    actual=set()
    for path in source.rglob('*'):
        if path.is_symlink(): raise ValueError('runtime symlinks are not permitted')
        if path.is_dir(): continue
        if not path.is_file(): raise ValueError('runtime special files are not permitted')
        actual.add(path.relative_to(source).as_posix())
    if actual!=set(lock['files']): raise ValueError('runtime has missing or unexpected files')
    tree=hashlib.sha256();total=0
    for name, expected in sorted(lock['files'].items()):
        if Path(name).is_absolute() or '..' in Path(name).parts: raise ValueError('unsafe runtime path')
        value=digest(source/name)
        if value!=expected: raise ValueError('runtime hash mismatch: '+name)
        tree.update(name.encode()+b'\0'+value.encode()+b'\n');total+=(source/name).stat().st_size
    if (tree.hexdigest(),len(actual),total)!=(lock['tree_sha256'],lock['file_count'],lock['runtime_bytes']):
        raise ValueError('runtime aggregate identity mismatch')
    output.mkdir(parents=True)
    for name in sorted(lock['files']):
        destination=output/name;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source/name,destination);destination.chmod(0o555 if name=='linker' else 0o444)
    manifest={name:lock[name] for name in ('schema','target','runtime_id','experimental','tree_sha256','file_count','runtime_bytes')}
    (output/'runtime.json').write_text(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n')
    (output/'runtime.json').chmod(0o444)
    return manifest

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--kind',choices=['decoder','encoder'],default='decoder')
    parser.add_argument('--acknowledge',required=True,choices=['owner-local-runtime'])
    args=parser.parse_args()
    try:
        lock_name='athena-codec-runtime-lock.json' if args.kind=='decoder' else 'athena-encoder-runtime-lock.json'
        result=prepare(args.source,args.output,json.loads((ROOT/'platforms'/lock_name).read_text()))
        print(json.dumps(result,sort_keys=True))
    except (OSError,ValueError,KeyError) as error:parser.exit(1,'refusing: '+str(error)+'\n')

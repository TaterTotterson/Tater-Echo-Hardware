#!/usr/bin/env python3
"""Prepare the small owner-local encoder extension to the qualified VDEC runtime.

Nothing is downloaded or published. Private inputs and the generated output
must remain outside tracked source/release trees.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

EXPECTED_HELPER = "5431a4fb2b32378313307b4550e71842df23fb155acf04e3a816662312df2e7d"
EXPECTED_COMPONENT = "46ec0c93f5fc5b6e6512125397d35236c5be5759dbdb285bd29c4c4714e181fe"
BASE_TREE = "490a3c771d1c6f979828e60b93c363ec42403ca9bd3c98b12f8c5cd0761f2cc4"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        result = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            result.update(chunk)
    return result.hexdigest()


def prepare(helper: Path, component: Path, output: Path, *, helper_hash=EXPECTED_HELPER, component_hash=EXPECTED_COMPONENT, include_helper=True, runtime_id="checkers-mt8163-venc-stock-1", base_tree=BASE_TREE) -> dict:
    if output.exists() or output.is_symlink():
        raise ValueError("output exists; refusing to replace an installed runtime")
    for path, expected in ((helper,helper_hash),(component,component_hash)):
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 8*1024*1024 or digest(path)!=expected:
            raise ValueError("encoder input identity is not qualified")
    manifest = {"schema":1 if include_helper else 2,"runtime_id":runtime_id,
                "component_sha256":component_hash,"base_tree_sha256":base_tree}
    if include_helper:
        manifest["helper_sha256"] = helper_hash
    output.mkdir(parents=True,mode=0o755)
    for source,name,mode in ((helper,"tater-venc",0o555),(component,"libMtkOmxVenc.so",0o444)):
        if name == "tater-venc" and not include_helper:
            continue
        shutil.copyfile(source,output/name)
        (output/name).chmod(mode)
    (output/"runtime.json").write_text(json.dumps(manifest,sort_keys=True,separators=(",",":"))+"\n")
    (output/"runtime.json").chmod(0o444)
    output.chmod(0o555)
    return manifest


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper",type=Path,required=True)
    parser.add_argument("--component",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--acknowledge",choices=["owner-local-runtime"],required=True)
    args=parser.parse_args()
    try:
        result=prepare(args.helper,args.component,args.output)
    except (ValueError,OSError) as error:
        parser.exit(1,f"refusing: {error}\n")
    print(json.dumps(result,sort_keys=True))
    return 0


if __name__=="__main__":
    raise SystemExit(main())

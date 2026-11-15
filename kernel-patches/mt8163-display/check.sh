#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/android_kernel_amazon_mt8163" >&2
    exit 2
fi

kernel_tree=$1
relative=drivers/misc/mediatek/video/mt8163/videox/compat_mtk_disp_mgr.c
source_file=$kernel_tree/$relative
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
patch_file=$script_dir/0001-fix-compat-suspend-query.patch
expected=78e46a66285fbd5a4b01888581d4a8e03d90f0bcb4aab255d5b456740bcd93bf

if [ ! -f "$source_file" ]; then
    echo "missing exact kernel source: $relative" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$source_file" | awk '{print $1}')
else
    actual=$(shasum -a 256 "$source_file" | awk '{print $1}')
fi
if [ "$actual" != "$expected" ]; then
    echo "source does not match Checkers kernel commit 8d928c5176cc: $relative" >&2
    exit 1
fi

patch --dry-run --silent -p1 -d "$kernel_tree" < "$patch_file"
echo "MT8163 display compat patch matches 8d928c5176cc and applies cleanly."

#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/android_kernel_amazon_mt8163" >&2
    exit 2
fi

kernel_tree=$1
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
patch_file=$script_dir/0001-enable-mt8163-jpeg-decoder.patch

digest() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

check_source() {
    expected=$1
    relative=$2
    source_file=$kernel_tree/$relative
    if [ ! -f "$source_file" ]; then
        echo "missing exact kernel source: $relative" >&2
        exit 1
    fi
    actual=$(digest "$source_file")
    if [ "$actual" != "$expected" ]; then
        echo "source does not match Checkers kernel commit 8d928c5176cc: $relative" >&2
        exit 1
    fi
}

check_source fdd5faa0b30f379876a761a7362fbbbbce8a8ce922ed556bd7e863d5fddaa9de \
    arch/arm64/boot/dts/mediatek/mt8163.dtsi
check_source b42cec1e1312875ef21dc741a8306cbb0ea1cdbf3ba73098a92d56d0795094e1 \
    drivers/misc/mediatek/jpeg/v2/Makefile
check_source af4d6c01a24df495e45a97ba3e696036940deabb02ddc7b40821abad6a49cd54 \
    drivers/misc/mediatek/jpeg/v2/jpeg_drv.c

patch --dry-run --silent -p1 -d "$kernel_tree" < "$patch_file"
echo "Checkers JPEG decoder patch matches 8d928c5176cc and applies cleanly."

# Checkers MT8163 JPEG decoder experiment

This directory contains a clean, reviewable experiment against the public
Checkers kernel commit `8d928c5176cc`. It is not a production or flash-ready
kernel change.

## Current validation

- The three affected files match their exact public-source SHA-256 values.
- The patch applies cleanly to the complete source archive for
  `8d928c5176cc`.
- The exact live `/proc/config.gz` was captured read-only and remains unchanged
  after `olddefconfig` (SHA-256
  `93bde8f9783a91ad4e5626030fd81b5f60112903d4466a24d08798c2d9568271`).
- The complete kernel builds with Arm GCC 8.3-2019.03 and reports the required
  release `4.9.337-g8d928c5176cc`.
- The linked kernel exports `jpeg_drv_dec_start`, `jpeg_drv_dec_reset`, and
  `jpeg_drv_dec_set_config_data`.
- All five appended Checkers device trees contain encoder/decoder register
  windows, SPI 208/209, and VENC CKE2/CKE3 with the names consumed by the
  driver.
- The live MT7668 Bluetooth module matches all 96 imported symbol CRCs and the
  live Wi-Fi module matches all 203 imported symbol CRCs against the rebuilt
  `Module.symvers`.
- A donor-derived Android boot image was assembled offline with the exact live
  Tater Recovery ramdisk. It is 13,498,368 bytes, fits the 16 MiB boot
  partition with 3,278,848 bytes spare, and has not been flashed.

The earlier GCC 12 object-only check remains useful as a quick structural
test, but it is no longer the highest validation gate. Full evidence, input and
artifact hashes, and the recovery boundary are recorded in
[`docs/findings/checkers-gcc8-jpeg-kernel-build.md`](../../docs/findings/checkers-gcc8-jpeg-kernel-build.md).

The running kernel proves that the JPEG encoder is present while the decoder
code is compiled out. Enabling only `JPEG_DEC_DRIVER` is unsafe: the current
MT8163 device tree supplies one encoder register range, interrupt, and clock,
while the vendor driver immediately requests a second set when decoder support
is compiled.

`0001-enable-mt8163-jpeg-decoder.patch` therefore keeps the change bounded to:

- enabling the existing public decoder implementation for MT8163;
- adding the decoder register window at `0x17004000`;
- adding the decoder SPI interrupt immediately after the encoder interrupt;
- adding the VENC CKE3 decoder clock expected by the existing driver; and
- matching the decoder IRQ request to the device tree's active-low level type.

The register window is named by the public decoder driver. The clock/interrupt
assignment is a bounded but still unverified hardware inference: CKE0 and CKE1
serve LARB3, CKE2 is the existing JPEG encoder gate, CKE3 is the remaining JPEG
decoder gate, and SPI 209 immediately follows the existing encoder SPI 208 in
the MediaTek layout. Compile success cannot prove those assignments. The first
boot must therefore stop at resource discovery and kernel-log inspection before
any decoder ioctl is permitted.

Before this can be used beyond offline validation, it must pass all of these
gates:

1. apply cleanly to the exact public commit (complete);
2. compile the complete kernel with the live configuration and pinned
   toolchain (complete);
3. preserve the kernel release and vendor-module ABI (complete);
4. produce a donor-derived boot image that preserves Tater Recovery
   (complete, offline only);
5. prove that the retained TWRP fallback still boots before writing `boot`;
6. boot as a trial on the owner-operated Checkers and confirm the new decoder
   resources without issuing CONFIG/START; and
7. run the guarded INIT/DEINIT probe only after a fresh explicit approval.

No current Tater release should consume this patch automatically.

For an exact unmodified source tree, validate provenance and patch context with:

```sh
kernel-patches/checkers/check.sh /path/to/android_kernel_amazon_mt8163
```

The checker hashes the three affected public source files before performing a
no-write patch applicability check.

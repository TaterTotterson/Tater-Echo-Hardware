# Checkers MT8163 JPEG decoder experiment

This directory contains a clean, reviewable experiment against the public
Checkers kernel commit `8d928c5176cc`. It is not a production or flash-ready
kernel change.

## Current validation

- The three affected files match their exact public-source SHA-256 values.
- The patch applies cleanly to the complete source archive for
  `8d928c5176cc`.
- A native ARM64 Linux compile with `checkers_defconfig` succeeds for the
  composite JPEG driver and `checkers_dvt.dtb`.
- The compiled object exports `jpeg_drv_dec_start`, `jpeg_drv_dec_reset`, and
  `jpeg_drv_dec_set_config_data`.
- The compiled DVT contains encoder/decoder register windows, SPI 208/209, and
  VENC CKE2/CKE3 with the names consumed by the driver.

That compile used GCC 12 as a structural check. Because the source archive has
no Git metadata, its generated release was only `4.9.337`, not the vendor-module
compatible `4.9.337-g8d928c5176cc`. No full kernel or boot image was built. The
release's pinned GCC 8.3 toolchain and exact release suffix remain mandatory
before any boot trial.

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

Before this can be booted, it must pass all of these gates:

1. apply cleanly to the exact public commit;
2. compile the complete kernel with `checkers_defconfig` and the release's
   pinned toolchain;
3. preserve the kernel release and vendor-module ABI;
4. produce a donor-derived boot image with a known recovery path;
5. boot as a trial on the owner-operated Checkers and confirm the new decoder
   resources without issuing CONFIG/START; and
6. run the guarded INIT/DEINIT probe only after a fresh explicit approval.

No current Tater release should consume this patch automatically.

For an exact unmodified source tree, validate provenance and patch context with:

```sh
kernel-patches/checkers/check.sh /path/to/android_kernel_amazon_mt8163
```

The checker hashes the three affected public source files before performing a
no-write patch applicability check.

# Changelog

## Unreleased

- Record a read-only Checkers v4.0.2 hardware baseline from the connected test
  unit without retaining device identity, credentials, or raw capture data.
- Add a clean-room MT8163 JPEG ABI probe whose safe default only describes the
  interface and whose active INIT/DEINIT mode requires target verification and
  explicit acknowledgement.
- Record that Checkers' pinned kernel exposes the JPEG encoder but compiles out
  the decoder, and make the active probe refuse that incomplete kernel before
  opening its control node.
- Add an exact-source-validated experimental kernel/resource patch and compile
  its decoder object and Checkers DVT without producing a bootable image.
- Rebuild the complete Checkers kernel with the exact live configuration and
  pinned Arm GCC 8.3 toolchain while preserving the vendor-module release.
- Verify every imported symbol CRC used by the live MT7668 Bluetooth and Wi-Fi
  modules against the rebuilt kernel.
- Assemble and validate an unflashed donor-derived test boot image that keeps
  the exact live Tater Recovery ramdisk and fits the 16 MiB boot partition.
- Boot the retained Checkers TWRP 3.7.0 fallback, verify root ADB access, and
  document amonet's intentional `recovery`-to-`swdl` partition alias.

## 0.2.0 — 2026-11-10

- Add read-only GPT, Android boot, MediaTek kernel, ramdisk, appended-DTB, and
  ext4 image inspection.
- Add filtered ext4 extraction with automatic exclusion of common secret and
  key filenames.
- Add binary string offsets, firmware capability maps, and ELF dependency
  reports.
- Add Ghidra batch analysis, export refresh, function sizes, and string
  cross-references.
- Record the first reproducible MT8163 stock-firmware offline baseline and the
  bounded JPEG decode interface selected for connected-device validation.

# Checkers GCC 8.3 JPEG kernel build

Date: 2026-11-10

This record advances the MT8163 JPEG decoder experiment from an object-only
structural check to a complete Checkers kernel and an offline boot-image
candidate. Nothing in this work was flashed. The connected owner-operated
Checkers was used only for read-only configuration, module, partition-header,
and partition-hash collection.

## Reproducible inputs

- Public kernel commit: `8d928c5176cc`.
- Public source archive SHA-256:
  `4e0b311affbd75d58c64630c235146a5f27edcfcfdac6104e7e7cd4ca86ebe08`.
- Arm toolchain: GNU Toolchain for the A-profile Architecture 8.3-2019.03
  (`arm-rel-8.36`), GCC 8.3.0.
- Toolchain archive SHA-256:
  `8ce3e7688a47d8cd2d8e8323f147104ae1c8139520eca50ccf8a7fa933002731`.
- Toolchain archive MD5 from Arm's checksum file:
  `c7e2ae4fd6a66df642d59e8453775b4c`.
- Live kernel configuration SHA-256:
  `93bde8f9783a91ad4e5626030fd81b5f60112903d4466a24d08798c2d9568271`.
  Running `olddefconfig` against the patched source did not change it.
- Decoder patch SHA-256:
  `142401f8a1e11fcf7c8abbbe69470300e20e1ab3945145ab54349329269b7f85`.
- Existing Checkers microphone, camera/latch, button, and headphone-jack fixes
  were applied from Tater Echo Linux commit
  `3a0c2d480e281226d28c831730abc3301d605ba4` before the build.
- The isolated amd64 Linux builder image ID was
  `sha256:5f34238481f9c2cc7a70d06630093551f0bde2f7270c65adb54aebae4ae734c0`.

The source and toolchain were extracted inside Docker volumes backed by a
case-sensitive Linux filesystem. This matters on the macOS host because the
kernel tree contains names that differ only by capitalization.

## Complete build result

The complete `Image.gz-dtb` target succeeded with the captured live
configuration. The generated kernel identifies itself as:

```text
Linux version 4.9.337-g8d928c5176cc (tater@checkers-research) (gcc version 8.3.0 (GNU Toolchain for the A-profile Architecture 8.3-2019.03 (arm-rel-8.36)) ) #1 SMP PREEMPT
```

Important artifact identities:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `vmlinux` | 20,995,640 | `9d3b3177469db82929e275d512fb2784568391d19727bcfdcf539140dccf85ff` |
| `Image` | 15,415,808 | `d860eac00c06402dcb9c3d9bf07b116da6d3cefc508e7243f0879d63a15b0b81` |
| `Image.gz` | 6,909,204 | `3d674a6a9f8ebfe0867008c069fae0a0834d0c36f47cedc24fc05580d7695db1` |
| `Image.gz-dtb` | 7,205,821 | `a1bb0dd3af8d62e0c7bcd6710662d1375e6126a6a7a4064db7d0c0e8eef38495` |
| `Module.symvers` | — | `52452f76379b78dde03edd2799814253492fb75504b53ccc5f3b6e2074060332` |

The build emitted two pre-existing warnings: an ARM64 `%p` formatting warning
in `process.c` and an undefined preprocessor-name warning in `fscrypt.h`. It
emitted no compiler errors.

`System.map` contains the decoder entry points
`jpeg_drv_dec_start`, `jpeg_drv_dec_reset`, and
`jpeg_drv_dec_set_config_data`. The appended kernel is exactly `Image.gz`
followed by the proto, HVT, EVT, DVT, and PVT Checkers device trees. All five
device trees contain:

- encoder window `0x17003000` and decoder window `0x17004000`;
- encoder SPI 208 and decoder SPI 209, both active-low level;
- VENC clock gates CKE2 and CKE3; and
- clock names `venc-jpgenc` and `MT_CG_VENC_JPGDEC`.

Compile success confirms internal consistency, not the inferred physical IRQ
and clock assignment. The first trial boot must inspect resource acquisition
and kernel logs without issuing a decoder CONFIG or START request.

## Vendor-module ABI check

The connected unit reports this vermagic for both live MT7668 modules:

```text
4.9.337-g8d928c5176cc SMP preempt mod_unload modversions aarch64
```

The ignored local copies were identified before analysis as:

- `mt76x8_bt.ko` SHA-256
  `e9b20f3d9e50d334cf9d4300ee45ea5adaac0ffad50806de7103c3bb57b1eec4`;
- `mt76x8_wlan.ko` SHA-256
  `e01a27d5c5f8558f39390f3ba7c61ea96bfbf15c443990d494e527f35ca79ee5`.

Every imported symbol version matches the new build: 96 of 96 for Bluetooth
and 203 of 203 for Wi-Fi, with no missing symbols and no CRC mismatch. The
module binaries remain ignored and must not be committed.

## Boot and recovery boundary

Read-only device evidence confirms:

- `boot` is `/dev/mmcblk0p9`, 16 MiB, Android boot magic, full-partition
  SHA-256
  `1e8dc968735f7e71d86db63196dd5ca450acc6f9d6c081cf58df868b17598950`;
- `recovery` is `/dev/mmcblk0p10`, 16 MiB, Android boot magic,
  full-partition SHA-256
  `50d66b274e770ba2809980f97392c5835e4912a72835d9d5d1e4787e7efce336`;
- the live boot image's used 13,492,224 bytes have SHA-256
  `3bee39e205d4a6499bdac7ab9be35f742376426f2218c81c587b8dd4394ce8df`;
  and
- the live boot header uses the production 64-bit boot mode, while the retained
  recovery header selects the 32-bit recovery mode.

Current Checkers v4 already has a Tater Recovery environment: it is the rescue
initramfs in `boot`. TWRP deliberately remains in the separate `recovery`
partition as the lower-level fallback. Replacing TWRP is therefore not required
for this experiment and would be a separate 32-bit recovery-kernel project.

An offline test image was assembled from the exact live Tater Recovery ramdisk
and the rebuilt kernel:

- size: 13,498,368 bytes;
- SHA-256:
  `07cf99a5ad51cfa1f7eb697245a669057594d04a609f1ab481054123c67c7715`;
- free space in the 16 MiB partition: 3,278,848 bytes;
- kernel payload: byte-identical to the recorded `Image.gz-dtb`;
- ramdisk payload: byte-identical to the live ramdisk; and
- Android boot ID: independently recomputed and valid.

Only the kernel-size field and boot ID changed in the donor header. No boot or
recovery partition was written.

## Remaining hardware gates

1. Explicitly reboot the owner-operated device into the retained TWRP and
   verify USB/ADB recovery access. This is a state-changing test and needs a
   separate approval.
2. Preserve the current boot image and its hashes outside the trial path.
3. Write the candidate only through the reviewed Checkers factory/recovery
   path, never from an unverified normal boot.
4. On the first boot, inspect only decoder resource discovery and kernel logs.
5. Run the guarded INIT/DEINIT probe only after the resource evidence passes
   and a fresh active-ioctl approval is given.

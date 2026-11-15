# Checkers display-compat kernel build

Date: 2026-11-11

The bounded MT8163 32-bit display suspend-query fix now passes a complete
Checkers kernel build. Nothing from this build was packaged or flashed.

## Inputs and isolation

- Public kernel commit: `8d928c5176cc`.
- Exact live Checkers configuration SHA-256:
  `93bde8f9783a91ad4e5626030fd81b5f60112903d4466a24d08798c2d9568271`.
- Arm GNU Toolchain 8.3-2019.03, the same pinned compiler used for the
  recoverable kernel trial.
- Display patch SHA-256:
  `a45cb9087d352da68d7f44be9b6e22031a8929624a7e7f2e03e27671d44cbcc4`.
- Original `compat_mtk_disp_mgr.c` SHA-256:
  `78e46a66285fbd5a4b01888581d4a8e03d90f0bcb4aab255d5b456740bcd93bf`.
- Patched `compat_mtk_disp_mgr.c` SHA-256:
  `9bfb3f7b36fe5b8ae496cafa12b4a7b01fcd4fa93b49c3821c16d68a520a57d2`.

The build used separate case-sensitive Docker source and output volumes. It
retained the existing Checkers microphone, camera/latch, button, and
headphone-jack fixes used by Tater. Before compiling, the rejected experimental
JPEG decoder patch was explicitly reversed. The three JPEG experiment files
then matched the pristine public source archive, and the final `System.map`
contained no `jpeg_drv_dec_start` symbol.

The ignored working reference had previously received those three experimental
JPEG edits. They were restored byte-for-byte from the verified public source
archive after this was noticed; both patch provenance checkers now pass against
the pristine reference again.

## Complete build result

The complete `Image.gz-dtb` target succeeded and identifies itself as
`4.9.337-g8d928c5176cc`. The build reproduced the two already-recorded baseline
warnings in `process.c` and `fscrypt.h`, with no compiler or linker error.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `vmlinux` | 20,993,440 | `cdd6a0d0218219590ae19c8447b078980548270a7ff95708584a98a469a564c6` |
| `Image` | — | `3d56eec15989bc28cbca52973ea9a6fc72ab86012137cd4a9c8a349daff7bb4a` |
| `Image.gz` | — | `28c555616ca1820f3b4c362407541dfb65595d2a654aeb26038403627e03704a` |
| `Image.gz-dtb` | 7,200,557 | `2762e6ecf4958dedb7fcd1ac7b416eb3fd423ef45d768496c2691efa86f339da` |
| `Module.symvers` | — | `52452f76379b78dde03edd2799814253492fb75504b53ccc5f3b6e2074060332` |

`Module.symvers` is byte-identical to the earlier full build whose live MT7668
Bluetooth and Wi-Fi imports were checked symbol-by-symbol. This source change
therefore preserves the recorded vendor-module ABI.

## Offline boot-image candidate

A new original tool, `scripts/repack_legacy_boot.py`, now verifies and repacks
legacy Android boot-v0 images. It refuses to overwrite an output, validates
the donor's SHA-1 boot ID before use, preserves the ramdisk/second/device-tree
payloads, recomputes the boot ID, and validates the completed image. Repacking
the original image with its own extracted kernel reproduced all 13,492,224
bytes exactly, including SHA-256
`3bee39e205d4a6499bdac7ab9be35f742376426f2218c81c587b8dd4394ce8df`.

The display-fix kernel was then repacked with the same verified donor:

- used image bytes: 13,492,224;
- SHA-256:
  `16b9440e757a771a051ef0524a3dc8324955916f4c1884b22e8c0dee532b68a7`;
- kernel payload: byte-identical to the recorded `Image.gz-dtb`;
- ramdisk payload: byte-identical to the original image;
- validated Android boot ID and header-v0 layout; and
- 3,284,992 bytes remain in the 16 MiB physical boot partition.

The candidate and donor remain ignored owner-local data. Packaging it proves
the recovery image can be assembled deterministically; it does not authorize
or require a device write.

Compile success validates the source and ABI shape. A future live test would
still need the established recovery and boot-image review, followed only by
the read-only 32-bit `GET_IS_DRIVER_SUSPEND` query. This fix is useful but is
not required for current Tater framebuffer presentation, so no unattended
kernel write was justified.

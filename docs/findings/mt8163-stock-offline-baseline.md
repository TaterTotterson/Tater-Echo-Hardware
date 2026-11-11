# MT8163 stock firmware offline baseline

Date: 2026-11-10

This note records what can be established from an owner-local stock backup
without connecting to or changing a device. Firmware, extracted files, Ghidra
projects, decompiler output, device identifiers, and keys remain outside Git.
The hashes and interface descriptions below are sufficient to reproduce and
audit the analysis when the operator has lawful access to the same inputs.

## Source identity

| Input | SHA-256 | Size |
| --- | --- | ---: |
| Android boot image | `92d48d9e2756e13bfc07274d1f21a0e52674cbd85256c442a482c4776300094f` | 16,777,216 |
| GPT image | `7cb09e3dc3cb4b34dacf1485e560df7500c4c4bdc4c3a1d2da1d327f732c8de7` | 17,408 |
| ext4 system image | `10808e01c300bf6ca08c38581b8e83d524a86ccda377f8fe07ab8ff0bab30cce` | 1,744,830,464 |

The boot image contains a MediaTek-wrapped, gzip-compressed AArch64 kernel
(`523979cbe1da9962d7fe3b20939e57171177ff88c36ab02e7daf3a1a5267d483`)
and a gzip ramdisk
(`1634290a927f99e77245a898aadf636a4a4f1b8486eee6bb4af8501c8e5098f6`).
The uncompressed kernel is
`170d82752db0ddc316423ef92072a6866cc71e040dbb9b5fda4bdfcb833a2dfc`.
Two appended device trees were recovered:

- `474c2ad1a153fd1bc7611c24a141c2dda8b48782356d36fbfef0280a30af0219`
  (HVT, 50,677 bytes)
- `d29720fea2ca4fbfe45c3b65e6214bd9afaf09a3f85f9be0c15367d6a11e4c1b`
  (EVT, 50,966 bytes)

The GPT contains 13 named partitions, including boot, recovery, system, cache,
and userdata. Partition contents are not published here.

## Confirmed hardware-facing components

The selected system roots yielded 1,007 files, including 973 ELF objects: 534
ARM32, 434 AArch64, and five x86 objects. The stock dependency set is nearly
closed; the only stock providers not present in the selected roots are
`libOpenCL.so` and `libmmprofile.so`.

The following are corroborated by device-tree nodes, init permissions, Android
media configuration, exported symbols/strings, library dependencies, and
targeted Ghidra analysis:

- Mali T720 graphics through the Mali, EGL, gralloc, ION, M4U, hardware
  composer, framebuffer, and MediaTek display-manager paths.
- MediaTek hardware video decode and encode through OMX, VDEC/VENC,
  `libvcodecdrv`, ION/M4U, and display-processing libraries.
- Hardware JPEG decode/encode through the MediaTek JPEG driver and M4U/ION
  buffer handling.
- Audio input/output, PCM and TDM capture, mixer control, speech enhancement,
  WebRTC audio preprocessing, and the Amazon/MediaTek SPI audio platform.

Android media configuration advertises hardware MPEG-2, HEVC, MPEG-4, H.263,
AVC, DivX, Xvid, and S263 decode; MPEG-4, H.263, AVC, and HEVC encode. Hardware
VP8/VP9 entries are disabled in this image, while software fallback entries are
present. That is a firmware configuration fact, not proof of what another
build or a runtime-modified system exposes.

Representative analyzed binary identities:

| Binary | SHA-256 |
| --- | --- |
| `libMtkOmxVdecEx.so` | `1c3345fd1725df98d0da548c6eaadde172d011a2d09ff79de233920cb5cbc441` |
| `libMtkOmxVenc.so` | `46ec0c93f5fc5b6e6512125397d35236c5be5759dbdb285bd29c4c4714e181fe` |
| `libvcodecdrv.so` | `cfe53dae4fde4ef786a570e10c935e5b6bee2834914770b79c83002b756c8cd8` |
| `libmtkjpeg.so` | `4e0df786751a03b6b6173b8c2b51e5330c57bf5351c5cf876713af988f4df3da` |
| `libJpgDecPipe.so` | `2f3b9380f2a98a06346da8f0843ab6b36f3ec8992c3029788b2efb71eae075a6` |
| `libdpframework.so` | `3073925774e99529ba2932d5e2407dda18f124deab8cd09af02e46ba17be97e8` |
| `hwcomposer.mt8163.so` | `4964c8038f29c5bf0de144e1fdb99231875eb408aa3b0ed3cb21ebe6dcc6b63d` |
| `audio.primary.mt8163.so` | `b85ee01100fab15cba78b7706e45200150422a7cfcc51c7e2164b3c000a3c958` |
| `libGLES_mali.so` | `39b982d5433a3f18d77b4b8ded51b0d520d293c5241e06bc6bf701409742388b` |

## First bounded proof: JPEG decode

JPEG decode is the best first connected-device proof because its userspace and
kernel boundaries are small enough to audit before touching the broader video
pipeline.

For `libJpgDecPipe.so` at the hash above, the Ghidra export records:

- `jpegDecLockDecoder` at `0x00015dc0`, with a reference from `0x00015dd8` to
  `/proc/mtk_jpeg`;
- `jpegDecUnlockDecoder` at `0x00015e78`; and
- `jpegDecStart` at `0x000165cc`.

Static call and constant evidence maps those functions to the driver's INIT,
START, WAIT, and DEINIT requests. This matches the public MT8163-family header:

- INIT: `_IO('x', 1)` / `0x7801`
- START: `_IO('x', 4)` / `0x7804`
- WAIT: `_IOWR('x', 5, JPEG_DEC_DRV_OUT)`
- DEINIT: `_IO('x', 6)` / `0x7806`

The matching open driver defines the input/output structures, creates
`/proc/mtk_jpeg`, and implements the request handlers. Public reference:
[MediaTek JPEG driver in Android's open kernel tree](https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/jpeg/v2/jpeg_drv.c).
The local comparison also used the open `mt8163-dev/android_kernel_amazon_mt8163`
tree at commit `e0a3a2b2378ae4c7494ffdfea34d2a9e04a5fb74`.

This establishes a strong candidate ABI. It does not yet establish that a
running Tater image exposes the node, grants the needed permission, has the
same exact driver revision, or can safely decode with buffers allocated by our
userspace.

## What still requires the connected device

The first device session should remain read-only and answer these questions in
order:

1. Which kernel, device-tree variant, modules, media nodes, display nodes, and
   ALSA topology are actually active under stock and Tater?
2. Does `/proc/mtk_jpeg` exist, and which user/group and security policy can
   open it?
3. Does a traced stock JPEG decode use the same request order, structures,
   buffer allocator, cache synchronization, and M4U mapping inferred offline?
4. Can a separately reviewed, bounded probe decode one known image into a
   private buffer without display, audio, power, or persistent-state changes?
5. Only after output validation: what are latency, CPU load, memory bandwidth,
   thermals, timeout recovery, and repeated-run stability?

Video presentation and microphone processing remain later experiments. The
offline evidence identifies their components, but it does not justify issuing
their hardware-control requests without runtime observation and a separate
safety review.

The first runtime observation is now recorded in the
[Checkers v4.0.2 read-only baseline](checkers-v4.0.2-read-only-baseline.md).

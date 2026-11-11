# Checkers v4.0.2 read-only hardware baseline

Date: 2026-11-10

This note records a read-only USB-console inspection of an owner-operated Echo
Show 5 first generation (`checkers`). It intentionally omits the device serial,
network address, credentials, raw console capture, and proprietary binaries.
No partitions, files, mixer controls, sysfs/procfs controls, hardware ioctls,
or boot state were changed.

## Running platform

- Tater Linux Checkers `v4.0.2`
- Linux `4.9.337-g8d928c5176cc`, AArch64 kernel with 32-bit ARM userspace
- Device-tree model and compatible: `MT8163` / `mediatek,mt8163`
- Device-tree hardware revision: `dvt`
- Root and store mounted read-only; persistent data mounted separately

The running kernel reports these relevant built-in options:

- `CONFIG_MTK_JPEG=y`
- `CONFIG_MTK_M4U=y`
- `CONFIG_MTK_VIDEOCODEC_DRIVER=y`
- `CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT=y`
- `CONFIG_ION=y`
- `CONFIG_MTK_VIDEOX=y`
- `CONFIG_MTK_VIDEOX_LIVEDISPLAY=y`
- `CONFIG_SND_SOC_MT8163_AMZN=y`
- `CONFIG_SND_SOC_TLV320AIC3101=y`
- `CONFIG_SND_SOC_RT5616=y`

## Live hardware interfaces

The following interfaces are present on the running Tater image:

| Interface | Purpose/evidence |
| --- | --- |
| `/proc/mtk_jpeg` | MediaTek hardware JPEG driver |
| `/dev/Vcodec` | MediaTek video codec driver, character major 160 |
| `/dev/ion` | ION buffer allocation/import |
| `/dev/mali0` | Mali GPU; kernel identifies Mali T720 `0x0720` r1p0 |
| `/dev/mtk_disp_mgr` | MediaTek display manager |
| `/dev/mtk_cmdq` | MediaTek command queue |
| `/dev/MTK_SMI` | MediaTek shared-memory/interconnect control |
| `/dev/devmap` | Vendor mapped-device interface |
| `/dev/fb0` | `mtkfb`, 480×960, 32 bits per pixel, triple-height virtual buffer |
| `/dev/camera-isp` and related nodes | Existing direct camera/ISP implementation |

The Mali r26p0 kernel driver initially deferred until display initialization,
then identified and successfully registered the GPU as `mali0`. ION creates
four heaps, and M4U initializes its secure and non-secure mappings during boot.

## Audio and microphone topology

ALSA exposes one `mt-snd-card` with 24 PCM endpoints. The current Tater
processes demonstrate the intended paths without any test-time reconfiguration:

- The microphone process holds `hw:0,22` (`TLV320AIC3101 Capture`) as packed
  24-bit, 16 kHz, four-channel audio.
- `tater-echo` holds the control device, `pcmC0D1c` capture, and
  `pcmC0D23p` RT5616 playback.
- `tater-show` holds `/dev/fb0`.

Kernel logs confirm the Amazon MediaTek SPI PCM platform, FPGA revision 193,
TLV320AIC3101 capture codec, RT5616 playback codec, TDM capture, and the full
MediaTek ASoC routing graph.

## Exact live JPEG userspace boundary

The running rootfs retains the Android/MediaTek JPEG, OMX video, Vcodec,
M4U/ION, display-processing, and Mali userspace libraries. Their hashes differ
from the earlier stock backup, so conclusions were refreshed against the exact
live library rather than reusing stock addresses.

The live `/vendor/lib/libJpgDecPipe.so` is a 53,640-byte ARM32 ELF with SHA-256:

`62e3566f07d02a1df93e583e65464f6067a32fba330d0714d113663f516e4d8f`

The binary was copied read-only over USB serial into the ignored research
workspace. Ghidra analysis of that exact hash records:

| Function | Address | Observed request |
| --- | ---: | --- |
| `jpegDecLockDecoder` | `0x00016a0c` | opens `/proc/mtk_jpeg`, then INIT `0x7801` |
| `jpegDecSetSourceSize` | `0x00016fc0` | CONFIG `0x40d07802`, 208-byte input structure |
| `jpegDecStart` | `0x000172ac` | START `0x7804`, then WAIT `0xc0087805` |
| `jpegDecUnlockDecoder` | `0x00016acc` | DEINIT `0x7806`, then close |
| `jpegDecResetDecoder` | `0x00016ab8` | RESET `0x7807` |
| `jpegDecResumeRow` | `0x00017424` | RESUME `0x4010780b`, 16-byte input structure |

The encoded WAIT size is eight bytes, matching the public ARM32
`JPEG_DEC_DRV_OUT` layout of a 32-bit timeout and a 32-bit result pointer. The
library also imports ION buffers, obtains their MediaTek address, configures
M4U ports, synchronizes caches, and supplies the resulting addresses in the
208-byte decoder configuration.

This confirms the live userspace/kernel contract. It does not yet prove a
successful independently initiated decode, correct buffer/cache handling, or
safe timeout recovery.

## Safety gate before active probing

The next probe should be built and reviewed offline before it is copied to the
device. It must:

1. default to a dry run and require an explicit execution flag;
2. verify target, kernel, node identity, ABI sizes, and exclusive availability;
3. use a small known baseline JPEG and private ION buffers;
4. configure only the JPEG decoder's documented M4U ports;
5. enforce a bounded wait and always issue DEINIT/close during cleanup;
6. never write partitions, persistent storage, display state, clocks, power,
   thermal controls, mixer controls, or unrelated device registers; and
7. validate the output buffer without presenting it to the display.

Running that probe is a separate, explicit hardware action. It was not
performed during this baseline.

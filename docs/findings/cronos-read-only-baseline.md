# Cronos v4.2.2 read-only hardware baseline

Date: 2026-11-13

This is a USB-console inventory of the owner's Echo Show 5 second generation
(`cronos`), not a GPU/codec qualification. Serial numbers, addresses, pairing
data, raw console logs, and proprietary files are deliberately omitted.
No GPU/codec job, camera capture, register access, service restart, remount,
partition write, or firmware installation was performed.

## Running platform and recovery boundary

- Tater Linux Cronos and active application: `v4.2.2`.
- Kernel release: `4.9.337-g8d928c5176cc`.
- Device tree: `MT8163` / `mediatek,mt8163`; command-line product: `cronos`.
- Both root-system slots are marked `good`; the application runs from slot A.
  No system/application pending-trial file was found at the inspected paths.
- Root and `/store` are read-only. `/data` has about 3.3 GiB available;
  `MemAvailable` was about 760 MiB during the initial observation.
- Read-only GPT/sysfs inventory identifies `boot` as 16 MiB, `recovery` as
  16 MiB, and `swdl` as 32 MiB. The unlock's recovery routing must be preserved:
  a GPT label alone does not establish which image TWRP boots. No recovery
  reboot or partition-content verification was performed in this session.

The shared kernel release string does **not** establish Checkers kernel ABI,
camera timing, runtime, or firmware compatibility.

## Graphics

The running driver identifies Mali-T720 `0x0720` r1p0 and registers
`/dev/mali0`. Its version is `r26p0-01rel0` (UK version `11.11`). Early probe
attempts report deferred initialization; the later probe succeeds. These
messages alone are not evidence of an active render failure.

The panel reports `mtkfb`, physical mode 480x960, 32 bits per pixel, 1920-byte
stride, and a 480x2880 virtual framebuffer. This matches the geometry used by
Checkers' 960x480 logical rotation/swizzle helper, but that helper remains
Checkers-gated until Cronos has a separate guarded test.

The installed `/vendor/lib/egl/libGLES_mali.so` SHA-256 is
`dcb3eac3dd0f220124b39c5a44b0e8a4d5caeb475fe9465db45d8f5f9a75f04d`:
an exact match for the GPU DDK currently qualified on Checkers. This is a
promising compatibility input, not a completed EGL/pbuffer proof. No prepared
`/data/tater-gpu` runtime is installed.

## Video codecs

`/dev/Vcodec` and `/dev/ion` are present. No owner was reported by the
read-only `fuser` check at the observation point. Cronos has not completed a
lock cycle, decoder lifecycle, synthetic encode/decode, or sustained test.
Prepared `/data/tater-vdec` and `/data/tater-venc` runtimes are absent.

The retained vendor components are **not** the Checkers-qualified components:

| File | SHA-256 |
| --- | --- |
| `libMtkOmxVdecEx.so` | `812cd2a56b50e994fee60cdb92d3b0c82ff339d25b55c55d31ac9233f7b6ee89` |
| `libMtkOmxVenc.so` | `161020e59faad3b1d250dcc3db168ce9109c08c8b2f02bc736109356f7ed5db4` |
| `libvcodecdrv.so` | `6a9b8be27634d37a451a05cb359797289254d8250573973b8fc064f4ff07989e` |
| `libvcodec_utility.so` | `7f46d588d62a552ec00954d0ca23662390071150a6cef7c04f814ebfd6548313` |
| `libvcodec_oal.so` | `bd76ad061e9ce75358e575956133990b793a5f6631444b0c4713907e25f422bf` |
| `hw/gralloc.mt8163.so` | `caa3ef25813be03a8c68d03ac6a5a382f79e0df9e0e41f90861885200710b97d` |

All files remain on the owner's device. Do not bypass the existing firmware
runtime allowlists, impersonate `checkers`, or substitute a foreign runtime
merely because both devices use MT8163.

## Camera and audio

The live kernel config selects `ov02b10_mipi_raw`, not Checkers' OV9734.
The public source at kernel revision `8d928c5176cc` describes OV02B10 as
1600x1200, red-first Bayer, one MIPI lane, with a 24 MHz master clock and a
declared maximum of 30 fps. Those are **source values**, not measured live
capture rate, exposure, clock, or image quality. Existing Tater camera tests
also distinguish its 800x600 developed Bayer-cell image from Checkers' path.
No sensor-ID ioctl, clock mapping, or pixel capture was issued here.

The current camera adapter's native Bayer-to-H.264 transport is explicitly
restricted to Checkers' 1280x720 blue-first input. Cronos needs its own reviewed
geometry/color-order/encoder contract; do not simply remove that guard or copy
Checkers' sensor-clock patch.

The normal microphone stream remained `RUNNING` on PCM 22, with advancing
hardware/application pointers. The board exposes TAS5805M playback on PCM 23,
not Checkers' RT5616 route. Tater remained connected throughout the checks.
No microphone samples, transcripts, or camera scene pixels were collected.

Two camera helper processes exist in the installed baseline: the root-system
service and an application child. Their executable hashes are identical and
neither owned the camera nodes at the inspected idle point. This needs an
ownership/supervision review before an active camera experiment; it is not by
itself proof of a camera failure. Neither process was stopped.

## Evidence identities

| Input | SHA-256 |
| --- | --- |
| Live `/proc/config.gz` | `1b432be499df8a1be09d56ace7635b72f6e36ec1fd437aaaddee52a31e73b485` |
| Live `/sys/firmware/fdt` | `28973943bb8f5402d99c87329fa37145b3d0d6cab6d1c09e7197a2bffe9ef4cc` |
| Active daemon | `b7aef4bd664f695db06c6b1482fb7a3d9986361b1f5d1270ba457eea3f7d0b6f` |
| Active renderer | `65ef8f877296e1c3b82b42e9ba93976e0fe42ad28b90251a3187d5b6fceeff8b` |
| Both camera-helper executables | `30a32e8fe058bfa697c649ce7776a64380f512b3eb29937b39cfa6a5e9161e8a` |
| Public OV02B10 sensor source | `7a75ac0a8cbc19c02d2de8fc43a779fc9c79034b68f4635560bc7dd55b3a5040` |

Public sensor-source path:
`drivers/misc/mediatek/imgsensor/src/mt8163/ov02b10_mipi_raw/ov02bmipiraw_Sensor.c`.

## Next guarded milestone

With explicit operator approval and stable power, prepare a Cronos-targeted,
temporary offscreen GPU experiment. It must verify the runtime dependencies,
check the actual product and framebuffer geometry, use only synthetic pixels,
and leave the live renderer as sole framebuffer owner. Require completed
pixel readback and normal microphone/service health after the bounded job.
No screen presentation, camera acquisition, flashing, persistent enablement,
or Checkers source-pin change is part of that first test.

Then independently qualify the Cronos codec components with synthetic fixtures
before camera integration, and only install the shared UI through the normal
recoverable application/root-system workflow after the relevant gates pass.

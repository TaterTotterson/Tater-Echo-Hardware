# Checkers live media bring-up

Date: 2026-11-11

This record covers the first recoverable Checkers kernel trial and the first
complete hardware-video decode from an independent Tater probe. The test unit
is owner-operated. Device identifiers, stock binaries, linker/runtime files,
raw frame contents, backups, and calibration data remain in ignored local
storage.

## Recovery and trial identity

Before the trial, the retained TWRP fallback was booted and the original
`boot` image was read back and hashed. The candidate was then written only to
the bootloader's named `boot` target. Neither physical `recovery` nor `swdl`
was modified.

The running candidate reported the exact required release
`4.9.337-g8d928c5176cc`. Its boot-image SHA-256 was
`07cf99a5ad51cfa1f7eb697245a669057594d04a609f1ab481054123c67c7715`.
The preserved rollback image SHA-256 was
`3bee39e205d4a6499bdac7ab9be35f742376426f2218c81c587b8dd4394ce8df`.
The candidate remained stable throughout the tests, and recovery remained
available.

After the media tests, the read-only diagnostic module was removed and the
original image was restored through the same bootloader-named `boot` target.
The rebooted unit reported the original kernel build and a live readback of the
13,492,224-byte boot payload matched the preserved rollback SHA-256 exactly.
No experiment remains loaded or flashed on the device.

## Standalone JPEG decoder result

The experimental `0x17004000` JPEG decoder inference is rejected for this
MT8163 variant.

The candidate driver bound and completed its INIT/DEINIT control cycle. A
private ION allocation, CPU mapping, cache synchronization, and M4U setup also
succeeded for the inferred JPEG source and destination ports. A real 8x8
baseline-JPEG START then timed out with status `0x5`; no decoder interrupt was
raised and the destination sentinel did not change.

A read-only diagnostic module compared three regions while their clock domains
were deliberately held:

- the VENC clock gate at `0x17000000`;
- the known JPEG encoder at `0x17003000`; and
- the inferred decoder at `0x17004000`.

The encoder showed coherent live state while its INIT was held, including a
set reset register, stall and restart values, image dimensions, and the global
ultra threshold. The inferred decoder region remained entirely zero during
the decoder INIT, during START, and even with all four VENC clock gates forced
active. The mapping and read method are therefore known to work, but the
inferred decoder block is absent or not implemented at that address. The
public stock MT8163 kernel intentionally exposing only the encoder is
consistent with the live result.

The experimental patch remains as an auditable negative result and must not be
used by production firmware.

## VDEC hardware-control proof

The stock `/dev/Vcodec` compatibility ABI was exercised independently with a
target-gated ARM32 probe. A guarded H.264 decoder lock produced all expected
hardware transitions:

- the VDEC power domain changed from off to on;
- `vdec_cken` and `vdec_larb_cken` became active;
- Linux IRQ 262 appeared as `Vcodec`; and
- the matching unlock removed the interrupt and returned both clocks and the
  domain to the idle state.

The public MediaTek driver has two legacy positive success returns: first-use
lock status `4` after acquisition and common ioctl-tail status `255`. The probe
accepts only zero and those exact values, blocks termination signals across the
lock/unlock pair, and otherwise fails closed.

## Stock decoder capabilities

The owner-local ARM32 library was loaded with its exact stock Android runtime
from volatile storage. Its published `eVDecDrvQueryCapability` interface
reported hardware decoding for:

| Format | Maximum | Alignment |
| --- | --- | --- |
| MPEG-2 | 1920x1088 | 16x32 |
| MPEG-4 | 1920x1088 | 16x32 |
| H.263 | 1920x1088 | 16x32 |
| H.264 | 1920x1088 | 16x32 |
| H.265 | 1920x1088 | 64x64 |

The decoder type was MediaTek hardware for every reported format. A separate
guarded lifecycle test created and released an H.264 handle without touching
hardware.

The old stock video allocator can allocate an ION buffer in this transplanted
runtime, but its Android-era mmap path returns `MAP_FAILED`. The probe does not
dereference that result. Instead, the published `eVideoAllocMVA` API was
validated with a private anonymous mapping: it supplied a temporary VDEC MVA,
kept the CPU mapping readable and writable, and removed the mapping cleanly.

## Complete H.264 hardware decode

The final bounded test used a locally generated, 107-byte Annex-B fixture:
one 128x128 constrained-baseline IDR with separate SPS/PPS and no SEI. The
fixture and decoded pixels remained in volatile storage.

The probe performed these steps:

1. mapped one private combined input/output region into VDEC;
2. created the H.264 decoder and supplied SPS/PPS to the published init API;
3. verified returned buffer and picture dimensions of 128x128;
4. selected the reported 16x1 block YUV type and external shared-buffer mode;
5. supplied the IDR and one private output frame;
6. issued the documented null-input flush for the final frame;
7. invalidated the CPU cache through the stock utility; and
8. deinitialized, released, unmapped, and freed every resource.

The decode completed with result zero. Clock tracing showed the real
`vdec_sel`, `vdec_cken`, and `vdec_larb_cken` transitions. All 24,576 bytes of
the bounded YUV420 output region changed from the sentinel. The output had
three distinct values, appropriate for the solid-color fixture, and FNV-1a
hash `3046432197`. No frame was displayed or saved.

This is the first complete proof that an independent Tater userspace can
control the Checkers VDEC hardware, map private buffers, initialize the stock
decoder, and recover a decoded frame without Android's media service.

## Exact decoder layout and clean conversion

A second bounded test used locally generated SMPTE-style color bars so every
plane and tile boundary could be compared against a software-decoded YUV420P
reference. Both inputs remained in ignored local storage. The hardware buffer
matched all 16,384 luma bytes when interpreted as raster-ordered 16x32 tiles,
and all 8,192 chroma bytes when interpreted as interleaved UV in
raster-ordered 16x16 tiles. No competing candidate layout produced the complete
match.

This resolves the decoder's format beyond the legacy `16x1 block` enum name.
The clean `mtk_yuv420_block` implementation now validates the padded and
visible dimensions, detiles those two planes, and converts limited-range
BT.601 YUV into RGBA8888. It has a host-side synthetic self-test and is linked
into the target layout probe so the actual decoded hardware buffer is also
converted in volatile memory. The target reports only dimensions and hashes;
it never saves or presents the frame.

Ten consecutive create/init/decode/flush/convert/deinit/release cycles then
completed with zero failures and the same 65,536-byte RGBA hash each time.
This checks repeatable cleanup as well as the one-shot success path.

The remaining presentation work is consequently an integration step, not a
pixel-layout unknown: pass the converted RGBA region to Tater's existing
`PresentTransientRegions` path while preserving its single ownership of the
primary framebuffer.

The ABI definitions used by the clean-room probe come from MediaTek's
published public header:
[vdec_drv_if_public.h](https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/videocodec/include/vdec_drv_if_public.h).

## Sustained VDEC buffer turnover

The public header defines the buffer, decode-mode, display-buffer, and
free-input contracts, but its published decode call does not accept the
output-buffer callback used by the stock streaming path. A narrow owner-local
ABI audit was therefore pinned to these exact inputs:

- `libvcodecdrv.so` SHA-256
  `cfe53dae4fde4ef786a570e10c935e5b6bee2834914770b79c83002b756c8cd8`;
- `libMtkOmxVdecEx.so` SHA-256
  `1c3345fd1725df98d0da548c6eaadde172d011a2d09ff79de233920cb5cbc441`.

Export and call-boundary evidence identified the owner-local four-argument
`eVDecDrvDecodeEx` entry point and the 12-byte output-callback registration.
No decompiled implementation or private header was copied into the probe. The
independently written shim supplies only an owned context and callback, and
the callback rejects exhaustion instead of allocating or guessing addresses.
The public `FEATURE_SUPPORTED` query returned result zero but reported the
`QUEUE_INPUT_BUFFER` bit clear. The stock Checkers decoder therefore does not
advertise multi-input queuing through its public contract.

The live synthetic stream test then used ten visually distinct 128x128 IDR
access units. Each access unit occupied a separate 64-byte-aligned input
mapping and each output callback received a different persistent frame slot.
The decoder released all ten requested input descriptors, requested ten output
buffers, and returned ten distinct converted-RGBA hashes. Those hashes matched
the complete software-decoded reference set exactly. The decoder's display
queue reported the repeated standalone IDRs with a one-picture rotation; a
bounded copy of the first synthetic access unit was used solely as lookahead
to release the retained final requested picture.
Ten complete create/init/10-picture/deinit/release repetitions then returned
the same ten frame hashes, slot sequence, callback count, and fold hash with no
failures—100 hardware-decoded pictures in the repeatability run.

A conventional one-IDR-plus-nine-P-picture fixture was also tested as a
boundary check. The raw owner-local queued-input route requested no output
buffers, and repeating parser configuration around synchronous public calls
produced buffers that did not match the reference. That experiment remains
rejected rather than counted as playback.

The stock OMX component proved to be the correct higher-level normal-GOP
boundary. The probe used its standard Android native-buffer extensions with
the matching owner-local Android 5.1 gralloc runtime. Initial native-buffer
registration exposed one hidden environmental dependency: the component
unconditionally constructs MediaTek's optional GUI-extension client, whose
default constructor waits forever for Android's Binder service manager. Tater
Linux intentionally does not run that service. A bounded compatibility shim
returns a local placeholder for that singleton and reports its optional pool
allocation unavailable. The decoder's ION import, MVA mapping, VDEC work,
callbacks, cache management, and gralloc surfaces remain the stock paths.

The component completed Loaded-to-Idle-to-Executing, negotiated its output
port from planar YUV to vendor-native color format `0x7f000001`, registered 11
direct-ION output surfaces, accepted all ten access units, returned ten output
timestamps through EOS, and completed Executing-to-Idle-to-Loaded teardown.
The surfaces used the already-proven 16x32 luma and 16x16 interleaved-UV block
layout, so the clean converter was extended to produce tightly packed
YUV420P. The resulting 245,760-byte output matched the software reference at
every byte. Both files had SHA-256
`2e099b0db78e6acc0cf83f41e9177694f7211d71a35506a0b6e4d7542b06a23c`.

The same cleaned path then decoded a ten-frame
`I,B,B,P,B,B,P,B,B,P` stream. Its tightly packed YUV420P output again matched
the software reference at every byte and in the same presentation order. Both
245,760-byte files had SHA-256
`fd14dff096322d0287976afb8b5ab33c089b803951c0f51982c9cc287b74af7c`.

This establishes sustained hardware decode, external output-buffer turnover,
cache synchronization, detiling, and reference-correct conventional IDR/P/B
decode and presentation ordering—not merely ten independent one-shot
initializations. The stock owner-local runtime remains non-redistributable.
All fixtures and decoded pixels stayed in volatile or ignored local storage,
and no display path was opened during these tests.

### Bounded `tater-vdec` adapter and full-size frames

The proven lifecycle was then factored into a standalone ARM32 `tater-vdec`
process adapter. It reuses the exact state, port-reconfiguration,
native-buffer, cache, and teardown implementation above; the diagnostic probe
and adapter do not carry separate copies. The adapter takes its component path
at runtime, contains no vendor binary or fixed device path, and emits bounded
YUV420P or RGBA8888 batches in presentation order.

Three normal I/P/B fixtures exercised actual display and HD dimensions. A
60-picture 960x480 run completed in 1.02 seconds and its 41,472,000 output
bytes matched the software reference exactly. A 30-picture 1280x720 run
completed in 0.97 seconds and its 41,472,000 bytes also matched exactly. The
component reported a 1,413,120-byte 1280x736 native surface, which the clean
converter cropped to the visible 720 rows. A 20-picture 1920x1080 run completed
in 1.29 seconds; its 62,208,000 bytes matched exactly after cropping the
component's 3,133,440-byte 1920x1088 surface. This closes the previous
assumption that the 128x128 layout proof would necessarily generalize to
non-tile-aligned video heights.

The adapter also emitted 30 tightly packed 960x480 RGBA pictures directly.
The limited-range BT.601 conversion had 56.87 dB average PSNR against
FFmpeg's swscale RGBA output with an exact alpha plane; the small difference is
coefficient rounding, while the source YUV remains byte-exact. An
operator-present two-second run passed those normal B-frame pictures through
Tater's existing transient framebuffer API. It completed 19 presentations,
recorded worst framebuffer conversion and pan times of 36.543 and 16.043 ms,
and restored the renderer as the sole `/dev/fb0` owner. The presenter slept
33.3 ms after each completed pan, so this count is deliberately not reported
as the panel's maximum rate.

The same process now also accepts length-prefixed Annex-B access units on
standard input and emits fixed-size RGBA pictures on standard output. A
30-picture 960x480 stream completed in 0.97 seconds and was byte-identical to
batch mode, SHA-256
`8a48b65ea4d5d01332b1a793ab92863c402ea628dccd73384ad497bee7dc913b`.
Two separate 180-picture tests crossed the original 120-slot diagnostic
storage limit and produced the same 331,776,000-byte output, SHA-256
`dfeded23bf7a94c243118ddc95a04f989af275963698066729a2f2746bda992d`.
Each run fed and copied 180 pictures, saw EOS, reported no write failures, and
completed decoder teardown. Sending output to `/dev/null` took 4.11 seconds,
about 43.8 pictures per second; target-side hashing of the 316 MiB output took
roughly 15 seconds and is not decoder throughput.

This established a reusable picture-stream boundary. The later opt-in Tater
orchestration now supplies incremental packetization, cancellation, strict
startup validation, and first-frame audio gating. Source packet timestamps,
audio-clock frame dropping, seek/pause/loop coverage, and long-run pressure
remain release gates. The private Android component and its runtime remain
owner-local; no such file was added to Git. The safe handoff and fallback
conditions are recorded in the
[Tater VDEC integration plan](../tater-vdec-integration.md).

Protocol v2 then made the handoff self-describing. A connected 30-picture run
reported negotiated 960x480 RGBA geometry, carried timestamps from 0 through
966,666 microseconds, emitted explicit EOS, and retained the exact v1 pixel
SHA-256. A truncated first record failed before feeding the decoder. Closing
the output consumer early also failed closed; after correcting the fatal
component-error path, the helper exited 1 without a segmentation fault and
`/dev/Vcodec`, `/dev/ion`, and `/dev/m4u` were idle while the normal Tater
renderer remained the sole framebuffer owner. A subsequent complete stream
returned the same framed SHA-256, demonstrating recovery after failure.

The pinned ARMv7 FFmpeg was then rebuilt from its checksum-pinned 8.1.2 source
under ARM emulation. Its runtime inventory confirmed the H.264 muxer and both
required bitstream filters. A first copy test revealed that `aud=insert` alone
duplicates delimiters when the source already has them. The final copy chain
uses `h264_metadata=aud=remove,h264_metadata=aud=insert`; its output contained
exactly 30 bounded access units rather than 60 alternating empty units.
Those normalized units then completed the real Checkers path with 30 inputs,
30 copied pictures, EOS, zero output failures, full teardown, and idle Vcodec,
ION, and M4U nodes.

The complete Tater process boundary was then exercised over HTTP with a
960x480 H.264/AAC MP4. Unprivileged FFmpeg performed URL access, demux, AAC to
PCM, Annex-B conversion, and AUD normalization; the Go packetizer parsed the
first SPS before starting the privileged helper. The helper returned all 30
RGBA pictures, 55,296,000 bytes, with the same exact SHA-256 as the direct
reference above. The audio gate released 96,256 PCM bytes only after the first
validated frame. A deliberately mismatched 1280x720 request rejected the
960x480 SPS before helper startup and fell back to software. That result and a
software-only control had identical frame and audio byte counts and hashes, so
the failed attempt leaked neither output. The live renderer and services were
not replaced, and the Vcodec and ION nodes were idle afterward.

## Complete Mali off-screen render

The current owner-local r26p0 Mali DDK was paired with Google's public Android
9 ARM VNDK snapshot and a narrow clean-room Bionic compatibility layer. The
test disabled only the optional GED logging, gralloc-extra, and GPU-aux
native-window paths; it did not replace Mali memory allocation, command
submission, synchronization, or pixel readback.

The target-gated probe initialized EGL 1.4, created an OpenGL ES 2 context and
a private 32x32 pbuffer, cleared it to a fixed RGBA color, completed the job,
and read the pixels back. The live identity was:

- EGL vendor `ARM`;
- GL vendor `ARM`;
- renderer `Mali-T720`; and
- OpenGL ES 3.1 DDK `v1.r26p0-01rel0`.

All 1,024 pixels matched within one channel value and the complete 4,096-byte
readback had FNV-1a hash `2057723333`. During a repeat, GPU IRQ 230 advanced
from 1 to 2 and job IRQ 232 advanced from 11 to 19. The probe never opened the
framebuffer or display manager and reported `display_touched:false`.

Five further independent EGL/context/pbuffer/render/readback/teardown cycles
all returned 1,024 matching pixels with no failures. The owner-local runtime
therefore survives repeated complete setup and teardown rather than depending
on retained process state.

This proves independent userspace access to the actual Checkers GPU and a
completed hardware-render/readback cycle. It is not yet a native-window or
display-plane presentation claim; those paths remain intentionally separate
from the off-screen proof.

## Tater hybrid GPU compositor

The off-screen proof was then converted into a bounded, long-lived clean-room
helper for Tater's real 960x480 RGBA canvas. Its GLES2 shader rotates into the
native 480x960 panel orientation and emits the panel's BGRA byte order. An
exact target-side self-test compared every output pixel rather than a sample:
all 460,800 pixels matched. GPU IRQ 230 advanced from 0 to 2 and job IRQ 232
from 1 to 10 during that job.

The helper's internal upload/draw/readback benchmark averaged 7.612 ms and
reached 12.679 ms at worst across 60 frames. The complete production-shaped Go
pipe round trip, including 1,843,200 input and output bytes per frame, averaged
12.744 ms and reached 18.532 ms after the exact reviewed runtime was installed.
The runtime preparation tool hashes the complete linker/library tree, copies
only that allowlisted set plus the open helper, and emits a read-only identity
manifest. Those proprietary owner-local libraries remain ignored and absent
from the public rootfs.

Tater keeps framebuffer ownership in Go. It sends only full logical canvas
bytes to the helper, receives the result into reusable normal RAM, copies one
contiguous frame into the next inactive framebuffer page, and uses the
existing pan ioctl. Receiving directly into uncached framebuffer memory was
measurably slower and was rejected. On the continuously changing live screen,
the final staged path reduced conversion from the CPU control's 25 ms to 17 ms
and improved the steady renderer from 29.2 to 29.7 fps; total presentation
remained approximately one 30 Hz frame because pan waits for the next display
boundary. Small dirty rectangles continue to use the faster CPU region path.

This is GPU-assisted Tater UI presentation, not a Panfrost/Mesa desktop or a
native Android window transplant. The helper never opens `/dev/fb0` or
`/dev/mtk_disp_mgr`, any failure leaves the framebuffer staging page untouched,
and the renderer automatically retries the complete update on CPU.

## Display-manager contract

A separate static ARM32 probe opened `/dev/fb0` and `/dev/mtk_disp_mgr`
read-only. It did not mmap framebuffer memory, pan, blank, allocate a layer,
submit a buffer, or trigger a session. The live primary-display query reported:

- `mtkfb`, 480x960 visible and 480x2880 virtual, 32 bits per pixel;
- 1,920-byte stride and a 5,529,600-byte three-page backing store;
- connected DSI0 in video mode, RGB888, with hardware vsync;
- raw refresh value 5893, or 58.93 Hz in the driver's hundredths-of-Hz units;
- eight layers in the primary session and a four-layer hardware capability;
- direct-link, single-pass output with `DISP_FEATURE_NO_PARGB`; and
- LCM index 5, no hardware output rotation, and no frame-config ioctl.

The 32-bit `DISP_IOCTL_GET_IS_DRIVER_SUSPEND` request returned `EFAULT`. This
is a confirmed kernel compatibility bug, not an unknown ABI: the public compat
handler stores the caller's value in a scalar and passes that value to the
native handler as a user pointer. A bounded source fix and provenance checker
are in `kernel-patches/mt8163-display`; nothing was flashed to test it.

The query completes the non-mutating display contract and confirms the
existing Tater framebuffer renderer is already using the kernel's native
three-page layout. Presentation of a private decoded or GPU-rendered buffer is
still a distinct active test.

The remaining buffer/fence/trigger contract was then mapped from the exact
public driver without executing it. Android's composer route imports an ION
file descriptor with `PREPARE_INPUT_BUFFER`, attaches the returned buffer index
to one of the validated overlay configurations with `SET_INPUT_BUFFER`,
optionally obtains a present fence, and commits the cached primary state with
`TRIGGER_SESSION`. Checkers advertises four hardware overlay layers and rejects
formats beyond the RGB565-through-YV12 range used by this generation. The
decoder's selected 16x1 block-YUV output therefore needs conversion before it
can be presented.

The current Tater framebuffer implementation already supplies the safer
integration point: `PresentTransientRegions` writes frequently replaced RGBA
content into the next inactive framebuffer page without polluting the static
UI caches, then pans to that page. This preserves one display owner and avoids
introducing a competing primary-session fence timeline.

An operator-present run then decoded the 128x128 synthetic H.264 fixture,
invalidated its output cache, converted the complete block-YUV surface to a
65,536-byte RGBA frame, and passed it to an uninstalled diagnostic command
using exactly `PresentTransientRegions`. The command scaled it to a 960x480
canvas, added unmistakable orientation markers and a moving progress bar, and
presented 44 frames in 6.101 seconds. Maximum measured conversion and pan times
were 92,809 and 21,646 microseconds. The sole existing renderer was stopped for
exclusive framebuffer ownership and restarted immediately afterward; its PID
changed from 479 to 4809. This completes the decoded-frame presentation proof
without adding a competing display-manager timeline or persistent install.

The same boundary was then exercised as an actual moving sequence. The
persistent decoder consumed ten distinct synthetic IDR access units, released
all ten input descriptors, supplied ten output slots, produced ten unique RGBA
hashes, and wrote the explicitly requested 655,360-byte volatile RGBA sequence.
Its SHA-256 was
`71d17e1002bfe1644097e2e92866f6c21df409bf070a934c138428a879fb6cbb`.
The single-owner presenter cycled those ten hardware outputs on screen for
6.010 seconds, completing 39 presentations; worst measured conversion and pan
times were 70.128 and 16.689 milliseconds. The renderer supervisor was held
while PID 4809 exited, then resumed and restored the normal renderer as PID
7152. Audio remained continuous afterward, both camera helpers stayed running,
and VDEC/camera/ISP/ION/M4U had no temporary owner. This proves moving
hardware-decoded content reaches the real panel. That presentation used the
synthetic all-IDR sequence; the separate normal IDR/P/B OMX proofs above
remained off-screen.

## Other live hardware surfaces

The same kernel exposes:

- `mtkfb` at `/dev/fb0`, 480x960 visible with a 480x2880 triple-buffered
  32-bit virtual surface;
- `/dev/mtk_disp_mgr` for the MediaTek display pipeline;
- a MediaTek ALSA card with playback, primary capture, TDM capture, loopback,
  and the external codec routes; and
- touchscreen, keys, proximity/light, and IIO sensor interfaces.

These inventories establish access paths but are not claims that every path
has completed an active test. Microphone capture still requires privacy-safe,
operator-present measurement because recordings can contain private ambient
audio. Decoded-frame presentation has now completed through the reversible
inactive-page path used by the existing Tater renderer.

## Camera and sensor boundary

The exact live device tree reports the Checkers DVT hardware variant. Its
camera and ISP platform drivers are bound. No camera was powered while the
operator was away; the later active proof was operator-present and restricted
to the sensor's internal test pattern.

The exact Checkers kernel configuration selects one sensor driver:
`ov9734_mipi_raw`. The corresponding public driver defines a 1280x720,
30-frame-per-second, one-lane MIPI RAW Bayer sensor with a 24 MHz master clock
and 36 MHz pixel clock. The kernel exposes this through `/dev/kd_camera_hw`,
`/dev/kd_camera_hw_bus2`, `/dev/camera-isp`, `/dev/camera-pipemgr`, and
`/dev/camera-sysram`; there is no V4L2 `/dev/video*` capture node. Independent
camera support therefore needs a clean MediaTek private-ABI adapter or a new
V4L2 kernel bridge, not merely a standard V4L2 userspace client.

The live GPIO-key input includes the camera-lens-cover switch. That gives an
independent firmware a kernel-reported privacy signal that should gate any
future camera stream. The deterministic `mt8163-camera` inventory reproduces
these source-derived facts and always reports `camera_powered:false` and
`capture_performed:false`; those flags describe that read-only command rather
than the separate active proof.

A separate static ARM32 query then opened only the matching input-event node
read-only and retrieved `SW_CAMERA_LENS_COVER` with `EVIOCGSW`. It identified
the `gpio-keys` provider and reported the switch inactive. The probe did not
open any camera or ISP node. No assumption was made about the physical scene,
and the inactive privacy signal is one reason no unattended camera test was
attempted.

With the owner present, a bounded active run selected the OV9734 standard color
bars and required register `0x5080` to read back as `0x10` before enabling ISP
DMA. It consumed eight 640x360 frames in memory and retained only four hashes
and vertical-band RGB summaries; no pixels were saved. Stable band profiles,
independent pattern-register readback, and the kernel's PowerON/control/close/
PowerDown sequence verify the sensor-to-ISP path. Reverse teardown returned the
camera, ISP, ION, and M4U nodes to idle, disabled the final ISP clock, left the
privacy state unchanged, and resumed both existing camera helpers.

This run also found an important ABI trap: the live config has
`CONFIG_CHECKERS` unset, so it uses the default feature enum even on a Checkers
product. The correct register set/get and test-pattern IDs are 3011, 3012, and
3043; the product-named header is shifted by one and invokes the wrong
operations. The tracked ABI probe now derives these constants through the same
generic header switch as the kernel.

The live read-only inventory also confirmed a Goodix touchscreen, headphone
accessory-detect input, ambient-light/proximity input, auxiliary ADC IIO
device, LCD backlight LED class device, ten thermal zones, and the lens-cover
GPIO switch. These paths complete the non-invasive surface map; they do not
claim active camera or microphone validation.

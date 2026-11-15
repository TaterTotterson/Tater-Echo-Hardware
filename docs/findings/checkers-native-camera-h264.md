# Checkers native camera capture and H.264

Date: 2026-11-12

This implements and qualifies a bounded development path on the connected
Checkers. The corrected kernel is now installed and boot-tested after verified
backup/recovery checks; no scene pixels were saved and no vendor binary is
included in tracked source or public artifacts.
Other Echo targets remain unqualified and keep their existing camera paths.

## Camera clock finding

The operator confirmed that the room light was already on during the earlier
slow-camera tests. Read-only clock diagnostics found a concrete configuration
problem: CAMTG selected 26 MHz and SENINF divided it by two, so OV9734 received
13 MHz rather than its intended 24 MHz. The live DT deliberately omits
`cmmclk-always-on`. In the exact public kernel, `CAMERA_HW_probe` acquired its
CCF clock handles only inside that optional always-on block. Normal
`SET_MCLK_PLL` therefore used NULL handles and silently failed to select the
48 MHz parent.

`kernel-patches/mt8163-camera/0001-initialize-clock-handles-without-always-on-power.patch`
acquires those handles unconditionally, propagates deferred-probe and clock
errors, and selects the parent before enabling it. It does not alter the DT,
privacy GPIO, sensor register tables, or public module ABI. Its SHA-256 is
`d560ce7f808230a97b9c8fd94c1ca4e5d09a75bc94ccb39f1ae04e7f293ff403`.
The same patch is wired into the Tater Linux Checkers-only kernel build.

A lab-only clock proof module temporarily selected the same 48 MHz parent
while the normal camera helper retained exclusive sensor/ISP ownership and
privacy handling. Readback then showed source 1, divisor 2, and 24 MHz; frame
delivery reached approximately 30.6 fps. Each test stopped the new helper,
waited for reverse camera teardown, unloaded the module (restoring the old
parent), resumed the original helper, and restored the original CPU floor.
That initial experiment was an in-RAM clock proof, not a boot test. The later
permanent-kernel qualification is recorded below.

The complete kernel build used separate cloned source/output volumes and the
original Arm GCC 8.3-2019.03 toolchain. The modified sensor object was explicitly
rebuilt before the final link. Results:

| Artifact | SHA-256 / identity |
| --- | --- |
| release | `4.9.337-g8d928c5176cc` |
| `.config` | `93bde8f9783a91ad4e5626030fd81b5f60112903d4466a24d08798c2d9568271` |
| `Image.gz-dtb` | `758cb695409303453232e4313574af005a670619007df47c479afb473344e957` |
| `Module.symvers` | `52452f76379b78dde03edd2799814253492fb75504b53ccc5f3b6e2074060332` |

`Module.symvers` is byte-identical to the earlier display-fix build. No
unreviewed always-on camera workaround is needed.

## Permanent kernel qualification

The unit's retained TWRP booted successfully and supplied a root USB ADB shell.
Both physical recovery images and the original boot partition were backed up
privately before writing. TWRP intentionally redirects its `by-name/recovery`
link to `swdl`; that is its unlock arrangement, not a partition-label defect.
Neither recovery image, the bootloader, nor the partition layout was changed.

The exact current boot image was repacked with the corrected kernel while
preserving its rescue ramdisk byte-for-byte. Candidate boot SHA-256:
`318f509bebc5cae3ed9ad7ab66ac9381a348535ba661b4bc0460800d3a519aa4`.
After exact target/old-image checks, only boot was written; readback matched
the complete candidate extent. Normal boot reached the retained good root
slot with the exact expected kernel release/configuration and working Wi-Fi,
ALSA microphone capture, framebuffer, touch, and ambient-light input.

Camera readback showed 24 MHz without the clock-proof module. A twenty-second
native capture run returned 594 pictures (29.70 fps) without forcing the CPU
floor. Complete final-candidate/root-slot, gesture, and physical behavior
qualification is still separate from this successful kernel boot.

## Encoder boundary

`probes/mt8163-vcodec/venc_probe_android.c` is an original OpenMAX IL client
built against public headers. It uses the owner-local
`OMX.MTK.VIDEO.ENCODER.AVC` component with bounded private buffers, normal
Loaded/Idle/Executing transitions, baseline H.264, no B pictures, 2 Mbit/s,
and periodic IDR requests. Component parameter headers require the complete
version word `1`; the decoder's version word is not interchangeable here.

The helper is ARM32 with the existing owner-local VDEC linker and dependencies.
It accepts no URL, credentials, camera node, microphone, or framebuffer.
`--capabilities` and `--describe` are non-hardware modes. Active modes check the
Checkers boot identity and exact running kernel release. Synthetic mode writes
only a new explicitly named ordinary output; live mode uses pipes.

Qualified runtime identities:

- encoder helper:
  `5431a4fb2b32378313307b4550e71842df23fb155acf04e3a816662312df2e7d`;
- device-owned `libMtkOmxVenc.so`:
  `46ec0c93f5fc5b6e6512125397d35236c5be5759dbdb285bd29c4c4714e181fe`;
- existing VDEC dependency tree:
  `490a3c771d1c6f979828e60b93c363ec42403ca9bd3c98b12f8c5cd0761f2cc4`.

`scripts/prepare_tater_venc_runtime.py` verifies the two extension inputs and
creates a new read-only owner-local runtime directory. It refuses an existing
output. The firmware helper separately validates its manifest and component,
helper, and two base codec-library hashes before enabling capture.

The camera driver copies packed 10-bit BGGR plus tone tables, not full RGBA.
`tater_bayer10.c` fuses mirrored-edge bilinear development, table lookup, and
limited-range BT.601 planar YUV420 conversion in four bounded bands. Independent
scalar-reference tests match all output pixels exactly, including edges.
Bayer/color processing remains CPU software; H.264 encoding is hardware.
Factored tone tables match the platform within one LSB, and analysis runs at
most 10 Hz. Live H.264 may use the sensor's permitted gain before extending
shutter; normal still-photo exposure remains unchanged and is restored when
the live acquisition ends.

## Streaming contracts

Raw input to `--stream-bayer10` has a 16-byte little-endian header:
`uint32 bytes`, `uint32 flags` (bit 0 requests IDR), `int64 pts_us`, followed by
6144 bytes of R/G/B tone tables and 1,152,000 packed Bayer bytes. Timestamps
must increase; zero size or clean EOF ends input.

Encoder output begins with 32-byte `TVE1`: header size, 1280×720 geometry,
`H264`, nominal rate 30/1, and maximum packet size 4 MiB. Each output record has
`uint32 bytes`, `uint32 flags` (EOS 1, config 2, keyframe 4), `int64 pts_us`,
then Annex-B bytes. Firmware further restricts transmitted pictures to 1 MiB.
Diagnostics stay on stderr and the Go adapter retains only a bounded tail.

Sat Chat adds `TSCV2`, with a 41-byte envelope:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 5 bytes | `TSCV2` |
| 5 / 9 / 13 | uint32 each | width / height / nominal FPS |
| 17 / 21 | uint32 each | sequence / flags (keyframe bit 0) |
| 25 / 33 | uint64 each | source PTS in microseconds / nonzero stream ID |
| 41 | bytes | bounded Annex-B access unit |

Every transmitted IDR is prefixed with the current SPS/PPS. A gap, epoch
change, or non-monotonic PTS discards predictive state and waits for a fresh
keyframe. Reset now sends EOS and drains/discards old output before reaping;
a 1.5-second deadline retains forced-kill cleanup for an unresponsive helper.
The initial kill-only reset passed simple playback but stalled after several
deliberate losses, so the qualification probe now requires picture delivery
near the end of its run, not merely a nonzero initial frame count.

Both Tater and its Home Assistant bridge negotiate `echo-h264` only for two
Checkers endpoints advertising the qualified encoding/decoding capability.
Mixed targets, older hosts, and absent runtimes retain JPEG. Reserved or
malformed video is consumed as video, never interpreted as voice PCM.
The stable loopback relay endpoint uses a one-deep queue and no redirects;
its receiver independently drains TCP into a bounded queue (now 32 pictures
to retain one startup GOP plus lookahead).
Overflow discards the dependent GOP and waits for an IDR rather than building
a stale compressed-video backlog or arbitrarily dropping predictive frames.
Checkers keeps its existing aspect-preserving call viewport. No display or
microphone service was replaced for these in-memory tests.

## Initial bounded results

- Synthetic 720p hardware encoding: 60 pictures in approximately 0.65 seconds;
  real encoder interrupt activity incremented with picture submissions.
- Native capture/encoding only, corrected clock: 449 pictures in 15.000 s,
  29.93 fps, with a steady 10-second helper interval reporting 30.5 fps and
  approximately 2.04 Mbit/s.
- Concurrent capture → production loopback relay → hardware decode:
  517 captured and 384 decoded in 20.027 s, 19.17 decoded fps, first picture
  at 643 ms. This measures both directions' processing on one unit.
- Final no-loss relay regression after startup/reset hardening: 388 captured
  and 282 decoded in 15.032 s, 18.76 decoded fps; the last picture arrived at
  14.975 s and the maximum picture gap was 73.939 ms.
- Repeated predictive-picture loss after graceful reset: 511 captured,
  12 deliberately dropped, 119 decoded in 20.027 s; decoding continued to
  19.140 s. Maximum observed picture gap was 2.441 s. Recovery is functional,
  but repeated loss materially reduces delivered FPS.
- Native still metadata: 1280×720 JPEG, validated in memory after streaming.
- Camera HTTP/protocol/snapshot tests, tone/exposure/teardown tests,
  independent C pixel-reference tests, firmware race tests, rootfs tests, and
  both host negotiation/relay test suites passed during development.

## Receive/conversion regression and sustained result

Two initial five-minute relay/decode attempts ended unexpectedly around
188–190 seconds. The original receiver was already approximately four seconds
behind live after sixteen seconds. Bounding TCP reception alone prevented the
backlog but exposed repeated decoder resets at the scalar conversion rate.
These failed tests were not accepted as release passes.

The independently written block-layout converter now uses ARM NEON in
sixteen-pixel groups, retaining the scalar reference for clipped tails. Host
tests compare every RGBA byte for random/minimum/maximum data, padded rows,
odd crops, and padded 720p surfaces. Both scalar and optimized helpers decoded
180 B-frame fixture pictures on Checkers with identical pixel SHA-256
`1da559aa4be2f3bbfbe56c43f96fbdba07c76d5734545edfeb630a03f0b8884a`
and PCM SHA-256
`08c495dc440ddd1e8376eb784f140470d67df6d80344032bff94f256f037e672`.

The qualified optimized helper SHA-256 is
`56091ad1146815d30be313be9e08cb671640033756de14f294f04ab04ef6ce91`.
A clean full-object rebuild reproduced the exact binary. The runtime
preparation tool selects this helper; firmware retains the old verified helper
for ordinary media but requires this new helper for live-call negotiation.

With the bounded receiver and optimized converter, the five-minute production
loopback relay test completed 8,521 captured / 8,517 decoded pictures in
300.028 seconds (28.39 decoded fps). The first picture arrived at 761.6 ms,
the final picture at 299.974 s, and the maximum gap was 77.9 ms. Warmed-up
available memory stayed approximately 683,836–686,036 KiB. The highest sampled
thermal value was 67.3°C; five minutes does not establish thermal equilibrium.

A subsequent thirty-second run omitted nine predictive pictures and still
delivered 645 pictures (21.50 fps), with delivery continuing to 29.245 s and a
maximum gap of 1.24 s. A native 1280×720 JPEG still succeeded afterward.
The normal microphone process stayed running; temporary capture/codec owners
were reaped and the original camera helper resumed.

The optimized helper also completed a 1,200-picture H.264/AAC B-frame fixture
through the unprivileged FFmpeg/Go media path: 2,211,840,000 RGBA bytes,
SHA-256 `076244e7cf68a7851c79915c74e7e912608cd4dca44c5d3d1440da6fa37f49ac`,
and 3,840,000 PCM bytes, SHA-256
`2f23cb3785e89e1f3ba241ff0b6de551c2fe08ef8f1073e4ae6c09e65e04f169`.
Elapsed time was 112.160 s including hashing every picture; this is not the
product's playback cadence. A separate five-second forced cancellation
returned a deadline error after 47 pictures and reaped decoder/FFmpeg while
the original microphone process continued running.

These are in-memory tests without the real display, host, or second device.
Not yet qualified: permanent deployment of the complete final rootfs and
helper set, authenticated slot health, two-device full-duplex calls, extended
thermal/A/V behavior, normal runtime provisioning/kernel migration, and the
remaining Echo models. Do not describe the 30 fps source as guaranteed 30 fps
full-duplex display. A coordinated system/kernel, application, and host release
is required; Tater Echo Firmware's `docs/checkers-release-readiness.md` tracks
the outstanding gates.

### Later repeated-start blocker

A five-minute test of the exact packaged camera helper with the release
Go 1.24/ARMv7 toolchain returned 9,077 captured pictures and zero decoded
pictures. Short repeats sometimes passed, so this is not a confirmed compiler
problem. Snapshot-pointer aliasing and the undersized startup queue have
independent regression reproducers and are corrected in offline RC.4, with
HTTP/picture-progress deadlines added. A later repeated-start attempt failed
before feeding decoder input and then the unit stopped responding to USB
console and Wi-Fi; USB still enumerates it. The cause remains unconfirmed.

No firmware write was in progress and known-good slot A remains selected.
RC.3 in inactive B must not be activated. No earlier successful measurement
overrides this release blocker.

The operator's power cycle restored known-good A, Wi-Fi, USB console, display,
and continuous microphone capture. No saved kernel crash report was available.
Review found that normal request cancellation force-killed both codec wrappers
before OMX teardown. New wrappers detach process cancellation long enough to
drain output and release hardware normally, retaining bounded forced-kill
fallbacks. Camera input is completed at its raw-frame record boundary, with
output drained concurrently to avoid pipe backpressure deadlock. This is a
code-proven teardown defect, not a confirmed root cause of the whole-unit hang.

Portable shutdown tests, Linux race tests, the camera adapter/driver tests,
and pinned Go 1.24 ARMv7 builds pass. Three isolated camera starts completed
approximately 29 fps with explicit clean encoder exits. An isolated synthetic
decoder consumed all 30 pictures and reached Loaded after freeing buffers.
Three short concurrent production-relay/decode starts also passed at about
27 decoded fps, with clean encoder exits and decoder reaping. A new complete
RC.5 rootfs was reproduced twice byte-for-byte (SHA-256
`627abd3acaf4fdfad072859607fb38821ba788a78a070f61bb9e43f7072fdcce`).
Final installed-candidate and repeated sustained qualification remain open.

The new five-minute wrapper test captured 8,646 and decoded 8,643 pictures
in 300.112 seconds (28.80 decoded fps), with a maximum picture gap of 88.2 ms.
Warmed available memory stayed approximately 646,444–648,564 KiB and the
highest sampled thermal value was 70.7°C, not a thermal equilibrium claim.
Both wrappers shut down cleanly and original microphone capture continued.
A thirty-second loss test omitted 13 predictive pictures, decoded 550, and
recovered through the end with a 1.310-second maximum gap.

RC.5 was then installed with known-good A retained, and the exact qualified
GPU-word and NEON helper/manifest pairs were installed after backing up the
four originals on the device and Mac. No proprietary library was replaced.
The real five-minute authenticated health window committed B good and
rebooted into B with read-only mounts. Normal services selected GPU retained
UI and native H.264 capture without transient mounts or helper overrides.
An installed-service 20-second relay/decode test delivered 549 pictures at
27.29 fps. The operator confirmed first-word capture and smooth text, menu,
touch, volume, microphone privacy, response interruption, and camera-cover
privacy. Physical recovery/rollback and extended final-candidate A/V still
need completion. The operator has no second Checkers available, so no
two-peer H.264 call result is claimed.

The operator's physical Volume Up boot subsequently entered TWRP briefly,
then Tater Recovery. USB inspection confirmed the real initramfs recovery
PID 1, the Tater Recovery framebuffer process, both rootfs slots good, and
the one-shot marker consumed. The cache router log, inspected through a
read-only/no-journal-replay mount, independently recorded its handoff. No
boot or recovery image was flashed during this test.

Recovery could select and boot retained good A, which reconnected to Tater
with normal ALSA capture. Its older GPU allowlist rejected the new shared
helper and safely used the CPU framebuffer path. This is bootable manual
recovery, not a proof of version-transactional accelerated rollback. Helper
and manifest version matching/restoration must be addressed in the normal
installer/update path before release. Corrected good B was then selected and
booted again, reconnecting to Tater and restoring camera, microphone capture,
the exact GPU-word helper, and retained GPU UI with read-only mounts. Both
slots remain good. Automatic failed-trial rollback was not fault-injected.

### Recovery policy revision (RC.6)

The operator subsequently chose native TWRP as the screen-device Volume Up
default. RC.6 removes only the exact retired Tater cache redirect and no longer
packages the handoff payload. It does not flash boot or recovery. The exact
old `hardware-dev.3` A rootfs was privately archived on device and Mac before
using A for RC.6; good RC.5 is retained in B. The RC.6 rootfs was reproduced
twice at SHA-256
`17a42585bc9d2a3bc970af77ea6610744330859f59b8f46078bf2dd413cb7934`.

RC.6 logged removal of the cached redirect, passed its actual connected health
trial, committed good A, and rebooted normally with read-only mounts. A native
recovery request then reached TWRP 3.7.0_9-0 on its own 4.9.77 kernel and
remained there through 113 seconds uptime with the same boot ID and no cached
OpenRecoveryScript. There was no automatic Tater Recovery handoff. This test
used USB native recovery entry; physical Volume Up's bootloader-to-TWRP path
was proved earlier. Explicit USB Tater Recovery remains available; Biscuit
and Radar's recovery behavior is unchanged pending their hardware work.
Normal reboot returned to good RC.6 A, reconnected to Tater, and restored GPU
retained UI, camera, and continuous microphone capture with read-only mounts.

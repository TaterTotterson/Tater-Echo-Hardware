# Athena codec prerequisites

2026-11-14, unpublished development. Hardware encoding/decoding is **not yet
qualified or enabled**. The working Panfrost GPU UI is a separate path.

## Read-only board evidence

The attached Athena uses `6.18.56-techo5-athena`, an ARM64 kernel with ARMv7
userspace. Its only V4L2 node is the existing raw-camera capture path. The
live DT includes MT8183 encoder (`vcodec@17020000`) and decoder
(`video-codec@16020000`) nodes, but neither has a bound codec driver.

`CONFIG_VIDEO_MEDIATEK_VCODEC` is off. `CONFIG_MTK_SCP` and
`CONFIG_RPMSG_MTK_SCP` are configured as modules but no installed module set
or remoteproc instance was found. Both codecs reference `scp@10500000`.
The board DT explicitly disables that SCP and removes its `memory-region`.
The existing read-only vendor mount has no file matching `*scp*`; its firmware
directory contains other device firmware. There is no SCP-labelled partition.
This does not prove that a bootloader has never started Amazon SCP code.

Full boot/recovery partition backups were transferred and compared with the
device hashes. The 12,951,552-byte boot image within the 16 MiB partition passes
the pinned platform checker, including its SHA-1 header ID, firmware-only
ramdisk, DTB and nonempty per-device speaker-tuning slot. Those private inputs
and backup hashes stay in the ignored workspace. No partition was written.

## Firmware and memory contract

Linux 6.18.56's MT8183 codec implementation uses the SCP, not the other Shows'
Android OMX libraries. The SCP driver allocates a 5 MiB DRAM working/code region
from the DT's reserved-memory provider. The coherent allocator rounds that
request to an 8 MiB bitmap region, so a 5 MiB pool is insufficient. With no provider it skips that
allocation; adding a codec module is not enough to make the present board DT
work.

An upstream candidate was inspected **only as a data file**:

- linux-firmware tag `20250708`, `mediatek/mt8183/scp.img`;
- SHA-256 `401e9c027b0e105efa9eb9b31683924a8d5f5ab3b1654f0915b174effe2df33d`;
- ELF32 little-endian ARM executable, 1,030,776 bytes;
- DRAM PT_LOAD: physical `0x50000000`, virtual `0x10000000`, 636,880 bytes;
- SRAM IPI buffer: `0x7bdb0`, 592 bytes;
- firmware identifies itself as the Kukui Chromebook build.

The distinct virtual and physical addresses matter: a generic ELF virtual
address audit cannot establish the SCP loader's memory contract. Athena's
current DT has no pool at the required physical address, and its source marks
that range as bootloader scratch during boot. That observation is a warning
requiring a boot-lifetime/EMI-MPU review, not proof that a properly reserved
pool can never be used after handoff. Do not point SCP at the protected
`vpu_binary` carveout or copy Chromebook GPIO/pinctrl settings by guesswork.

Athena's audio I2C pads are also shared with the SCP; the working board port
uses GPIO-backed I2C for its microphone ADC and speaker amplifier. Firmware
compatibility therefore includes preserving audio ownership and privacy, not
just receiving a codec-ready response. Upstream remoteproc defaults to
automatic boot, and the codec drivers can boot firmware on the first device
open. Even an apparent codec-inventory `open()` is not a read-only test.

## Reusable audit

`scripts/audit_athena_codec.py --root /` inspects Linux DT/config/platform
metadata without opening a camera/codec, touching registers, loading modules
or copying firmware. It requires an Athena MT8183 DT and rejects incomplete
runtime snapshots. With `--scp-elf /private/candidate.img`, it additionally
validates bounded ARM ELF load segments and reports physical addresses and
the IPI section. Firmware bytes, embedded strings, calibration and serials
are not emitted. It never sets `hardware_codec_qualified` or `safe_to_load`.

Eleven audit regression tests cover wrong devices, incomplete snapshots,
disabled/absent/module-only support, malformed/truncated/overflowing ELF
segments, physical-vs-virtual addresses, and refusing to equate configured
prerequisites with hardware qualification. The full research suite passes
110 tests.

## Build and next hardware gate

The exact kernel.org 6.18.56 source tarball was SHA-verified:
`4633bbda2ab95eaf2a7af4041236451d6759c72e6946265531af4b3aacefbf89`.
The existing pinned board patch sequence and captured running configuration
were used with Ubuntu GCC 13.3.0-6ubuntu2~24.04.1. SCP and codec module compile
tests pass. Those initial compile-only outputs deliberately lack full-kernel
symbol verification and are marked **UNQUALIFIED**; they must not be installed.
Compilation is not generated-video or board-firmware compatibility evidence.

The subsequent full kernel, DTB and complete module build also passed, with
normal full-kernel symbol verification and no unresolved-symbol warnings.
Its existing audio/display/network configuration is preserved; changes are
the codec's module/dependency selects, compiler metadata, and disabling build
debug sections to keep the RAM job bounded. Kernel build identity is
`2026-11-14 10:00:00 UTC`, distinct from the installed October build.

The private foundation boot image uses that new kernel but keeps the original
DTB, original firmware ramdisk and byte-identical speaker tuning. Its structure
and header digest pass the pinned filled-slot boot checker. SCP remains
disabled, so this is explicitly **not** an image that enables codecs. It was
not flashed. Kernel Image.gz SHA-256:
`6519c7da7d10fe7235953f9c4196b6a24a3894cc5adedd4a829058937ca4ef12`.

The Docker VM's disk was full. A private RAM-backed Linux build area avoided
deleting any user containers, images, volumes or cache. No package-signature
verification was disabled. No speculative SCP/ISP job was launched, no live
camera/microphone samples were recorded, and Tater was not restarted.

Before enabling codecs on the device:

1. Confirm recovery/restore with the operator present and preserve per-unit
   tuning, original ramdisk, pairing/settings and the known-good GPU image.
2. Establish SCP reservation, protected-memory and shared-peripheral ownership;
   prepare a strictly controlled first boot, not an automatic firmware launch.
3. Qualify encoder output against software references and implement the
   decoder's V4L2 Request API state/buffer lifecycle. Do not treat its stateless
   decoder as a stateful Annex-B pipe.
4. Verify generated color/motion/reference ordering, sustained cancellation,
   camera/call performance, audio, physical privacy and recovery before
   exposing a hardware capability or packaging a release.

Primary sources: [Linux 6.18.56 source](https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.56.tar.xz),
[upstream MT8183 firmware directory](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/refs/tags/20250708/mediatek/mt8183/),
[V4L2 stateless decoder interface](https://docs.kernel.org/userspace-api/media/v4l/dev-stateless-decoder.html).

## Subsequent controlled hardware trial

The operator verified physical DC-power/Volume-Up/TWRP recovery. The new
kernel booted with original DT/ramdisk/tuning and passed the owner's wake,
first-word, speaker and touch checks. A source-audited overlay then reserved
the ELF's physical DRAM window, without importing Chromebook pinmux or EC
devices. The first 5 MiB pool failed allocation before firmware execution;
Linux's coherent allocator uses `get_order`/`bitmap_find_free_region`, so the
correct minimal pool is 8 MiB at `0x50000000`. This lies in previously ordinary
Linux System RAM, outside the known protected reservations.

The source-hash-locked trial patch refuses Athena probes by default, disables
remoteproc auto-boot/recovery, and holds SCP reset before registering its IRQ.
Default-off refusal and the manually opted-in, still-offline probe passed.
The inspected upstream firmware then signalled ready in about 149 ms. Its
loader path was temporary and restored; nothing was installed for boot.
Both drivers registered and answered format/control queries. H.264 encode
accepts NM12/NM21/YM12/YM21; decode requires frame-based stateless H.264 and
returns MM21. Owner wake/audio/menu checks still passed with SCP running.

This does **not** yet qualify encoded pixels, decoded reference order, live
camera/calls, cancellation or long-run stability. No production capability is
enabled. A later automatic reboot was traced to the normal rootfs kernel
updater reapplying its bundled old kernel: its saved previous-image hash is
the test kernel, and its new trial identifies the bundled old one. Do not
misreport this as evidence of a codec crash. Further manual tests need
matching boot-local template metadata; the final rootfs must carry its actual
matching kernel. Original boot/recovery backups and per-unit tuning remain
private and intact.

## Generated-video checks: failed, not production-qualified

The encoder acknowledged its initialization/parameters/encode command, but SPS
generation produced zero IRQ status and zero bitstream bytes before timing out.
Its 450 MHz clocks and runtime power were active. Read-only observations showed
power/reset/SRAM control `0xd` and bus clamp zero. The retained recovery DT's
four variants agree with the driver's MMIO/active-low interrupt assignment.
These checks do not establish a power, interrupt or protection fault.

An independently prepared, isolated native GStreamer 1.24.2 test exercised
frame-based H.264 requests and MM21 (`NV12_16L32S`) conversion. It used only
generated 640×480 baseline video, with camera/audio/GPU device opens denied.
The process exited zero and wrote 460800 bytes, but pixel comparison found an
all-zero frame and the driver logged NALU error 196738, with no decoder IRQ.
That result is **failure**, not successful decoding. The actual cause remains
unresolved. No HEVC/AV1 stream, camera scene, security-register write or guessed
DMA remapping was attempted.

Read-only SCP configuration snapshots matched the open-source firmware's AP
and cached-DRAM translations. A bounded checksummed EC-console snapshot returned
nontextual payload; its meaning is unknown and is not used as proof of memory
corruption. The temporary observation modules were removed after each check.
The normal GPU application, voice and settings remain independent of codec
qualification. Diagnostic binaries and private firmware stay outside Git.

`scripts/verify_athena_codec_pattern.py` now checks generated I420 output for
blank data, known Y/U/V bars, ordered six-bit frame IDs and a moving marker.
It rejects truncated/extra frames, swapped chroma and repeated/reordered frame
IDs. Even a passing synthetic check reports `hardware_codec_qualified: false`:
live capture ownership/order, cancellation, privacy and integration still need
separate validation. This verifier never opens a hardware device.

At the end of this trial, the exact original boot partition was restored and
fully hash-read back through the verified TWRP recovery. Normal boot confirmed
the original kernel with the new GPU app/native Panfrost compositor still
running. Temporary SCP firmware, observation modules, decoder runtime, debugfs
mount and template binds were gone. No settings, pairing or weather/sensor
configuration was restored or changed by that cleanup. All trial sources and
private artifacts remain available; hardware codec parity is still incomplete.

## Resumed investigation: AP-controlled encoder

The subsequent operator-present investigation found a concrete difference
between the Chromebook SCP route and Athena's original processor-controlled
codec interface. One generated SCP encode added DEVAPC status module 254
(`VENCSYS_VENC`). Its diagnostic record identified a domain-3 write to the
encoder's reset register at `0x170200a8`. No access permissions, secure firmware
or bootloader settings were changed. Always-on configuration reads returning
zero are not interpreted as proof of permissive access rules.

The original public MT8183 driver describes an AP-controlled `/dev/Vcodec`
interface, rather than this SCP path. A temporary original adapter now uses
Linux runtime power, clocks, IRQ and IOMMU/coherent-DMA APIs for the encoder.
It is exact-target, explicitly opted in, root-only, and not installed or
advertised by the production application. It exposes only its owned encoder
page and bounded owned allocations; it does not provide arbitrary physical
memory, legacy-ioctl or secure-command passthrough. Cleanup resets the engine
before releasing retired DMA buffers, including on process exit.

The owner's codec libraries load in a private temporary Bionic/VNDK runtime.
No proprietary component is distributed here. Metadata loading was initially
fenced from hardware and filesystem writes; hardware tests subsequently used
only the exact owned adapter descriptor and generated input. The standard OMX
control interface supplies the matching component settings internally.

Generated encoding has now passed these narrower checks:

- Standard Loaded/Idle/Loaded lifecycle with 12 owned input and eight owned
  output buffers; all component workers and allocations cleaned up.
- A real encoder interrupt and a decodable 640×480 Baseline Level 3.1 frame.
- Ten generated frames, with independently software-decoded Y/U/V bars,
  sequential frame IDs and moving markers all passing the offline verifier.

Explicit `OMX_IndexParamVideoPortFormat` negotiation matters: setting only the
port definition left the component's internal input format at YV12 while the
caller supplied NV12. That produced valid luma but invalid chroma and was
rejected. Explicitly selecting its advertised NV12 format corrected all planes.
The public VAL cache interface also distinguishes global maintenance from
range maintenance; global `(NULL, 0, 0)` is handled under the DMA-coherent
allocation contract, not mistaken for an invalid range.

The ten-frame Annex-B result was 2,497 bytes, SHA-256
`b77be67412accc42fe69b1917a28211e1be4952b0660070af418805f887b703a`.
Its 4,608,000-byte independently decoded I420 output passed all ten frame checks,
SHA-256 `15f2ad86bfe756b42fcf666c110d461a68180fb428b1e62a850a55e26759daae`.
These are **generated encoder results**, not completed hardware-video parity.
AP-controlled decoding, sustained/cancellation tests, privacy, camera/calls,
and complete kernel/rootfs/runtime packaging remain unqualified. The normal
GPU UI and software media path remain the installed application configuration.

The earlier original-kernel restoration is historical: continued research
re-entered the manually guarded test kernel with boot-local matching template
binds. Those are research-only, not a release or persistent boot solution.
Final handoff must restore the original boot or install a properly matched
system candidate; do not leave an unmatched manual kernel across reboot.

Interface reference: [original MT8183 codec driver](https://github.com/amazon-mt8183-devs/android_kernel_amazon_mt8183/blob/92f9c3e3079403a34b77193451775a0ce7f3054b/drivers/misc/mediatek/videocodec/mt8183/videocodec_kernel_driver.c),
[public VAL cache/memory API](https://github.com/amazon-mt8183-devs/android_kernel_amazon_mt8183/blob/92f9c3e3079403a34b77193451775a0ce7f3054b/drivers/misc/mediatek/videocodec/include/kernel/val_api_public.h).

## AP-controlled decoder: generated pixels and order verified

The temporary AP-controlled decoder now passes both single-frame and ten-frame
generated tests. Real decoder completion interrupts were observed, all output
descriptors and DMA addresses belonged to the caller, and normal deinitialization
left zero live allocations. The ten-frame 640×480 I420 result was 4,608,000 bytes,
SHA-256 `15f2ad86bfe756b42fcf666c110d461a68180fb428b1e62a850a55e26759daae`:
**byte-for-byte identical to independent software decoding** of the encoder
fixture above. Colors, IDs 0–9 and moving-marker ordering all passed.

Important interface differences were established rather than guessed:

- The original driver's decoder interrupt-wait branch ignores the encoder-only
  returned-status-count fields. Treating their uninitialized decoder values as
  valid counts incorrectly rejected the wait. The adapter now follows the
  original decoder/encoder distinction and still requires real hardware IRQs.
- This owner's runtime uses the active extended decode interface. Its legacy
  decode entry point returns success without decoding, which is not accepted
  as qualification. The local callback-interface version also differs from
  MT8163 and is checked before any job.
- Linux retains ownership of decoder clocks. Shared clock registers remain
  read-only to userland; only observed, already-satisfied enable requests are
  translated to native clock ownership and barriers. The source-defined decoder
  DCM bit is controlled through a fixed, bounded kernel operation, preserving
  other bits and restoring its original setting on cleanup.
- The order verifier rejected an intermediate test that returned frame 0 ten
  times despite ten successful interrupts. Distinct immutable input packets
  using the verified runtime contract fixed this. Output buffers remain distinct
  while references can be live; interrupt counts alone never establish order.

The one-frame I420 result also matched software decoding exactly, SHA-256
`1bb5bda2472ea49b97b3529c5ab7d6536fefd5b7f1c3bf461c8208f7e317009f`.
Temporary modules were removed after testing; the installed GPU/voice app and
settings were not changed. No camera scene, host restart or security-policy
change was involved.

These are **generated encoder and decoder milestones**, not a release-ready
camera/call pipeline. Sustained/cancellation, privacy, live camera/call
integration, independent review and matched kernel/rootfs/runtime packaging
remain required. The earlier manual-kernel/template-bind warning still applies.

Field reference: [matching MT8183 DCM golden settings](https://github.com/amazon-mt8183-devs/android_kernel_amazon_mt8183/blob/92f9c3e3079403a34b77193451775a0ce7f3054b/drivers/misc/mediatek/base/power/power_gs_v1/mt8183/mtk_dcm_array.c).

## Continued qualification: buffer reuse, cancellation and stream protocol

Both AP engines subsequently passed five deliberately interrupted-job/restart
cycles. The test exits immediately before the real IRQ wait, after the original
HAL starts the generated picture; it does not fake an IRQ or vendor cleanup.
The kernel quiesces/resets before reclaiming retired DMA. Each fresh ten-frame
restart reproduced the independently verified result, and modules unloaded.
These bounded process-exit checks are not a substitute for long live calls.

Fixed-pool encoding now passes 300 generated 640×480 frames: 300 real IRQs,
301 returned input buffers including EOS, clean deinitialization and zero live
buffers. Independent software decoding verifies every color plane, repeated
frame ID and moving marker. The 27,597-byte H.264 result has SHA-256
`6d259e9c6d3347cec2edd39e3cd6c9284f1236d9dd69cac77240bcb2aee9814a`.

Decoding also passes 300 generated pictures with actual input-release and
reference-frame-release handoffs before buffer reuse. Its complete
138,240,000-byte I420 result matches 30 repetitions of the independent ten-frame
reference, SHA-256
`02a587b46faeabcb7a0b06794546c89913bf23f21a1fa68b363316d1623f7fdd`.

A performance problem in the **experimental caller** was isolated with monotonic
timings and CPU sampling: it supplied the full 1 MiB allocation capacity as the
logical packet length, making the runtime scan unused noncacheable padding.
Supplying the actual access-unit length reduced the measured generated-frame
decode call from approximately 150 ms to 0.8 ms, with unchanged independently
verified pixels. This is not an end-to-end camera FPS or latency measurement.
The allocation still retains bounded accessible padding; no parser or IRQ
check was skipped and no proprietary binary was patched.

The temporary timestamped RGBA stream prototype also passes complete byte-level
comparison against independently software-decoded I420 and a BT.601 conversion
reference. Tests cover framing, nonzero 64-bit PTS and clean EOS:

| Generated Baseline/no-B stream | Frames | Verified TVO2 SHA-256 |
| --- | ---: | --- |
| 640×480 | 30 | `2662ae49a3050edd8748fd05868e974186c418832839d14a52689182d8ba012a` |
| 768×432 | 30 | `678fdc356fdb89fe3c52336ce7b2913937e5b8f774ce7ac5522772a8aea22842` |
| 1280×720 | 10 | `645b89ad326a866e0e8222b977dc964e60f3b1dbc90184b49b1af8773855792a` |
| 1280×960 | 10 | `2e80a948f74c47912aebebbd9741e937548105e159d54e896d03e8e3a56bfdfe` |

Tiled storage rounds dimensions before allocating. Larger pictures use separate
owned frame allocations, retaining the existing 8 MiB per-allocation and 64 MiB
per-engine limits. The runtime's free-input getter differs from MT8163 and is
validated against the exact known ring pointer. The published external-frame
timestamp setting supplies an owned picture token; it is not a hardware clock.
Reordered Main/High-profile playback and general streaming lifecycle integration
are **not** qualified by these Baseline tests.

The original kernel sources are now in
[`kernel-patches/athena-codec/ap`](../../kernel-patches/athena-codec/ap/README.md).
They remain default-off, exact-board/kernel and root-only experiments. Public
source builds add non-executable DMA/MMIO mappings, irreversible executable
permission rejection and decoder PFN-wrap checks. Real-device negative mapping
tests, default-off refusal and forced-exit tests passed. Generated duplex testing
then ran the 300-frame encoder concurrently with the 30-frame 768×432 stream
decoder: real IRQ counts, clean buffer ownership/teardown, and both complete
independent result hashes still matched. No secure-policy changes were made.

The installed application still uses its previous software/JPEG media path and
the working Panfrost UI. No production hardware capability has been enabled.
Owner-local codec libraries, runtime and generated artifacts remain private;
only original source, interface findings and hashes are public-repo inputs.
Remaining gates include production helper/runtime packaging, sustained and
reordered playback, real camera/call/privacy tests, and a matched kernel/rootfs
with normal boot/OTA/rollback checks. The manual-kernel/template-bind warning
above still applies. Tater was not restarted, and nothing was pushed or tagged.

## Continuous helpers and firmware receiver integration

Original source-only helpers now live in `probes/mt8183-codec`, with locked
NDK/public-header builds and separate owner-local runtime preparation. The
decoder accepts continuous single-slice Baseline I/P input, rejects unsupported
profiles/sequence changes and preserves per-slot picture tokens plus 64-bit
presentation timestamps. Operations remain individually bounded; there is no
ten-second or 300-frame whole-session ceiling. Truncated input, output closure
and cancellation fail closed, followed by a successful exact-pixel restart.

The encoder's continuous NV12/TVE1 path passes a paced 360-picture 640×480 run
lasting over fourteen seconds. Independent software decoding verifies colors,
frame IDs and motion order for every picture. PTS actually exercise the upper
32 bits, and requested recovery pictures 90/180/270 contain real IDRs. The
55-file encoder and 24-file decoder owner-local closures are separately pinned;
neither proprietary closure is included in the repositories or public builds.

The firmware receiver now has an explicitly opted-in Athena path, with every
dependency/helper hash checked and no automatic capability enabling. It keeps
general media on the software path: reordered Main/High/B-picture decoding is
not qualified. A generated-only probe exercises the actual Sat Chat receiver,
packet queue, helper launch, RGBA delivery and graceful EOS without touching
the camera, screen or audio. All 360 delivered frames (442,368,000 RGBA bytes)
match the independent software reference, SHA-256
`470e0e04412a994a726413972303c872e124f31bb64c53ffa973cf6afefd68b9`.

This integration test also caught an output-buffering bug in the helper's
capability mode: the fail-closed exit wrapper bypasses libc teardown, so the
JSON response must be explicitly flushed when launched with a pipe. Both
helpers now do so; stream decoding/encoding behavior was not changed.

The installed GPU/voice app is still unchanged. Live camera conversion/encoder
integration, real calls/privacy, matched kernel/rootfs packaging, normal reboot
and OTA/rollback remain required before release. No Tater restart, push or tag
was needed for these checks.

## Live camera and matched-package preparation

The original camera adapter now converts immutable OV13855 packed-BGGR frames
to the qualified 640×480 NV12 input. It uses the existing V4L2 dequeue/copy/
requeue lifecycle and platform tone tables, with a centered 4:3 crop. Native
still resolution is unchanged. Independent native-RGB/BT.601 color and chroma
order tests pass. Four disjoint row bands reduce on-device generated conversion
from 47.4 to 12.5 ms; this is CPU preprocessing, not GPU Bayer conversion.

Bounded, scene-in-memory capture tests hold approximately 30 encoded FPS after
startup. Concurrent hardware decode through the firmware's actual receiver and
loopback relay keeps pace, with monotonic capture PTS and no growing queue.
Deliberately omitted predictive pictures recover at subsequent real IDRs.
Normal EOS, camera teardown and both module unloads pass. No camera recordings,
audio data or owner settings are saved by these probes.

An offline matched rootfs candidate now contains the two original helpers,
their exact original kernel modules and the empty-tuning kernel template. A
separate source-locked bundle verifier rejects unexpected/private files and
mixed ELF architectures. Boot readiness requires exact board/kernel identity,
SCP offline, pinned modules and actual AP nodes; private runtime manifests alone
cannot enable hardware. Every helper retains its full independent runtime and
sandbox checks. The existing GPU-only helper bundle is unchanged.

This candidate is **not installed or release-qualified**. Visual two-device
calls, privacy/cover and voice regressions, matched normal boot and system/app
OTA/rollback remain open. Full-resolution encoding and reordered general media
remain unqualified. The installed application is unchanged, all trial modules
are unloaded, and no host restart or push/tag occurred. The manual-kernel
handoff warning above remains applicable until the matched system is installed
or original boot is restored.

## Installed matched-boot startup validation

The matched development system/application are now installed through separate
system/application A/B transactions, preserving the original good system slot.
A normal reboot exposed a packaging assumption: the research SCP driver had
been manually loaded, so its `offline` sysfs file disappeared after reboot.
The original boot check refused hardware rather than inferring offline state.

The corrected public bundle pins the same previously qualified GPL rpmsg, IPI
and patched SCP modules as well as the AP adapters. On the exact board/kernel,
the SCP adapter holds reset and disables automatic boot/recovery; no SCP image
is included or started. Only a confirmed offline state permits AP startup.
Unknown/running state refuses; per-stage failure cleanup is tested. This path
passes on the device and on a clean matched-system boot without temporary
metadata overrides. Both application codec preflights and the GPU compositor
are active. Existing owner-local codec closures remain private and unchanged.

The startup correction leaves camera/app/helper/kernel bytes unchanged. The
owner confirms voice, speaker and touch at the compatibility stage; full-app
visual calls/privacy and remaining end-to-end recovery/rollback checks are not
yet recorded as passes. No published release, push or tag has occurred.

The subsequent installed owner regression now passes wake/first-word capture,
interruption, controls, microphone privacy, camera cover and a sustained
two-way Athena–Cronos hardware-video call. Colors/motion/audio and active-call
mute/unmute pass. The active host profile is `echo-h264-balanced`; both actual
codec processes were observed, with monotonic output timestamps and no growing
decode queue. Both processes exit and the camera releases after hangup; the
post-call wake regression passes. Ordinary system/application health commits
complete without temporary boot-template binds. Release OTA/factory migration
and end-to-end rollback/recovery gates still remain; nothing was pushed/tagged.

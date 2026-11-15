# Athena native Panfrost UI bring-up

2026-11-14, unpublished owner-device development. No hardware video qualification.

Athena (Echo Show 8 second generation, A8H3N2) is MT8183, not an MT8163 Show.
The installed `6.18.56-techo5-athena` kernel already binds Panfrost to its
Mali-G72. The display controller separately exposes an 800×1280, two-page
XRGB8888 framebuffer for the 1280×800 landscape workspace. No kernel change
was needed for GPU UI support.

The original Tater retained-scene helper now has a separately compiled native
musl/Mesa path. It opens a surfaceless EGL pbuffer, rejects software renderers,
and requires the exact Athena DT, kernel, panel/depth and Panfrost device.
The original Android/Mali paths remain byte-identical for Checkers, Cronos and
Crown. The helper never opens fb0; Go remains the sole framebuffer owner.

## Runtime and qualification

An isolated Mesa 24.2.8 ARMv7 runtime was prepared from signature-verified
Alpine v3.21 packages in the reviewed builder. Its loader and every dependency
are ordinary hash-pinned files. It does not replace system libraries, use
MT8163 proprietary libraries or provision any codec firmware. Firmware's
`tools/athena_mesa.py` and Go adapter enforce the same exact runtime identity.
All generated/runtime files and device backups remain outside public source.

Generated-only on-device tests passed:

- Actual renderer: `Mali-G72 (Panfrost)`; software Mesa is rejected.
- Rotation and channel order: 1,024,000 / 1,024,000 exact pixels.
- Production native-size menu: exact color/rotation pixel comparison.
- Direct GPU-scaled generated video with retained floating controls: about
  25 offscreen frames/s over 60 frames. This is not installed display FPS.
- OWW and both NWW classifiers load and score generated silence with the
  matching application runtime. No microphone recording was made.

The old ONNX Runtime was privately backed up before atomically selecting the
current pinned runtime. A normal daemon/renderer-only A/B bridge trial passed
its five-minute health gate before installing the GPU-only helper application.
The GPU candidate committed its normal health trial with active retained
composition and parallel dual wake. All seven application component hashes and
unchanged pairing/settings digests were verified.
The owner subsequently reported that all requested hands-on functional checks
work well. This closes that reported GPU/UI checkpoint, not hardware-codec
qualification. Final system OTA, reboot/rollback, thermal and licensing/
source-offer packaging review remain gates. The private full rootfs is built,
not installed.
No Tater host restart, kernel/boot/rescue flash, settings reset or publication
was performed.

## Reproducible off-device preparation

The firmware's `platforms/athena-hardware-lock.json` pins the original helper
sources, output hash and reviewed ARMv7 builder image. Run
`scripts/build_athena_gpu.py` with that lock; firmware's hardware-bundle tooling
also supports `--target athena`. This produces only a GPU adapter and metadata,
never fictitious encoder/decoder executables.

`scripts/prepare_athena_mesa_runtime.py` takes the exact ARMv7 Alpine v3.21 APK
set listed by `probes/mt8163-gpu/athena-mesa-packages.json`. Packages come from
Alpine's official main/community repositories. It checks each package digest,
then performs an offline, signature-verifying installation into a disposable
container prefix with package scripts disabled. The resulting isolated file
tree must match the hardware-qualified manifest exactly before it is emitted.
An independent clean rebuild produced the same 124 file hashes. This runtime
preparation does not itself complete license-notice/source-offer review for a
public firmware distribution.

## Hardware video boundary

The live kernel has `CONFIG_VIDEO_MEDIATEK_VCODEC` and MediaTek VPU disabled.
Only the camera's V4L2 node is present. The MT8183 encoder DT node has no bound
driver and no matching VPU firmware has been established. Athena therefore
does not advertise hardware H.264 and continues its existing software/JPEG
media path, with GPU composition/scaling. Enabling a capability flag or using
the other Shows' Android codec helpers is not an implementation of this work.

A future codec phase needs an Athena-specific kernel/DT and firmware contract,
reviewed recovery/backups preserving the per-device speaker DSP slot, then
bounded generated encode/decode/color/order tests before real calls. No
speculative SCP/VPU/ISP job was launched during this bring-up.

The next read-only/offline phase is recorded in
[Athena codec prerequisites](athena-codec-prerequisites.md). It does not change
the installed UI checkpoint or turn on hardware codec advertising.

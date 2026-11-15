# Rook GPU, video and camera cadence checkpoint

Date: 2026-11-14. Owner-authorized development; not public-release approval.

## Target and boundaries

The 2017 Echo Spot (Rook) has a native round 480×480 display, MT8163/Mali-T720
graphics and a GC0312 640×480 raw8 camera. Rook-specific helpers validate its
kernel/panel identity; they do not masquerade as a Show target. The GPU helper
retains Tater's sole framebuffer owner and does not apply Show rotation.

All original tooling and adapters are public source. Proprietary components,
collected vendor libraries, calibration, boot/recovery backups and device logs
stay owner-local. H.264 encoding/decoding and supported composition use hardware;
Bayer development, tone conversion, copies and JPEG remain CPU work. This is
neither a zero-copy camera claim nor a fully open graphics/codec stack.

## Generated checks and installed UI

- Every native orientation/color/circular-mask pixel matches: 230,400 pixels.
- Actual round-menu composition matches the production renderer.
- Raw8/LUT hardware encoding preserves generated primary colors and timestamps.
- The production hardware receiver preserves 120 numbered pictures; a separate
  360-frame continuous run matches the known reference digest.
- Checkers/Cronos/Crown helper rebuilds remain byte-identical.
- The installed round GPU UI runs around 30 FPS. A blocking IIO illumination
  read originally stalled touch for 1–1.8 seconds. A single background reader
  with a bounded result queue removes it from the UI thread; race tests pass.
- Normal application A/B OTA installed all nine verified components without
  changing pairing or wake settings. The matching open ONNX runtime is backed
  up/staged separately; MWW/NWW confirmation and boundary readiness are checked.

## Stale camera slots

Relay sequence/PTS were ordered while the operator saw older arm positions.
The Rook driver, like Cronos, could publish an IMGO slot before a delayed base
handoff had filled it. The held-slot policy gives each target two sensor ticks,
publishes the preceding held target after another handoff, and copies owned
bytes before exposure/callback work. Missed-tick, wraparound, delayed-latch and
slow-consumer simulations pass. The operator confirmed backward jumps stopped.

## Low-FPS cause and correction

The held policy exposed approximately 2.8 source/encoded FPS. Incoming hardware
playback independently ran around 15 FPS; Bayer preparation took about 3 ms.
Active CCF readback showed CAMTG at 26 MHz despite a 48 MHz request. The normal
divide-by-two path consequently supplied 13 MHz, not the intended 24 MHz.
The exact public kernel conditionally acquired CCF handles only when the DT
requested always-on camera power.

The reviewed clock-handle patch initializes handles unconditionally, propagates
errors and selects the requested parent before enabling it. It does not enable
always-on power or alter sensor tables/privacy GPIOs. A live-stream lease also
caps shutter at the existing 500-line frame length and uses the existing gain
range; ordinary still acquisition retains the original 1500-line limit.

The full kernel was rebuilt with original GCC 8.3-2019.03 and unchanged running
configuration at `amazon-oss/android_kernel_amazon_mt8163` commit
`4174e0b4d0e2eb452d14fbe3c0e5094611f748b6`. Required USB command-mode build
compatibility is retained. All 266 imported Broadcom module CRCs match.

| Artifact | SHA-256 / identity |
| --- | --- |
| Kernel release | `4.9.337-g4174e0b4d0e2` |
| Running configuration | `9e51dd4b9849c29c4bd88c56b04c0338f528d01dae9ed7071edbaacacb8fb7d8` |
| Corrected kernel plus original DTBs | `aa33d9b29a5c4d3e5a3805842df385858589de62272e8cca8a5f263adaef5d17` |
| Module.symvers | `7f2804a41b00f0e048471b459e6a35ff855aee24e60d20812ec7f52ef1e2c9dc` |
| Private boot candidate | `bd1db2f95b410ccbe267d696ea66d8f065ec8bcedfddc159141756245c59423a` |

Physical Volume Up entered retained TWRP. Exact original boot/recovery backups,
target identity and partition labels/sizes were verified before writing only
boot. The complete candidate extent read back correctly; recovery was unchanged.
Both original appended DTBs and the rescue ramdisk are preserved byte-for-byte.
The installed kernel boots with Wi-Fi and the GPU UI. No rootfs/store, vendor,
bootloader, partition layout or recovery image was replaced.

The corrected installed camera measures about 15 source/encoded FPS at the same
640×480 resolution, with roughly 1.5 ms preparation. Active CAMTG reads 48 MHz;
the fixed divide-by-two gives the intended sensor clock. The operator confirms
smoother motion, correct colors and no backward jumps. The stream uses the
existing balanced H.264 contract with Cronos. An initial call returned cleanly
to still exposure and released the camera clock.

During a later contiguous 120-second observation, Rook forwarded 1,799 pictures
(14.95 FPS) and Cronos 1,773 (14.74 FPS). Both directions had zero sequence
loss/duplication/reversal, dropped GOPs and dropped audio frames, without a
growing queue. The owner passed correct colors/ordered motion, two-way audio,
physical camera/audio mute and restoration, immediate first words after the
call, and reply interruption. Final inspection found no camera/Vcodec owner or
encoder/decoder child; GPU and wake/confirmation/boundary readiness remained
active. The normal application health gate committed slot B, and the saved
pairing configuration digest stayed unchanged. The owner subsequently confirmed
all remaining non-OTA functional checks, including the additional media/UI
acceptance. Rook is ready to move on; OTA testing is explicitly deferred to the
combined satellite test. Whole-system release qualification is not inferred
from those functional checks.

Final fleet-wide system OTA/rollback, factory provisioning, extended A/V/thermal
qualification and public-source/artifact delivery remain separate release work.

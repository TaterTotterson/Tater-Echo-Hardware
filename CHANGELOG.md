# Changelog

## Unreleased

- Publish the workbench and original hardware helper sources as
  Tater Echo Hardware (`Tater-Echo-Hardware`), retaining the `tater-re` CLI and
  Python import namespace. Add the complete AGPLv3 license and preserve the
  separate MIT/GPLv2 notices for helpers and kernel work.
- Add target-specific GPU/codec helpers and qualification findings for
  Checkers, Cronos, Crown, Athena and Rook, plus Biscuit ADC3101 gain/filter
  diagnostics. Private libraries, firmware, calibration and device data remain
  excluded; publication is not final firmware release approval.
- Resolve the reproduced Cronos camera-buffer ownership hazard using a
  two-tick held-target handoff. Fingerprint-only checks and owner-observed
  installed calls confirm ordered motion while GPU/H.264 remain enabled;
  the superseded previous-slot trial is retained in the findings as a failed
  attempt, not as the final implementation.

- Add a clean source-locked Checkers helper builder consumed by the firmware
  pipeline. Fresh builds must reproduce the hardware-qualified GPU/VDEC/VENC
  hashes; public output never includes owner-local link/runtime libraries.
- Add separate, verified library-only runtime preparation for the Checkers USB
  installer. Private outputs refuse existing/public release directories and
  remain independent of firmware-slot helper updates and rollback.

- Add retained glyph-atlas protocol v3 and a single blended GPU batch for
  independent STT/reply word motion and opacity. Verify movement/alpha pixels
  and a 120-frame 29.21 fps display run; preserve rotation/v2 compatibility.

- Extend the Checkers Mali helper with retained text/layout, GPU edge glow and
  thinking dots, and snapshot-based menu/STT crossfades. Verify exact fade
  endpoints and interrupted fades on hardware without opening the framebuffer;
  keep the existing bounded rotation protocol and device-owned library tree.

- Record a read-only Checkers v4.0.2 hardware baseline from the connected test
  unit without retaining device identity, credentials, or raw capture data.
- Add a clean-room MT8163 JPEG ABI probe whose safe default only describes the
  interface and whose active INIT/DEINIT mode requires target verification and
  explicit acknowledgement.
- Record that Checkers' pinned kernel exposes the JPEG encoder but compiles out
  the decoder, and make the active probe refuse that incomplete kernel before
  opening its control node.
- Add an exact-source-validated experimental kernel/resource patch and compile
  its decoder object and Checkers DVT without producing a bootable image.
- Rebuild the complete Checkers kernel with the exact live configuration and
  pinned Arm GCC 8.3 toolchain while preserving the vendor-module release.
- Verify every imported symbol CRC used by the live MT7668 Bluetooth and Wi-Fi
  modules against the rebuilt kernel.
- Assemble and validate an unflashed donor-derived test boot image that keeps
  the exact live Tater Recovery ramdisk and fits the 16 MiB boot partition.
- Boot the retained Checkers TWRP 3.7.0 fallback, verify root ADB access, and
  document amonet's intentional `recovery`-to-`swdl` partition alias.
- Complete a recoverable Checkers boot trial, reject the inferred standalone
  JPEG decoder with register evidence, and restore the original boot payload
  with an exact live hash match.
- Prove independent H.264 hardware decode through the stock MT8163 VDEC,
  determine its complete 16x32-luma/16x16-interleaved-UV block layout, and add
  a clean host- and target-tested RGBA8888 converter.
- Add a bounded synthetic streaming probe that turns over ten separate aligned
  VDEC inputs and owner-local callback buffers, releases every requested input,
  and matches ten distinct converted frame hashes to the software reference
  set without displaying or saving pixels.
- Extend the read-only VDEC capability report with the public queued-input
  feature bit; record that Checkers reports it unsupported and reject a
  normal-GOP experiment whose output hashes did not match its reference.
- Add a bounded stock-OMX native-buffer path with a headless GUI-extension
  compatibility shim, direct ION-backed gralloc output, cache synchronization,
  and clean block-YUV-to-YUV420P conversion; match a conventional one-IDR-plus-
  nine-P fixture and an `I,B,B,P,B,B,P,B,B,P` fixture to all 245,760
  software-reference bytes in presentation order.
- Factor that proven lifecycle into a standalone `tater-vdec` adapter with
  bounded H.264 batch input and tightly packed YUV420P or RGBA8888 output.
  Validate exact software-reference output at 960x480, 1280x720, and
  1920x1080, including cropped 736- and 1088-line hardware surfaces, and
  present a normal B-frame sequence through Tater's single-owner renderer.
- Add length-prefixed streaming input and raw RGBA output to `tater-vdec`;
  reproduce the batch output exactly and complete repeatable 180-picture runs
  beyond the original bounded callback storage.
- Add protocol v2 with explicit input/output timestamps, a negotiated RGBA
  geometry header, framed pictures, and an unambiguous EOS record; preserve
  the exact validated v1 pixel output on connected Checkers hardware.
- Bound protocol-v2 output waits, ignore process-level `SIGPIPE`, and make a
  disconnected consumer fail with status 1 and kernel resource recovery rather
  than crashing during a stock-component error transition.
- Prepare the inactive production handoff with the minimal pinned-FFmpeg H.264
  copy features plus host-tested Go wire parsing and incremental AUD framing.
- Normalize pre-existing H.264 AUDs before reinsertion and carry that exact
  ARMv7 FFmpeg output through a successful 30-picture hardware decode.
- Reject AUD-only records before they reach OMX so malformed or accidentally
  double-delimited streams fail over without opening a decoder picture.
- Add the opt-in Tater hardware-media orchestration with strict helper/FFmpeg
  preflight, SPS geometry validation, concurrent protocol-v2 I/O, a bounded
  first-frame audio gate, and startup-only software fallback. Validate exact
  30-picture hardware output and byte-identical audio/frame fallback on
  connected Checkers.
- Make the open helper runtime relocatable at `/data/tater-vdec`, export its
  required headless compatibility symbols explicitly, and remove the embedded
  research-only gralloc path while keeping every proprietary library
  owner-local.
- Complete an off-screen Mali-T720 EGL render and pixel readback with matching
  GPU interrupt activity while leaving the display untouched.
- Add a bounded Checkers RGBA rotation/swizzle stream helper, exact full-panel
  self-test, internal and Go-pipe benchmarks, and an owner-local runtime
  preparation tool with complete tree identity and read-only manifest.
- Integrate the helper into Tater's live renderer for large frames while
  retaining the CPU dirty-rectangle path, sole Go framebuffer ownership,
  normal-RAM staging, and automatic failure fallback.
- Map the native framebuffer, display-manager, overlay/fence contract, and a
  confirmed 32-bit suspend-query bug; rebuild the complete pinned kernel with
  the bounded compatibility fix.
- Map the Checkers OV9734 sensor, privacy switch, camera/ISP ARM32 ABI, and
  reverse-teardown state machine without powering the sensor or capturing a
  frame unattended.
- Present a hardware-decoded synthetic H.264 frame through Tater's existing
  transient framebuffer path, then restore the sole renderer owner.
- Extend that proof to ten distinct persistent-decoder outputs, present them as
  a moving bounded sequence, and verify display/audio/camera ownership recovers.
- Complete an operator-present OV9734 internal-color-bar capture without
  retaining pixels; verify the live feature-enum IDs, stable band summaries,
  sensor/ISP/ION teardown, final clock disable, and helper restoration.
- Add a metadata-only live audio probe and map the open Checkers path from its
  two microphone channels and stereo loopback through ALSA into `tater-echo`,
  without opening a PCM node or reading or saving samples.
- Add a two-second anonymous-memory/CPU headroom probe and record initial
  bandwidth, thermal, latency, memory-recovery, and service-recovery baselines.
- Add a hardware-access matrix and a deterministic current-upstream audit
  distinguishing generic Mali-T720 support from the missing MT8163 platform
  and V4L2 decoder ports.
- Add a deterministic Android-hardware source audit showing that the public
  MT8163 OMX loader and VAL headers do not include the decoder component they
  dynamically open.
- Build and boot a complete clean Checkers Tater rootfs, pass the real
  authenticated A/B slot health gate with the prior image retained as a good
  rollback, and repeat strict hardware video/audio, camera, microphone,
  display, touch, wireless, memory, and thermal checks from the committed
  image.

## 0.2.0 — 2026-11-10

- Add read-only GPT, Android boot, MediaTek kernel, ramdisk, appended-DTB, and
  ext4 image inspection.
- Add filtered ext4 extraction with automatic exclusion of common secret and
  key filenames.
- Add binary string offsets, firmware capability maps, and ELF dependency
  reports.
- Add Ghidra batch analysis, export refresh, function sizes, and string
  cross-references.
- Record the first reproducible MT8163 stock-firmware offline baseline and the
  bounded JPEG decode interface selected for connected-device validation.

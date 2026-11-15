# Roadmap

## Milestone 1 — local firmware workbench

Status: complete for the first MT8163 offline baseline.

- [x] Inventory extracted firmware and individual artifacts by SHA-256.
- [x] Parse ELF identity and dependency metadata.
- [x] Inspect Android boot, GPT, ext4, ramdisk, kernel, and appended DTB data.
- [x] Export Ghidra functions, strings, cross-references, calls, and targeted
  decompilation.
- [x] Produce repeatable capability and dependency reports.
- [x] Preserve a complete audit trail without committing private inputs.

## Milestone 2 — read-only device evidence

Status: initial Checkers baseline complete; operation tracing remains.

- [x] Snapshot the first Checkers platform, driver, device-node, ALSA, display,
  media, and firmware metadata through its read-only USB console.
- [x] Correlate the exact live JPEG library hash and function addresses with
  the running kernel interface.
- Add scenario markers for a stock video start and microphone capture start.
- Add bounded `open`, `mmap`, and `ioctl` observation without changing device
  behavior.
- Correlate runtime operations with Ghidra functions and open kernel handlers.

## Milestone 3 — MT8163 media proof

- [x] Record the live JPEG control requests and their ARM32 structure sizes.
- [x] Run the target-gated INIT preflight and identify the decoder path omitted
  from the current Checkers kernel and device tree.
- [x] Make subsequent active probes refuse kernels without decoder symbols.
- [x] Prepare and compile-test the MT8163 decoder code and DVT resource patch.
- [x] Rebuild the complete kernel with pinned GCC 8.3, the exact live
  configuration, and the vendor-module release suffix.
- [x] Match every imported symbol CRC used by the live MT7668 Bluetooth and
  Wi-Fi modules.
- [x] Assemble and verify an offline donor-derived boot image without flashing
  it.
- [x] Prove the retained TWRP fallback boots with root ADB before any trial
  write to `boot`.
- [x] Map the private ION/M4U allocation path and the VDEC lock, power, clock,
  interrupt, capability, lifecycle, and cache-management interfaces.
- [x] Reject the inferred standalone JPEG decoder after a real decode timeout
  and a live encoder/decoder register comparison.
- [x] Reproduce one H.264 decode into a private buffer and validate all 24,576
  output bytes without displaying or saving the frame.
- [x] Complete an EGL pbuffer clear and read back all 1,024 pixels through the
  current Mali-T720 r26p0 DDK, with matching GPU interrupt activity and no
  framebuffer access.
- [x] Turn the off-screen proof into an allowlisted, owner-local Checkers
  compositor; verify all 460,800 rotated/swizzled pixels, benchmark the complete
  Go pipe path, and integrate large frames behind automatic CPU fallback while
  retaining one framebuffer owner.
- [x] Map the read-only display-manager session, capability, panel, and
  framebuffer contracts; preserve the confirmed 32-bit suspend-query fix.
- [x] Identify the Checkers OV9734 camera, one-lane 720p30 MIPI contract,
  lens-cover switch, and private MediaTek sensor/ISP device boundary without
  powering the sensor or capturing a frame.
- [x] Verify the exact ARM32 sensor, ISP, pipe-manager, and SYSRAM request
  layouts on the target with a no-open/no-ioctl ABI executable; map the small
  sensor/ISP/FrameB adapter boundary by hash.
- [x] Map the ION buffer, layer, fence, and trigger portion of the display
  manager from public source without disturbing the running Tater renderer;
  select Tater's transient framebuffer path for the first presentation.
- [x] Prove the decoder's complete block-YUV layout with a synthetic reference
  and implement a clean, host-tested conversion to RGBA8888.
- [x] Exercise sustained VDEC buffer turnover with ten distinct synthetic IDR
  pictures, separate aligned input/output pools, cache invalidation, and exact
  software-reference hash-set agreement.
- [x] Audit current upstream Linux: generic Mali-T720 is supported, but MT8163
  has no upstream board tree, Panfrost platform data, or V4L2 decoder match.
- [x] Audit the public Android hardware tree and distinguish its open OMX
  loader and MT8163 VAL headers from the absent decoder component source.
- [x] Verify a conventional one-IDR-plus-nine-P GOP through the stock OMX
  native-buffer path, detile every output to planar YUV420P, and match all
  245,760 software-reference bytes after complete lifecycle teardown.
- [x] Verify B-frame presentation ordering with an
  `I,B,B,P,B,B,P,B,B,P` fixture and an exact 245,760-byte software-reference
  match through the same native-buffer lifecycle.
- [x] Factor the proven lifecycle into a bounded `tater-vdec` process adapter,
  add direct YUV420P and RGBA8888 output, and verify reference-correct
  960x480, 720p, and 1080p output including padded hardware surface heights.
- [x] Add framed streaming input and RGBA output to `tater-vdec`; reproduce the
  finite-batch output exactly and complete repeatable 180-picture buffer-ring
  runs with no callback or write failures.
- [x] Add protocol v2 negotiated geometry, per-picture microsecond timestamps,
  explicit EOS, bounded nonblocking output, and connected-device failure-path
  recovery without leaking Vcodec, ION, M4U, or framebuffer ownership.
- [x] Add the minimal H.264 copy muxer/bitstream filters to Tater's pinned
  FFmpeg source build and a tested incremental AUD packetizer/wire reader.
- [x] Connect `tater-vdec` to Tater's existing media path behind an explicit
  opt-in, gate audio until the first validated picture, and prove both exact
  hardware output and byte-identical startup fallback on Checkers.
- [x] Carry source packet timestamps through the copy boundary with a bounded
  copy-only `framecrc` side channel, preserve B-frame presentation order, and
  integrate late-frame dropping against the real ALSA consumption clock.
- [x] Build a complete public Checkers rootfs, install it into the inactive
  system slot, pass the authenticated five-minute health gate, commit and
  reboot it with the prior system slot preserved, then repeat framebuffer,
  ALSA, camera, microphone, wireless, and strict no-fallback VDEC checks from
  the committed image.
- Replace the owner-local decoder, OMX component, and gralloc dependencies
  with a redistributable adapter or V4L2 bridge.
- Replace the owner-local Mali DDK/VNDK runtime with a real MT8163
  Panfrost/Mesa/DRM port; the current production helper is intentionally a
  guarded bridge, not the open-stack endpoint.
- [x] Present a decoded buffer through the existing Tater display path.
- [x] Cycle ten distinct persistent-decoder outputs through the existing Tater
  display path and restore the normal renderer and audio owners afterward.
- [x] With the operator present, enable the OV9734 internal color bars before
  streaming and capture one bounded test-pattern frame through the clean
  adapter; verify power, clocks, IRQs, and mappings return to idle.
- [x] Measure an initial bounded CPU/memory-bandwidth/thermal baseline and
  verify display, audio, and camera service ownership recovers afterward.

## Milestone 4 — microphone and audio frontend

- [x] Inventory the live ALSA capture/playback devices, external-codec route,
  and camera privacy-switch input without recording ambient audio.
- [x] Map the Checkers capture transport from the two microphone ADC channels
  through the SPI-audio FPGA and standard four-channel ALSA stream; preserve
  calibration contents as owner-local data.
- [x] Identify hardware versus software beamforming, gain, AEC reference, VAD, and
  noise-suppression responsibilities.
- Compare stock output with Tater's raw multichannel frontend using repeatable
  recordings and geometry-aware tests.
- Adopt only changes that improve measured wake and command-audio behavior.

## Milestone 5 — Athena

- Add Mesa/Panfrost userspace experiments for the existing mainline GPU path.
- Document the Amazon SCP firmware boundary.
- Evaluate the mainline MT8183 V4L2 decoder without assuming incompatible SCP
  firmware can be loaded safely.
- Keep Athena media work separate from the legacy MT8163 ABI.


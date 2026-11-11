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
- [x] Prepare a target-gated INIT/DEINIT probe without executing active mode.
- Map the remaining JPEG buffer, ION/M4U, VDEC, and display-manager interfaces.
- Reproduce a single JPEG decode in an isolated probe.
- Reproduce one H.264 decode into a private buffer.
- Present a decoded buffer through the existing Tater display path.
- Measure CPU, memory bandwidth, thermals, latency, and recovery behavior.

## Milestone 4 — microphone and audio frontend

- Map stock capture initialization and calibration selection per target.
- Identify hardware versus software beamforming, gain, AEC reference, VAD, and
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


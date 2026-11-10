# Roadmap

## Milestone 1 — local firmware workbench

- Inventory extracted firmware and individual artifacts by SHA-256.
- Parse ELF identity and dependency metadata.
- Export Ghidra functions, strings, calls, and targeted decompilation.
- Search analysis from a deterministic command line.
- Preserve a complete audit trail without committing private inputs.

## Milestone 2 — read-only device evidence

- Snapshot platform, driver, device-node, ALSA, display, media, and firmware
  metadata through ADB or SSH.
- Add scenario markers for a stock video start and microphone capture start.
- Add bounded `open`, `mmap`, and `ioctl` observation without changing device
  behavior.
- Correlate runtime operations with Ghidra functions and open kernel handlers.

## Milestone 3 — MT8163 media proof

- Map the JPEG, VDEC, ION/M4U, and display-manager kernel interfaces.
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


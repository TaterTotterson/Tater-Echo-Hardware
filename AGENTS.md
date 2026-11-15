# Tater Echo Hardware rules

- Never commit or publish Amazon, MediaTek, Arm, or other proprietary firmware,
  binaries, calibration data, device backups, keys, or serial-numbered data.
- Keep firmware inputs and generated Ghidra projects in ignored local
  workspaces. Commit only hashes, interface descriptions, original tooling,
  and independently written implementations.
- Device collection is read-only by default. Do not add a write, flash,
  register poke, power-sequence change, or mutating ioctl without an explicit
  user request and a target-specific safety review.
- Treat decompiler and AI output as hypotheses. Record binary SHA-256,
  addresses, runtime traces, and test results for conclusions.
- Keep clean-room implementation code separate from decompiled pseudocode.
  Do not paste reconstructed proprietary source into production repositories.
- Prefer deterministic, machine-readable CLI output so investigations can be
  repeated and audited.

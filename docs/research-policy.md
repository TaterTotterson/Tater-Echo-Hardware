# Research policy

## Ownership and custody

Analyze firmware and devices owned by the operator. Keep complete firmware,
partition dumps, vendor libraries, calibration material, device identifiers,
keys, and Ghidra databases outside Git in the ignored research workspace.

Committed evidence may contain cryptographic hashes, public symbol names,
addresses tied to a named binary hash, independently written interface
descriptions, and reproducible test results. It must not contain reconstructed
proprietary source or redistributable copies of proprietary data.

## Evidence standard

Decompiler output and AI explanations are hypotheses, not facts. A hardware
contract should identify:

1. the SHA-256 of every binary used;
2. function or instruction addresses supporting the conclusion;
3. corresponding open kernel source, header, or device-tree evidence;
4. a runtime trace or controlled hardware test when practical; and
5. uncertainty and target/version limitations.

## Device safety

Collection is read-only unless a separate, reviewed experiment explicitly
requires mutation. Default tools must not:

- flash or erase partitions;
- remount filesystems read-write;
- write sysfs, debugfs, procfs, registers, mixer controls, NVRAM, or GPIOs;
- issue an undocumented mutating ioctl;
- change power, clocks, voltage, thermal limits, or secure-world state; or
- collect secrets or full proprietary files by default.

Hardware experiments belong on a recoverable test device with stable external
power and a verified recovery path.

## Clean implementation boundary

Research notes describe observable behavior and interface contracts. Production
code is written independently from those contracts and open documentation. Do
not paste decompiler pseudocode into Tater repositories.


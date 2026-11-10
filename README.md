# Tater Hardware Research

Tater Hardware Research is a local-first workbench for understanding the
hardware interfaces used by Amazon Echo firmware and implementing independent,
open Tater support for those interfaces.

The project does **not** attempt to regenerate or publish Amazon source code.
It inventories firmware obtained from hardware we own, connects reproducible
static analysis to runtime evidence, and records the interface contracts needed
for clean Tater implementations.

## Safety and privacy

- Firmware, device backups, calibration data, Ghidra projects, and runtime
  captures stay in an ignored local workspace.
- Nothing is uploaded to an AI or analysis service by this tool.
- The device snapshot command is read-only. It does not flash, remount, write
  registers, change mixer controls, or issue hardware-control ioctls.
- Only original tools, hashes, evidence summaries, interface descriptions, and
  clean implementations belong in Git.

See [Research policy](docs/research-policy.md) before adding a new collector or
device operation.

## Current milestone

The first version provides:

- recursive firmware-directory inventory without copying the input;
- SHA-256 identity and magic-based image classification;
- dependency, interpreter, build-ID, architecture, and SONAME extraction from
  32- and 64-bit ELF files without external Python packages;
- searchable JSON manifests;
- a Ghidra headless adapter and export/decompile scripts;
- a conservative ADB device snapshot for Android, Fire OS, LineageOS, and
  Tater Linux devices.

Ghidra is optional until static analysis is requested. ADB is optional until a
physical device snapshot is requested.

## Set up

```sh
cd /Volumes/TaterSpeed/Taters/Tater-Hardware-Research
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

The repository includes `bin/tater-re`, so no package installation is required.
An editable Python install remains optional for development environments with a
current pip.

Initialize an ignored local workspace:

```sh
./bin/tater-re --workspace .tater-re init
```

## Inventory firmware

Point `ingest` at an extracted firmware directory, an Android vendor tree, a
boot image, or an individual library. Inputs are referenced in place and are
not copied into the repository.

```sh
./bin/tater-re --workspace .tater-re ingest /path/to/extracted/vendor
./bin/tater-re --workspace .tater-re inventory
./bin/tater-re --workspace .tater-re search libvcodec
./bin/tater-re --workspace .tater-re search mtk_disp_mgr
```

Every artifact is addressed by its SHA-256. A unique prefix is accepted by
commands that take an artifact ID.

## Ghidra analysis

Install Ghidra separately, then point the workbench at its installation when it
cannot be found automatically:

```sh
./bin/tater-re --workspace .tater-re ghidra status --ghidra-home /path/to/ghidra
./bin/tater-re --workspace .tater-re ghidra analyze SHA256_PREFIX \
  --ghidra-home /path/to/ghidra
./bin/tater-re --workspace .tater-re ghidra decompile SHA256_PREFIX FUNCTION_OR_ADDRESS \
  --ghidra-home /path/to/ghidra
./bin/tater-re --workspace .tater-re ghidra search vcodec
```

Analysis exports stay under `.tater-re/analysis`. Ghidra itself refuses project
directories whose path contains a dot-prefixed element, so its ignored database
is kept beside the workspace in `tater-re-ghidra-projects/`. The export is JSON
Lines so an investigator or coding agent can query it without controlling the
Ghidra user interface.

## Read-only device snapshot

With one test Echo attached over ADB:

```sh
./bin/tater-re --workspace .tater-re device snapshot
```

Use `--serial SERIAL` when multiple devices are attached. The snapshot records
platform identity, kernel and device-node inventories, ALSA state, loaded
modules, Android media-service summaries when available, and selected device
tree data. It intentionally does not pull proprietary libraries or calibration
contents.

## Next work

The staged plan is in [Roadmap](docs/roadmap.md). The first hardware proof will
target one bounded MT8163 operation—preferably JPEG decode—before any production
firmware integration.


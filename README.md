# Tater Echo Hardware

Tater Echo Hardware contains the original GPU, video and camera helpers,
hardware diagnostics, reproducible build tooling, and research used to bring
Tater Echo satellites up on Linux. The research workbench remains local-first:
private firmware and device data are never publication inputs.

## Repository roles

- [Tater Echo Hardware](https://github.com/TaterTotterson/Tater-Echo-Hardware):
  hardware helpers, diagnostics, interface findings and source-locked builds.
- [Tater Echo Linux](https://github.com/TaterTotterson/Tater-Echo-Linux):
  board platforms, kernels, boot images, root filesystems and recovery.
- [Tater Echo Firmware](https://github.com/TaterTotterson/Tater-Echo-Firmware):
  the satellite application, UI, factory installer and release packaging.

Publishing this source does not qualify a firmware release. Board-specific
kernel/runtime provisioning, factory installation, OTA and rollback checks
remain separate release gates. Do not flash diagnostic or experimental outputs
as production firmware.

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

The offline workbench provides:

- recursive firmware-directory inventory without copying the input;
- SHA-256 identity and magic-based image classification;
- read-only GPT, Android boot image, ramdisk, kernel, appended-DTB, and ext4
  inspection;
- dependency, interpreter, build-ID, architecture, and SONAME extraction from
  32- and 64-bit ELF files without external Python packages;
- searchable JSON manifests, binary strings with exact offsets, capability
  maps, and dependency reports;
- a Ghidra headless adapter with batch analysis, repeatable JSONL export,
  string cross-references, and targeted decompilation;
- a conservative ADB device snapshot for Android, Fire OS, LineageOS, and
  Tater Linux devices.

The first owner-local MT8163 stock image has been mapped offline. The evidence
and the exact boundary between confirmed facts and the next device tests are in
[the MT8163 baseline](docs/findings/mt8163-stock-offline-baseline.md).
The first connected, read-only confirmation is in the
[Checkers v4.0.2 baseline](docs/findings/checkers-v4.0.2-read-only-baseline.md).
The complete pinned-toolchain kernel build and vendor-module ABI check are in
the
[Checkers GCC 8.3 JPEG build](docs/findings/checkers-gcc8-jpeg-kernel-build.md).
The recoverable live trial, rejected standalone-JPEG inference, verified VDEC
lock, sustained multi-buffer 128x128 H.264 hardware decode, and complete
Mali-T720 off-screen render/readback are in
the [Checkers live media bring-up](docs/findings/checkers-live-media-bringup.md).
The complete image build, A/B health-gated commit, post-commit strict hardware
video/audio test, and full-device smoke are recorded in the
[Checkers ground-up firmware trial](docs/findings/checkers-ground-up-firmware-trial.md).
The distinction between live independent access and a fully redistributable
open stack is tracked in the
[Checkers hardware-access matrix](docs/hardware-access-matrix.md).
The difference between generic upstream Mali-T720 support and a real MT8163
port is captured in the
[Checkers upstream Linux gap](docs/findings/checkers-upstream-gap.md).
The separate
[MT8163 decoder source audit](docs/findings/mt8163-vdec-source-availability.md)
shows that the public Android hardware tree provides an OMX loader and ABI
headers, but not the decoder component that loader opens.

Athena's separate MT8183/Panfrost work is recorded in
[the GPU/UI bring-up](docs/findings/athena-panfrost-ui-bringup.md).
[The codec prerequisite audit](docs/findings/athena-codec-prerequisites.md)
distinguishes its working GPU UI from the still-unqualified SCP video path,
including read-only checks and an offline full-kernel/module build.

Rook's native round GPU UI, target-specific hardware codecs, completed camera
buffers and recoverable camera-clock correction are recorded in the
[Rook GPU/codec checkpoint](docs/findings/rook-gpu-codec-bringup.md). It separates
the measured 15 FPS installed capture from remaining final release gates.

## Guarded hardware probes

The GPU and H.264 adapters are also buildable production helper sources, not
documentation-only probes. `scripts/build_tater_hardware.py` consumes the
firmware repository's `platforms/checkers-hardware-lock.json`, builds in a fresh
temporary tree, and requires byte-identical hardware-qualified outputs. The
firmware's `tools/hardware_bundle.py` verifies the research source pin before
running that recipe. Only the three open helpers and public metadata belong in
the resulting public archive.

`scripts/prepare_tater_hardware_runtime.py` separately verifies and prepares the
owner's private library trees for the factory's `provision_hardware.py` tool.
That output stays private/ignored; it excludes helpers and is never a firmware
release input. New firmware carries its helpers with the owning system/app
slot, while private libraries remain unchanged. See the firmware repository's
`docs/checkers-hardware-build.md` for the complete workflow and remaining
publication, kernel migration and installed-hardware qualification gates.

The first clean-room probe records the confirmed MT8163 JPEG control ABI. Its
default mode is host-safe and does not open a device:

```sh
make -C probes/mt8163-jpeg
probes/mt8163-jpeg/build/mt8163-jpeg-probe --describe
```

The probe's inspect and INIT/DEINIT modes are target-gated. Active mode also
requires an explicit hardware-ioctl acknowledgement and must not be run without
a separate device review and operator approval. See
[the probe README](probes/mt8163-jpeg/README.md).

The experimental JPEG-decoder candidate proved that the inferred standalone
block is not present at `0x17004000` on this device. That patch is retained only
as a documented negative result. The separate `mt8163-vcodec` probes now
validate the real hardware video-decoder path, including a complete off-screen
H.264 decode and ten-picture synthetic buffer-turnover test. See
[the Vcodec probe README](probes/mt8163-vcodec/README.md).
The `mt8163-gpu` probe independently completes a private EGL pbuffer render and
pixel readback without opening the display. Its bounded stream helper now also
verifies every pixel in a full Checkers rotation/swizzle and drives Tater's
large-frame hybrid GPU compositor while Go retains sole framebuffer ownership.
See
[the Mali probe README](probes/mt8163-gpu/README.md).
The Vcodec work also proves the complete block-YUV layout and includes a clean,
host-tested RGBA8888 converter for Tater's transient renderer. Its first
reusable `tater-vdec` adapter now decodes bounded normal H.264 batches to
YUV420P or RGBA8888, has exact reference matches through 1080p, and completes
repeatable 180-picture framed RGBA streams beyond the original bounded batch.
An explicitly gated Tater integration now carries HTTP H.264 through the
unprivileged pinned FFmpeg process into `tater-vdec`, matches the exact known
30-picture hardware hash, and falls back before first presentation without
duplicating PCM when SPS geometry does not match. It now also preserves source
packet PTS through a copy-only metadata side channel, synchronizes against PCM
periods consumed by the real ALSA path, completes a 1,200-picture soak, and
holds the final displayed frame through the speaker's queued PCM tail.
The stock component and its Android compatibility runtime remain owner-local
and are not part of this repository. The production handoff is documented in
the [Tater VDEC integration plan](docs/tater-vdec-integration.md). The
`mt8163-display` probe maps the framebuffer and display-session
capabilities without changing display state. It also identified a bounded
32-bit suspend-query fix under `kernel-patches/mt8163-display`; that fix now
passes a [complete pinned-toolchain kernel build](docs/findings/checkers-display-compat-kernel-build.md)
without the rejected JPEG experiment.
The `mt8163-camera` inventory identifies the exact Checkers sensor and private
ISP ABI from public kernel source without powering the camera or capturing an
image. Its target-side ABI check now verifies the exact ARM32 sensor, ISP,
pipe-manager, and SYSRAM layouts without opening those devices. See
[the camera inventory README](probes/mt8163-camera/README.md) and the
[camera ABI boundary](docs/findings/checkers-camera-abi.md).

Ghidra is optional until static analysis is requested. `debugfs` from
e2fsprogs is needed for ext4 images, and `dtc` is needed to render appended
device trees as source. ADB is optional until a physical device snapshot is
requested. On macOS, the optional image tools can be installed with:

```sh
brew install e2fsprogs dtc
```

## Set up

```sh
git clone https://github.com/TaterTotterson/Tater-Echo-Hardware.git
cd Tater-Echo-Hardware
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

The repository includes `bin/tater-re`, so no package installation is required.
An editable Python install remains optional for development environments with a
current pip.

## License

Unless a file has a separate license notice, Tater's original code and
documentation are licensed under [AGPL-3.0-only](LICENSE). Existing MIT-licensed
helpers and GPL-2.0-only kernel modules/patches retain their respective terms;
see [the license notices](NOTICE.md). No license here grants permission to
redistribute proprietary runtime libraries, firmware or device data.

The original `scripts/repack_legacy_boot.py` helper can verify an Android
boot-v0 donor and assemble an ignored local candidate with a replacement
kernel while preserving and validating every other payload. It refuses to
overwrite existing output and is intended only for the reviewed recovery
workflow documented in the Checkers build records.

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
./bin/tater-re --workspace .tater-re strings SHA256_PREFIX /dev/mtk_disp_mgr
```

Every artifact is addressed by its SHA-256. A unique prefix is accepted by
commands that take an artifact ID.

## Inspect firmware images

Image commands are read-only with respect to their input. Extracted files go
under the ignored workspace unless `--output` is supplied. Files with common
key, credential, and keystore names are excluded from ext4 extraction and
directory ingestion.

```sh
./bin/tater-re --workspace .tater-re image inspect /path/to/gpt.img
./bin/tater-re --workspace .tater-re image inspect /path/to/boot.img
./bin/tater-re --workspace .tater-re image boot-extract /path/to/boot.img
./bin/tater-re --workspace .tater-re image list /path/to/system.img --root /lib
./bin/tater-re --workspace .tater-re image extract /path/to/system.img \
  '/lib/*.so' '/lib/hw/*' '/lib/egl/*'

./bin/tater-re --workspace .tater-re report capabilities --format markdown
./bin/tater-re --workspace .tater-re report dependencies --format json
```

## Ghidra analysis

Install Ghidra separately, then point the workbench at its installation when it
cannot be found automatically:

```sh
./bin/tater-re --workspace .tater-re ghidra status --ghidra-home /path/to/ghidra
./bin/tater-re --workspace .tater-re ghidra analyze SHA256_PREFIX \
  --ghidra-home /path/to/ghidra
./bin/tater-re --workspace .tater-re ghidra batch jpeg --limit 10 \
  --ghidra-home /path/to/ghidra
./bin/tater-re --workspace .tater-re ghidra export SHA256_PREFIX \
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

The staged plan is in [Roadmap](docs/roadmap.md). Hardware-decoded frame
presentation and the operator-present, no-save camera-pattern proof are now
complete. Standard ALSA microphone transport is also mapped without retaining
audio. Tater's existing MIT-licensed MT8163 camera package is the clean camera
adapter; V4L2 would improve portability but is not an access blocker. The main
remaining media gaps are redistributable replacements for the owner-local VDEC
and Mali userspace bridges, plus repeatable frontend-quality comparisons rather
than more transport probes.


# Tater Echo Hardware

Reusable hardware helpers and development tools for building Linux applications
and custom firmware on supported Amazon Echo devices.

This is the hardware toolkit behind [Tater Echo Firmware](https://github.com/TaterTotterson/Tater-Echo-Firmware):
GPU composition, hardware H.264 encoding/decoding, camera-buffer and color-format
adapters, audio diagnostics, and reproducible runtime preparation. Other firmware
projects can use the sources and documented interfaces without running the Tater
app or Home Assistant bridge, subject to the applicable licenses.

**Looking to install Tater on an Echo?** Use the
[firmware downloads and installation guide](https://github.com/TaterTotterson/Tater-Echo-Firmware#readme).
This repository is for developers; it is not an installer or a universal Echo
driver package.

## What it provides

- **GPU rendering helpers:** Off-screen GLES composition, panel rotation and
  color swizzling, retained scenes, fades, animated text, glow/thinking effects,
  and video composition. Your application still owns layout and presentation.
- **Hardware video adapters:** Target-specific H.264 encoder/decoder helpers
  with framed input/output, timestamps, bounded buffers, and guarded teardown.
  These use the dedicated video blocks, not the GPU's shader cores.
- **Camera integration building blocks:** Sensor/ISP interface documentation,
  Bayer/color conversion, buffer-order findings, and camera-clock corrections.
  The production capture service itself lives in the firmware repository.
- **Audio development tools:** Read-only ALSA/mixer diagnostics and original
  Biscuit ADC gain/high-pass-filter experiments. Production Biscuit/Radar
  gain/filter integration lives in the matching firmware/kernel sources.
- **Reproducible builds and runtime tools:** Source/toolchain/output locks,
  compatibility shims, and local runtime assemblers that keep public helpers
  separate from private vendor libraries.
- **A research workbench:** Firmware inventories, image/ELF inspection, optional
  Ghidra integration, and read-only device snapshots for extending support.

## Devices and hardware paths

Support is specific to the exact target, kernel, runtime, and helper version.
Similar-looking Echo models are not interchangeable.

| Target | Device | Hardware path covered |
| --- | --- | --- |
| `checkers` | Echo Show 5, 1st generation | MT8163 Mali GPU helper; retained-Lineage H.264 adapters |
| `cronos` | Echo Show 5, 2nd generation | MT8163 Mali GPU helper; target-specific H.264/camera adapters |
| `crown` | Echo Show 8, 1st generation | MT8163 Mali GPU helper; target-specific H.264/camera adapters |
| `athena` | Echo Show 8, 2nd generation | MT8183 Panfrost/Mesa GPU helper; separate AP-controlled video adapters |
| `rook` | Echo Spot, 1st generation | MT8163 round-display GPU helper; target-specific H.264/camera adapters |
| `biscuit` | Echo Dot, 2nd generation | Audio diagnostics and ADC gain/filter reference work; no screen/video helper bundle |
| `radar` | Echo, 2nd generation | Related production audio integration in the firmware/kernel repositories; not a Biscuit probe target |

The screen-target helpers were integrated into Echo Firmware 5.0.0. That release
also includes the Biscuit/Radar audio changes. See its
[validation coverage](https://github.com/TaterTotterson/Tater-Echo-Firmware/blob/main/docs/echo-5-release-review.md)
for completed device tests and explicitly accepted gaps, including Radar's
unperformed physical tests. Research checkpoints below are dated evidence, not
a promise that every experimental probe or media format is production-ready.

## Using the toolkit in another project

This is a collection of small C/C++ helpers and Python build tools, **not one
drop-in shared library or a stable, cross-device SDK**. Hardware helpers run as
separate processes with bounded stdin/stdout protocols. This makes them usable
from Go, C/C++, Python, or another application that implements the contract.

1. **Select the exact target and kernel.** Start with the matching board sources
   in [Tater Echo Linux](https://github.com/TaterTotterson/Tater-Echo-Linux), not
   an arbitrary Android/Linux image.
2. **Pin a matching helper set.** The firmware repository's
   [`platforms/`](https://github.com/TaterTotterson/Tater-Echo-Firmware/tree/main/platforms)
   directory holds the consuming source, toolchain, runtime, and output locks.
   Its [`tools/hardware_bundle.py`](https://github.com/TaterTotterson/Tater-Echo-Firmware/blob/main/tools/hardware_bundle.py)
   builds/verifies the GPU/MT8163 bundles; Athena codecs use the separate
   [`tools/athena_codec_bundle.py`](https://github.com/TaterTotterson/Tater-Echo-Firmware/blob/main/tools/athena_codec_bundle.py)
   contract. Do not mix helpers from different targets or profiles.
3. **Supply the required local runtime.** Follow the device-specific preparation
   recipe below. Keep proprietary libraries private and check their exact
   identities before enabling hardware access.
4. **Implement the process protocol.** Use the helper sources and the firmware's
   [`internal/hardwarebundle`](https://github.com/TaterTotterson/Tater-Echo-Firmware/tree/main/device/internal/hardwarebundle)
   and [`internal/videostream`](https://github.com/TaterTotterson/Tater-Echo-Firmware/tree/main/device/internal/videostream)
   integrations as references. Keep a single display owner, preserve timestamps
   and buffer lifetimes, and handle helper failure explicitly.
5. **Qualify your integration.** Test generated colors and frame order, real
   voice/video, camera and microphone privacy, cancellation, and recovery on
   the actual model. Passing a host test or copying a lock does not qualify a
   new firmware build.

For example, from a matching **firmware** checkout, build and verify the public
Checkers helpers using the sources in this repository:

```sh
python3 tools/hardware_bundle.py build --target checkers \
  --source /path/to/Tater-Echo-Hardware \
  --ndk /path/to/android-ndk-r21e \
  --headers /path/to/pinned-public-header-checkouts \
  --link-libraries /path/to/Tater-Echo-Hardware \
  --output /path/to/new-public-helper-directory
python3 tools/hardware_bundle.py verify --target checkers \
  /path/to/new-public-helper-directory
```

This example selects the retained-Lineage Checkers profile, which needs no
private link libraries; the required `--link-libraries` directory is unused by
that profile. Other profiles have their own locked requirements. The header
checkout names/revisions and NDK must match the consuming lock. The builder
refuses changed inputs or nonmatching outputs; it does not download or install
firmware. See the
[build/runtime guide](https://github.com/TaterTotterson/Tater-Echo-Firmware/blob/main/docs/checkers-hardware-build.md)
and the [existing public build inputs](https://github.com/TaterTotterson/Tater-Echo-Hardware/releases)
for the complete workflow.

### Where to start in the source

- [GPU helpers and protocols](probes/mt8163-gpu/README.md): common renderer source
  with target-specific paths, including Athena's Mesa adapter.
- [MT8163 video adapters](probes/mt8163-vcodec/README.md) and
  [Checkers retained-Lineage clients](probes/checkers-lineage-codec/): H.264
  interfaces, pixel layouts, and bounded buffer handling.
- [Athena AP codec adapters](probes/mt8183-codec/README.md): separate MT8183
  encoder/decoder interfaces and safety restrictions.
- [Camera interfaces](probes/mt8163-camera/README.md),
  [Cronos completed-buffer findings](docs/findings/cronos-completed-camera-buffers.md),
  and [Rook bring-up](docs/findings/rook-gpu-codec-bringup.md).
- [Audio tools](probes/mt8163-audio/README.md) and
  [Biscuit filter findings](docs/findings/biscuit-adc3101-hpf-trial.md).
- [Runtime input provenance](docs/findings/owner-runtime-supply-chain.md),
  [`scripts/assemble_mt8163_lineage_runtime.py`](scripts/assemble_mt8163_lineage_runtime.py),
  and the Athena runtime/build tools under [`scripts/`](scripts/).

## Runtime requirements and limits

**The open helpers are included; proprietary vendor runtimes are not.**
MT8163 GPU/video paths still use compatible retained MediaTek/Arm libraries.
Athena uses Panfrost/Mesa for GPU rendering, but its separate codec adapters
still require compatible device-local codec libraries and kernel modules.
Some preparation also uses pinned public Android bootstrap/VNDK inputs.

Echo 5's factory installer assembles the required runtime locally from verified
public inputs and the unit's retained vendor libraries. A normal user does not
need our development files, an old FireOS backup, or a separately prepared
runtime. A third-party project must implement or reuse that provisioning flow;
cloning this repository alone does not provide those libraries.

- GPU composition is not hardware video decoding. The codec blocks are separate.
- This is not an all-GPU or zero-copy camera pipeline: capture, Bayer/color
  preparation, JPEG, scaling, and frame copies may still involve the CPU.
- Codec/profile/size limits differ by target. Athena's current decoder is a
  restricted live Baseline H.264 path, not a universal movie decoder.
- MT8163 uses an off-screen Mali helper plus application-owned presentation;
  it is not a complete upstream Panfrost/DRM port.
- This toolkit does not unlock an Echo, bypass its bootloader, replace its whole
  operating system, or offload wake inference/AEC to an undocumented DSP.

## Related repositories

- **This repository:** Reusable hardware helpers, diagnostics, compatibility
  tooling, interface documentation, and research evidence.
- [**Tater Echo Linux**](https://github.com/TaterTotterson/Tater-Echo-Linux):
  Board platforms, kernel sources/builds, boot, root filesystems, and recovery.
- [**Tater Echo Firmware**](https://github.com/TaterTotterson/Tater-Echo-Firmware):
  Satellite applications, UI, production camera/audio integration, installers,
  and release/OTA packaging. This is the working integration reference.

## Host development and tests

```sh
git clone https://github.com/TaterTotterson/Tater-Echo-Hardware.git
cd Tater-Echo-Hardware
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

The Python workbench requires Python 3.9+ and has no external Python runtime
dependencies. Hardware builds additionally need the target's pinned compilers,
headers, and, for some recipes, Docker. `bin/tater-re` runs directly from the
checkout; a Python install does not build the hardware helpers.

<details>
<summary>Optional firmware research workbench</summary>

Inputs stay local. Start an ignored workspace, then inventory an extracted
firmware tree or inspect an image without changing the input:

```sh
./bin/tater-re --workspace .tater-re init
./bin/tater-re --workspace .tater-re ingest /path/to/extracted/vendor
./bin/tater-re --workspace .tater-re inventory
./bin/tater-re --workspace .tater-re search libvcodec
./bin/tater-re --workspace .tater-re image inspect /path/to/boot.img
./bin/tater-re --workspace .tater-re report capabilities --format markdown
```

The workbench includes SHA-256 inventory, GPT/boot/ext4 inspection, ELF
dependencies, strings with exact offsets, and optional Ghidra batch/export
support. Ghidra is installed separately; `debugfs` is needed for ext4 and `dtc`
for device-tree source rendering. On macOS the optional image tools are:

```sh
brew install e2fsprogs dtc
```

With an approved test device attached over ADB, the snapshot command is
read-only and does not pull proprietary libraries or calibration contents:

```sh
./bin/tater-re --workspace .tater-re device snapshot
```

Use `--serial SERIAL` when multiple devices are attached. See
`./bin/tater-re --help`, the [roadmap](docs/roadmap.md),
[hardware-access matrix](docs/hardware-access-matrix.md), and
[dated findings](docs/findings/) for the full research trail. Earlier findings
describe the state of their particular trial; current firmware release
qualification is recorded separately in the firmware repository.

</details>

## Safety and licensing

Read the [research policy](docs/research-policy.md) before adding device
operations. Hardware jobs, mixer writes, kernel experiments, and flashing are
not read-only diagnostics: they require target-specific review, operator
approval, and a recovery plan. Do not run an experimental probe on a different
model by removing its guards.

Keep vendor libraries/firmware, calibration, backups, keys, device identities,
recordings, and Ghidra projects outside Git and public artifacts. Only original
tools, clean implementations, hashes, and nonprivate evidence belong here.

Unless a file says otherwise, original Tater code and documentation are
[AGPL-3.0-only](LICENSE). MIT-marked helpers and GPL-2.0-only kernel work keep
their own licenses; read [NOTICE.md](NOTICE.md) before reusing or distributing
components. Neither these licenses nor a source release grants permission to
redistribute proprietary vendor files.

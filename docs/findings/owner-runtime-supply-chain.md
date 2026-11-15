# Owner-local hardware runtime supply chain

Checkpoint: 2026-11-15. Offline source/provenance audit only; no USB console,
camera, kernel, firmware, installed library or release gate was changed.

## Checkers migration issue

The previously tested Checkers decoder tree
`490a3c771d1c6f979828e60b93c363ec42403ca9bd3c98b12f8c5cd0761f2cc4`
contains 76 files matching the original private FireOS collection. Its decoder
uses Android 5 GraphicBuffer/native-buffer interfaces. The USB factory instead
installs a pinned Lineage image to obtain retained vendor files, then removes
Android userspace. That does **not** reproduce this old decoder runtime.

The exact pinned Checkers Lineage ZIP (SHA-256
`785fa643fd68b2e6f6f02d96a2da58373c6a577b92a27cf6cec69603bb94068e`)
was reconstructed offline from its full-OTA block transfer list and Brotli
data. Its vendor decoder, encoder, driver, OAL and utility bytes match the
newer retained-library family used by Cronos/Crown/Rook, not the old Checkers
FireOS components. The pinned Rook ZIP was independently reconstructed too
(SHA-256 `2755428c124df88ffd3bc6a9c36db16bbe3b7e639883fd1092dc839ac5ae9d13`).

Simply accepting the newer component hash in the old Checkers helper would
mix incompatible buffer/parameter interfaces. No such hash-only substitution
was made.

## Reproducible MT8163 input assembly

`platforms/mt8163-lineage-runtime-sources.json` records each output file's
source kind, relative source path, hash and any explicit transformation.
There are no device identities, backups or vendor bytes in this metadata.

| Library set | Files | Inputs |
| --- | ---: | --- |
| GPU | 34 | 6 SDK bootstrap, 17 VNDK files including 3 transformed copies, 5 retained vendor, 6 original shims |
| Codec | 66 | 6 SDK bootstrap, 28 VNDK including 3 transformed copies, 27 retained vendor, 5 original shims/aliases |

The bootstrap is the Android SDK API24/default/armeabi-v7a revision 7 runtime;
the VNDK v28 reference commit is
`1cb210981d2baf325aaab36cdd84afa40c775f00`. These are internally pinned public
inputs, **not** another owner's Android system libraries. Input availability
does not waive the inputs' license/notice requirements or authorize publishing
vendor files.

`scripts/assemble_mt8163_lineage_runtime.py` accepts a retained `vendor/lib`
tree, extracted public SDK bootstrap, public VNDK checkout and NDK r21e. It:

- checks all named inputs and original shim sources before building;
- rebuilds six GPU compatibility DSOs, the diagnostic-only ION shim and a
  GLES dependency alias (actual GLES symbols remain in the retained DDK);
- applies the narrow public libc-O import-version transformation to copies;
- verifies every generated/copied file and both aggregate tree identities;
- writes a private local output only, refusing an existing output or a
  release/factory destination. It neither downloads nor accesses a device.

Fresh assembly from the **Checkers installer image's vendor files**, rather
than a prepared research runtime, exactly reproduced:

- GPU: 37,506,624 bytes,
  `bc75d30a7be167b827e89b1d7629ec5dc4b528c99106d0b1e9beed02b4815561`.
- Codec: 41,926,684 bytes,
  `8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a`.

A second independent assembly using vendor files extracted from the pinned
Rook installer reproduced these same complete trees and byte totals.

Example, with user-supplied local paths and an explicitly private output:

```sh
python3 scripts/assemble_mt8163_lineage_runtime.py \
  --vendor /private/owner/vendor/lib \
  --sdk /private/owner/public-sdk24-runtime \
  --vndk /private/owner/vndk28/arm/arch-arm-armv7-a-neon/shared \
  --ndk /private/owner/android-ndk-r21e \
  --output /private/owner/assembled-libraries \
  --acknowledge owner-local-runtime
```

The SDK input contains `linker` and `lib/`; `--vendor` is not an arbitrary
Android system tree. Hash checks fail closed on other versions. Automatic
download/extraction and first-install integration are implemented separately in
the firmware factory installer; the offline tool never contacts a device.

## Separate Checkers Lineage candidate

`scripts/build_checkers_lineage.py` builds two original ARMv7 clients against
public OpenMAX headers and NDK r21e, with **no FireOS link-library input**.
The source/output lock is `platforms/checkers-lineage-candidate-lock.json`.

- Decoder: the shared linear-output/parameter-word-1 ABI, with an independent
  exact Checkers product/kernel/panel guard, TVO2 timestamps and bounded batches.
- Encoder: reuses the original Checkers Bayer conversion, dimensions, tone LUT,
  Y/V/U ordering, framing and deadlines. A separate entry point supplies the
  stricter Checkers guard and retains vendor DSOs until process exit after OMX
  teardown; no sensor timing/capture changes are introduced.
- Interpreter: `/data/tater-vdec-lineage-1/linker`, deliberately separate from
  the currently installed FireOS runtime. Candidate library manifests use
  `checkers-mt8163-vdec-lineage-1` / `checkers-mt8163-venc-lineage-1`.

Two clean offline builds produced the same executable hashes:

- VDEC: `247f414fd971554227ae422357062b5ed691836b8f66516c0884fc1c0ff7cae2`.
- VENC: `2d5ad722019a6744568a4b445e2108242dafe5d20d2f1a6913e04cb0ba99b4ea`.

`scripts/prepare_checkers_lineage_candidate.py` verifies these clients and the
assembled libraries, then prepares **versioned private trial directories**.
It does not install them. The firmware's new Checkers factory profile selects
these versioned paths, while the old library directories remain separate.
Published RC.1 archives and all physical release flags are unchanged. The
locked public helper builder also supports the new profile without taking
FireOS link-library inputs.

Before replacing the production profile, require an operator-present Checkers
trial: generated encoder/decoder color and temporal-order comparison, repeated
teardown, real two-way calls, camera/privacy controls, immediate first-word
voice capture and rollback. Host tests cannot qualify codec/kernel behavior.

## Other targets and remaining factory work

- Cronos/Crown/Rook retained vendor collections match the shared family; the
  pinned Rook installer independently supplies the expected vendor bytes.
  Each target must still pass its own exact helper/board guards and full USB
  workflow. A matching library does not qualify another board automatically.
- Athena's current AP-controlled codec startup intentionally keeps SCP
  **offline**. It does not need the experimental SCP firmware from earlier
  trials. Its separate 24-file decoder and 55-file encoder trees now have a
  complete public bootstrap/shim recipe and retained-vendor factory integration.
  The previously unclear `liblog`, `libsync`, native-window anchor and SP-HAL
  loader dependencies are all independently pinned and reproduced. See
  `platforms/runtime-compat-lock.json` and `scripts/build_runtime_compat.py` for
  the nine source-built compatibility DSOs, corresponding source packet and
  upstream sync notice. The GLES alias uses a hash-pinned retained DSO only as
  link metadata; the proprietary DSO is never copied into its public output.
- Biscuit/Radar do not need these GPU/video library sets. Their public kernel
  audio/recovery changes still need full migration/hardware qualification.

The firmware installer prepares and verifies public inputs before flashing,
then assembles device-local libraries from its retained vendor partition after
the new system boots. Complete assemblies and repeat installation were checked
offline for all five screen targets; interrupted-root installation/retry and
invalid existing-file refusal are covered by Linux shell tests. The bundled
portable reader extracts only bounded, hash-verified files from two pinned
public Android images: no filesystem mounts, administrator privileges, NDK,
FireOS backup or separately prepared private runtime is needed by the user.

The remaining gate is the complete physical USB workflow and hardware checks
on each target, including the revised Checkers decoder and separate-microphone
kernel. Do not depend on developer caches or publish prepared vendor runtimes.

This audit therefore does not authorize the final 5.0.0 tag or establish that
the end-user USB installer is already complete.

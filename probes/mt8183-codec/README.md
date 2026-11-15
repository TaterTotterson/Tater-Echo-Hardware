# Athena AP codec helpers — experimental

Original userspace adapters for Athena's separate hardware video encoder and
decoder. These are not GPU shaders and are not a replacement for the existing
Panfrost/Mesa UI. Owner-local codec libraries remain external to this repository.

Current scope:

* `athena_vdec.c`: continuous protocol-v2 H.264 input and timestamped TVO2 RGBA
  output. Only progressive, single-slice Baseline I/P pictures with unchanged
  SPS/PPS are accepted. Main/High, B pictures, multiple slices and sequence
  changes are rejected, not silently advertised as supported media playback.
  Generated shapes qualified: 640×480, 768×432, 1280×720 and 1280×960.
* `athena_venc.c`: experimental continuous NV12 input and timestamped TVE1 H.264
  output, initially 640×480/30, Baseline/no B pictures. It does not open a camera
  or perform Bayer conversion. Live camera integration is a separate milestone.
* `ap_legacy.c`: original published-ABI translation to exclusively owned native
  Linux DMA/IRQ services. It never passes unknown legacy ioctls through.
* `ap_sandbox.h`: syscall boundary installed before owner libraries are loaded.
  Only the already-open AP adapter's enumerated ioctls are allowed; no network,
  execution, writable opens or module operations. The encoder permits bounded
  process-local threading. Only ITIMER_REAL can operate the process watchdog.
* `ap_decoder_abi.h`: original native-clock adaptation. The common clock page
  stays read-only; only source-confirmed, already-satisfied enable requests are
  translated to barriers, and the kernel owns the fixed DCM operation. Actual
  video register operations and real interrupt waits are not skipped or faked.

Input/output sizes, dimensions, timestamps, buffer identity and recycling are
checked. Memory is fixed-size; input/output operations have absolute deadlines,
codec operations have a process watchdog and actual interrupt waits are capped
at one second. Broken input, a closed output and cancellation must fail closed;
the kernel resets/powers off before freeing retired DMA allocations.

## Reproducible, offline helper build

`platforms/athena-codec-lock.json` pins original source hashes, public header
hashes, NDK r21e identity and the helper output. The VAL headers are from the
GPL MT8183 tree at commit `92f9c3e3079403a34b77193451775a0ce7f3054b`
(`drivers/misc/mediatek/videocodec/include`), not the incompatible older
88-byte memory layout. Published VDEC interface headers come from the separately
pinned MT8163 reference; Athena-specific callback/free-input enum differences
are explicit in the adapter and validated on hardware. No reconstructed
proprietary implementation is included.

```sh
python3 scripts/build_athena_codecs.py \
  --lock platforms/athena-codec-lock.json \
  --ndk /path/to/android-ndk-r21e \
  --val-headers /path/to/pinned-mt8183-headers \
  --vdec-headers /path/to/pinned-mt8163-headers \
  --omx-headers /path/to/pinned-frameworks-native/include \
  --output /path/to/new-helper-output
```

The builder does not link or copy owner libraries. Runtime preparation is
separate: `scripts/prepare_athena_codec_runtime.py` accepts only the exact
24-file owner-local decoder closure in `athena-codec-runtime-lock.json`, refuses
extra files/symlinks/overwrites, and writes a local runtime plus manifest.
Never publish that output. The encoder is built separately from the same lock;
its additional owner-local OMX dependencies still require separate provisioning.

Generated pixel/timestamp tests are necessary, not sufficient for release.
Matched kernel/rootfs packaging, camera privacy, sustained live calls, real
media compatibility and OTA/rollback still require their own qualification.
Nothing here automatically loads a module or enables satellite capabilities.

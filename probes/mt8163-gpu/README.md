# MT8163 Mali off-screen probe

## Generated-only Cronos camera conversion benchmark

`bayer10_offscreen_android.c` is a separate experiment, not an installed camera
helper. It generates RAW10 patterns and tone tables, compares GPU RGBA and I420
bytes against the established CPU converter, then measures uploads, conversion
and encoder-ready readback together. It never opens a camera or framebuffer.

Build the three opt-in variants (ES2 float, ES3 integer, and hybrid CPU unpack):

```sh
make android-bayer-bench ANDROID_CC=/path/to/armv7a-linux-androideabi21-clang
```

On the exact qualified Cronos only, after the target-specific safety review in
[the findings](../../docs/findings/cronos-gpu-bayer-conversion.md), a bounded
invocation with the owner's existing private runtime is:

```sh
timeout 40 env LD_LIBRARY_PATH=/data/tater-gpu/lib \
  LD_PRELOAD=/data/tater-gpu/lib/libbionic28compat.so \
  /tmp/cronos-bayer-es3 --generated-bayer --target cronos \
  --acknowledge-gpu-job private-pbuffer-no-camera --bounded run
```

`TATER_GPU_BAYER_PASS` means generated-color accuracy passed, **not** that the
GPU beat the CPU, camera streaming is qualified, or Sat Chat jitter is solved.
Do not run during a call or silently replace the production color path.

## Original GPU/display probes

This clean-room ARM32 probe tests the owner-operated Checkers Mali T720 through
the standard public EGL and OpenGL ES 2 interfaces. It loads the exact
owner-local `libGLES_mali.so`, creates a 32x32 pbuffer with no native window,
clears it to a known color, finishes the GPU job, reads the pixels back, and
checks every channel. It never opens the framebuffer, display manager, input,
audio, camera, or persistent storage.

The binary is built against Android's public C ABI and uses an owner-local
Bionic runtime in volatile storage. The interpreter, vendor libraries, and all
runtime files remain ignored and are never committed.

Active mode is deliberately gated:

```sh
./mt8163-mali-gles2-probe --offscreen-gles2-pbuffer \
  --target checkers \
  --acknowledge-gpu-job private-pbuffer-no-display run
```

Success requires EGL initialization, an ES2 context, a pbuffer, a completed
clear, an error-free readback, and 1,024 matching RGBA pixels. The probe emits
only renderer identity and a small deterministic result summary; it does not
save the pixel buffer.

`tater-gpu-rotate` is the next clean-room integration probe. It uploads one
960x480 RGBA Tater canvas, rotates it into Checkers' 480x960 panel geometry in
a GLES2 shader, swaps red/blue into the panel's BGRA byte order, and reads the
completed pixels back. Its guarded self-test checks every output pixel:

```sh
./tater-gpu-rotate --self-test-rotate --target checkers \
  --acknowledge-gpu-job pbuffer-rotate-readback run
```

The `--stream-rgba-rotate-v1` mode exposes the same operation through a
bounded binary stdin/stdout protocol for renderer benchmarking. It remains an
off-screen pbuffer path: a successful test proves useful composition work on
Mali, not a Panfrost/DRM native-window presentation claim.

The connected Checkers proof matched all 460,800 rotated pixels. The helper's
60-frame upload/draw/readback benchmark averaged 7.612 ms (12.679 ms maximum),
and the complete Go pipe round trip averaged 12.744 ms (18.532 ms maximum).
Tater uses this only for large frames; small dirty rectangles remain on its
cache-friendly CPU path. Output is staged in ordinary RAM before one contiguous
framebuffer copy, so a short helper response cannot leave half a visible frame.

The retained-scene v2 interface accepts a bounded 40-byte `TGS2` command
containing an optional full RGBA source, crossfade progress,
animation time, accent/level, and bounded glow/dot flags. Text/layout stays in a
GPU texture between updates. Before replacing it, `glCopyTexSubImage2D` snapshots
the last completed panel image; interrupted menu and transcript fades therefore
start from the exact currently displayed result. GLES2 performs the crossfade,
edge/ambient light, and thinking dots. Normal `TGI1` frames still select plain
rotation/swizzle, preserving compatibility with video and existing renderers.

The Tater renderer selects this retained path for Checkers home, menu, climate,
and conversation surfaces and keeps the existing CPU/video paths for other
surfaces. The runtime preparation tool pins the new helper separately from the
unchanged device-owned library tree. Existing rotation-only runtimes remain
accepted by the newer renderer.

The current helper advertises `scene_version: 3` and adds `TGS3` text frames.
The 56-byte header carries the existing scene effects plus bounded atlas
dimensions, optional RGBA atlas bytes, and a quad count (at most 96). Each
20-byte quad contains a uint16 atlas rectangle and float32 destination X/Y and
opacity. Atlas pixels are uploaded only on visible text changes. One blended
GLES2 triangle batch translates/fades the premultiplied glyphs, preserving the
normal scene snapshot and the original rotation/v2 protocols. Atlas dimensions,
payload length, quad bounds, coordinates, and finite opacity are validated
before drawing. Stable words are matched in the Go renderer so new partial STT
results do not move or refade the whole paragraph.

The 2026-11-12 word-animation test verified glyph movement, opacity, channel
swizzle, and retained-atlas updates on the connected GPU. The real display
presented 120 streaming STT/thinking/reply frames at 29.21 fps with 0.372 ms
average CPU layout and 31.868 ms average complete presentation. Offscreen
previews show the unchanged prefix remaining opaque while newly appended words
rise and fade into place. The test renderer is installed as a temporary mount;
rebooting restores the device's installed firmware.

On 2026-11-12, the connected Checkers passed all 460,800 rotation pixels, exact
fade start/end and interrupted-fade readback tests, and GPU-rendered menu/STT
previews. A 120-frame retained scene with rim, ambient glow, and thinking dots
averaged 17.746 ms per Go/helper round trip (30.635 ms maximum) while the normal
renderer remained running. These are offscreen timings, not a 60 fps claim;
the UI retains its 30 Hz target and avoids jobs for settled static screens.

A separate sole-owner framebuffer test then presented 120 synthetic listening,
streaming-STT, thinking, and reply frames at 29.22 fps. CPU layout work averaged
0.381 ms per frame and complete presentation averaged 33.024 ms including page
flip synchronization. The slowest changing-text frame was 50.985 ms. The normal
renderer resumed automatically after the bounded test; microphone/audio services
were not stopped. The updated renderer/helper are mounted temporarily on the
test unit, so a reboot restores the installed versions.

`scripts/prepare_tater_gpu_runtime.py` prepares the reviewed owner-local
runtime. It verifies the complete linker/library tree and helper identity,
copies only the allowlisted runtime files, and emits a read-only manifest used
by Tater's startup preflight. The resulting directory remains private and must
not be added to a release or repository.

## Android 9 compatibility runtime

The current r26p0 vendor DDK declares Android API 28 but Tater Linux does not
ship Bionic. The reproducible test runtime combines:

- an owner-local 32-bit Bionic linker and four LL-NDK libraries;
- Google's public ARMv7 Android 9 VNDK snapshot from
  [`platform/prebuilts/vndk/v28`](https://android.googlesource.com/platform/prebuilts/vndk/v28/);
- the exact owner-local DDK and MediaTek vendor dependencies; and
- the clean-room compatibility DSOs in this directory.

No runtime binary is committed. Build the probe and compatibility DSOs with
NDK r21.4 (or an equivalent compiler) by setting the two compiler paths:

```sh
make android-api28 \
  ANDROID_CC=/path/to/armv7a-linux-androideabi21-clang \
  ANDROID_API28_CC=/path/to/armv7a-linux-androideabi28-clang
```

The minimal VNDK-SP set used by the test is `libbase`, `libc++`, `libcutils`,
`libhardware`, `libhidlbase`, `libhidltransport`, `libhwbinder`, `libion`,
`libutils`, `libunwindstack`, `liblzma`, `libz`, and
`android.hardware.graphics.common@1.0`. The Bionic bootstrap supplies `libc`,
`libdl`, `liblog`, `libm`, and `libsync`.

The API 28 snapshot has four Android O imports that the bootstrap does not
export. Copy `libbase.so`, `libc++.so`, and `libcutils.so` before running this
strict, one-time version-index adjustment:

```sh
python3 neutralize_libc_o.py lib/libbase.so lib/libc++.so lib/libcutils.so
```

Then place the six generated compatibility DSOs in the private runtime's
library directory. The optional GED, gralloc-extra, and GPU-aux shims apply
only to this native-window-free pbuffer test; they deliberately disable those
paths and must not be used to claim native-window presentation.

Run with the compatibility DSO preloaded:

```sh
LD_LIBRARY_PATH=/tmp/android-gpu28/lib \
LD_PRELOAD=/tmp/android-gpu28/lib/libbionic28compat.so \
/tmp/android-gpu28/mt8163-mali-gles2-probe \
  --offscreen-gles2-pbuffer --target checkers \
  --acknowledge-gpu-job private-pbuffer-no-display run
```

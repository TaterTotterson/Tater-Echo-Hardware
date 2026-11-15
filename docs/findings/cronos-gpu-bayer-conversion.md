# Cronos generated-pattern GPU Bayer conversion prototype

## Scope and pre-run safety review (2026-11-13)

The user chose a GPU conversion prototype after native ISP processing caused
device hangs. Native ISP processing remains disabled; this experiment neither
loads its libraries nor opens its nodes. This is not a production camera change.

The original `probes/mt8163-gpu/bayer10_offscreen_android.c` uses only generated
1600×1200 packed RAW10 and generated 3×2048-byte tone tables. It compares a
640×480 RGBA result and contiguous I420 result against the existing CPU
converter, including CFA boundaries, channel ordering, LUT rounding, and chroma.
No scene recording, sensor access, framebuffer access, MMIO, camera ioctls,
privacy changes, service replacement, flash, or persistent configuration writes
are permitted by this probe.

The GPU work is explicitly mutating device activity, authorized as a prototype:
it allocates a private EGL ES2/ES3 context, pbuffer, textures and framebuffer object
through the owner's already-qualified isolated Mali runtime. It can contend
with the normal UI GPU context; it never takes ownership of the display. The
target gate checks Cronos product, exact qualified kernel, framebuffer metadata
read-only, and a character Mali node before loading the runtime. The invocation
also requires the explicit generated-only acknowledgment. The executable has a
45-second alarm; the reviewed owner-local runner additionally uses a shorter
process timeout and durable serial capture. Only a uniquely named `/tmp`
executable is transferred, with hash verification and no replacement of an
existing file. No proprietary runtime or calibration data is added to source.

Read-only pre/post checks must confirm normal daemon, renderer and camera
service PIDs, no ISP node owners, no unexpected ISP interrupts, and no active
Sat Chat call. If USB stalls, stop rather than automatically retrying GPU work.

## Acceptance criteria

- Generated gray, red, green, blue, bars, gradient, noise and nonlinear,
  channel-specific LUT cases agree with the established CPU conversion.
- Report max/mean byte error for RGBA and I420; fail on any error over one byte
  or mean absolute error above 0.01. Exact agreement is preferred.
- Benchmark includes source/tone uploads, both shader passes, and I420 readback.
  It excludes extra RGBA diagnostic readback, just as a real encoder path would.
- Compare wall time and process CPU time against the existing four-thread CPU
  converter on this same device. Shader time alone does not establish a win.
- Synthetic results do not qualify live camera capture or end-to-end Sat Chat.

## Initial generated-only results

The first ES2 attempt completed and left all service PIDs and ISP IRQ counts
unchanged, but failed accuracy. Explicit power-of-two unpack multipliers and
the exact 2.5× sampling ratio removed the numerical errors. The second attempt
matched every RGBA and I420 byte in all eight patterns. Its end-to-end GPU
median was 96.915 ms versus 31.198 ms for the existing four-thread CPU path;
process CPU time was 8.250 versus 57.750 ms per frame. This is CPU offload but
not a latency win and must not be enabled as a performance improvement.

The next reviewed variant requests a private ES3 context and uses integer
textures, integer unpacking and `texelFetch`, replacing float address/unpack
math. Its memory bounds, generated inputs, runtime, target gate, private
pbuffer, watchdogs and no-camera/no-display ownership boundary are unchanged.
It uses a private vertex buffer (required for an ES3 context), not a display
buffer. Failure to obtain ES3 is a test failure, not permission to change a
driver or firmware.

No live camera or released firmware integration is qualified by these tests.

The ES3 integer variant also matched every byte, with 36.110 ms GPU median
versus 34.641 ms CPU median; process CPU cost was 7.000 versus 57.500 ms.
This is promising offload, not yet a demonstrated wall-time improvement.

A final generated-only hybrid variant keeps packed-byte expansion on the CPU,
uploads a private R16UI texture, and leaves demosaic, scaling, tone mapping and
I420 conversion on the GPU. CPU expansion and the larger upload are included
in its benchmark. It uses the same buffers/size limits and no new device API;
the extra private CPU buffer is bounded at 1600×1200×2 bytes. The longer run
uses 64 frames, still inside the existing 40-second outer timeout. This is a
measurement variant, not an automatic fallback or production setting.

## Confirmed outcome

All corrected variants matched every RGBA and I420 byte across all eight
generated patterns. No +/-1 tolerance was needed in the passing runs.

| Variant | Frames | GPU wall median | CPU reference wall median | GPU path process CPU/frame | CPU reference process CPU/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| ES2 packed float | 16 | 96.915 ms | 31.198 ms | 8.250 ms | 57.750 ms |
| ES3 packed integer | 16 | 36.110 ms | 34.641 ms | 7.000 ms | 57.500 ms |
| ES3 hybrid CPU unpack | 64 | 42.805 ms | 29.889 ms | 17.375 ms | 56.688 ms |
| ES3 packed integer, confirmation | 64 | 36.401 ms | 30.686 ms | 7.500 ms | 56.062 ms |

The confirmation run ranged 33.006–46.628 ms on GPU and 25.441–52.328 ms on
CPU. The GPU path used about 87% less aggregate **conversion-process** CPU
time, but its median frame latency was about 19% higher. CPU time includes
the CPU converter's worker threads; it is not whole-device CPU utilization.
These short synthetic runs are not sustained FPS or camera-stream claims.
The normal renderer remained running, but no active call or deliberate UI
animation load was present. Sensor capture, application pipes, network relay,
encoding and display are outside this benchmark.

Cronos retained its normal service PIDs after every run, with no ISP ownership
and no ISP interrupts. No firmware, slot, startup path, installed helper or
user setting was modified. The native ISP processing quarantine stays intact.

**Decision:** retain the existing CPU camera conversion as the production
default. Keep the ES3 integer path as a separately built research candidate.
It is genuine working GPU color conversion, but not evidence that GPU
conversion will solve end-to-end Sat Chat jitter. A separately reviewed,
reversible live-camera trial would need to measure full-pipeline frame age,
frame pacing, audio stability, UI contention and fallback before integration.

### Reproducibility

The explicit `android-bayer-bench` Makefile target builds all three variants;
none is a dependency of the release GPU helpers. Source is original C/GLSL
using public NDK EGL/GLES interfaces. Runtime libraries remain owner-local.
Tests cover generated primary colors, I420 U/V ordering, output bounds,
target/acknowledgment/watchdog gates, exact unpack math and build isolation.

Private transfer/capture logs retain the binary hashes. Key executed identities:

- Correct ES2 (`cronos-gpu-bayer2`):
  `348dc8a18c5a51176b6ac1175c831b7b1d6769427db417d06f267d3e0a67bd29`.
- Initial ES3 integer (`cronos-gpu-bayer3`):
  `ad7503a53f1d282cf7b3b85e59cdac921aecea7311211751586170a8c29bc1d0`.
- Hybrid (`cronos-gpu-bayer4`):
  `b30a0d358122f817c9e39090ce133ed042fbf9d456b4c25f70786b11732aef23`.
- Final 64-frame ES3 integer (`cronos-gpu-bayer5`):
  `218c6f03239a7816b7d0686318e296184ef08847e2fa1af1dc2fdde8dff0223e`.

The final probe identified ARM Mali-T720, OpenGL ES 3.1, runtime
`v1.r26p0-01rel0.526d936ea9da20486773a9aaceecd920`, fragment highp precision
23 bits. It was built with NDK r21e's ARMv7 API21 Clang, `-O2`,
`-DTATER_GPU_BAYER_ES3 -DITERATIONS=64`, warnings as errors, `-ldl -lm`, and the
already-installed isolated `/data/tater-gpu/linker` interpreter. Do not treat
the experimental executable hashes as production runtime manifest entries.

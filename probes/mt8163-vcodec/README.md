# MT8163 Vcodec probes

## Native camera encoder

`android-tater-venc` builds the original Checkers H.264 encoder adapter. It
uses a separately hash-qualified owner-local component, accepts only bounded
synthetic YUV or framed Bayer/tone data, and never opens the camera, microphone,
display, or network itself. Native 720p capture/encoding reached about 30 fps;
simultaneous capture plus local hardware decoding measured about 19 fps.
The clock finding, active gates, wire formats, exact identities, loss-recovery
results, and remaining release qualification are recorded in
[`checkers-native-camera-h264.md`](../../docs/findings/checkers-native-camera-h264.md).
No vendor library is distributed by this build target.

## Decoder probes

This clean-room ARM32 probe validates the smallest confirmed hardware-control
boundary for the Checkers video decoder. Its default `--describe` mode is
host-safe and prints the fixed compatibility ABI without opening a device.

The active mode opens `/dev/Vcodec`, supplies a resident private handle page,
locks the H.264 decoder hardware, holds the lock for two seconds, unlocks it,
and closes the descriptor. The public kernel driver uses this bounded pair to
enable and then disable the VDEC power domain, clocks, and interrupt. The probe
does not map VDEC registers, configure a bitstream or frame buffer, start a
decode, wait for a decode interrupt, touch the display, or write persistent
storage.

The public MT8163 driver can return its positive `VAL_RESULT_INVALID_ISR`
first-use status after it has already acquired and powered the otherwise-idle
decoder. Its common ioctl tail also returns positive status `255` after a
successful request. The probe recognizes only zero and those two exact legacy
success statuses, treats the lock as held, and still performs the matching
unlock. All other returns fail closed.

Active mode is guarded by the complete invocation:

```sh
./mt8163-vcodec-lock-probe --execute-lock-cycle --codec h264-dec \
  --target checkers --acknowledge-vdec-power-cycle bounded-no-register-access
```

Before execution, verify that no process owns `/dev/Vcodec`, the device has a
stable power source and tested recovery path, and the running kernel matches
the reviewed MT8163 public implementation. Termination signals are blocked
across the lock/unlock sequence so cleanup cannot be interrupted.

`capability_probe_android.c` is a separate, read-only public-ABI client. It is
built with an Android NDK and queries `eVDecDrvQueryCapability` from the
owner-local MT8163 library. It does not create a decoder, open `/dev/Vcodec`,
allocate buffers, or change hardware state. Vendor libraries, the Android
linker, and all runtime build products remain in ignored local storage and are
never committed. It also queries the published queue-input feature bit. This
exact runtime returns success while reporting that bit clear, so multiple
queued input buffers are not a public capability on Checkers.

`lifecycle_probe_android.c` advances one deliberately small step beyond the
capability query. Its guarded mode creates an H.264 decoder handle through the
published `eVDecDrvCreate` ABI and immediately releases it through
`eVDecDrvRelease`. It does not initialize the decoder, allocate video buffers,
lock or power the hardware, parse a stream, or touch persistent storage. The
probe reports only result state and never prints the opaque decoder handle.

`memory_probe_android.c` exercises the matching published video-memory ABI
without starting a decoder. Its guarded mode requests one 1 MiB, 4 KiB-aligned,
non-cacheable VDEC buffer, verifies that the returned mapping is readable and
writable, and frees it. It reports only whether virtual and hardware addresses
were supplied; their values and the private ION handle are never printed.

`mva_probe_android.c` tests the other published allocation route: it creates a
private anonymous CPU mapping, asks the stock utility to map that range into
the VDEC address space, then removes the mapping. This keeps all addresses
private while confirming that an ordinary writable buffer can be given a
temporary hardware-visible address without initializing a decoder.

`init_probe_android.c` combines those verified pieces for bounded decoder
initialization. It accepts only a small Annex-B H.264 fixture containing SPS,
PPS, and IDR units, maps it into VDEC, creates the stock H.264 decoder, calls
the published initialization entry point, checks the returned dimensions,
selects the published external shared-buffer mode and 16x1 block YUV type, and
then deinitializes and releases everything.

Its separately guarded `--decode-one-h264-frame` mode adds one 1 MiB private
output mapping, supplies the published frame-buffer structure, and requests
one IDR frame plus the documented null-input drain when output is delayed.
Success requires the bounded output region to differ from its sentinel, have a
changed hash, and contain at least three distinct byte values. It invalidates
the output cache through the stock utility, never displays the frame, and
never writes decoded pixels to persistent storage.

The live 128x128 test changed all 24,576 YUV420 bytes and produced three
distinct values while ftrace recorded real `vdec_sel`, `vdec_cken`, and
`vdec_larb_cken` activity. The reproducible result is recorded in
[`docs/findings/checkers-live-media-bringup.md`](../../docs/findings/checkers-live-media-bringup.md).

The guarded `--probe-yuv-layout` mode is restricted to a locally generated
synthetic fixture and its software-decoded reference. It proved an exact
layout match across all 24,576 bytes: luma uses raster-ordered 16x32 tiles and
chroma is interleaved UV in raster-ordered 16x16 tiles. The probe now also runs
the clean `mtk_yuv420_block` converter in memory and reports only the converted
RGBA byte count and hash. It never saves or displays the decoded image.

For a bounded, operator-present presentation test, the explicit
`--dump-one-h264-rgba FIXTURE OUTPUT_RGBA` mode writes that converted picture
as tightly packed RGBA8888. The file is exactly
`picture_width * picture_height * 4` bytes. This mode writes only the named
ordinary output file; it never opens the framebuffer, and presentation remains
a separate single-owner step through Tater's `PresentTransientRegions` path.

The guarded `--probe-synthetic-h264-stream` mode extends that proof across a
bounded sequence of 2–32 locally generated, visually distinct IDR access
units. The public decoder header supplies the low-latency mode and all buffer
structures, but this stock runtime exposes its streaming output-buffer request
only through the owner-local `eVDecDrvDecodeEx` compatibility entry point and a
12-byte callback registration. The clean probe supplies a separate aligned
input descriptor and a persistent private output slot for every requested
picture, validates every returned pointer against that owned pool, performs
cache invalidation and block-YUV conversion in memory, and requires one unique
RGBA hash per requested access unit.

The legacy decoder retains the last picture until a complete next picture is
available. For this synthetic all-IDR test only, the probe resubmits its first
already-owned access unit as bounded lookahead and excludes that lookahead from
the requested result count. A 10-picture moving fixture produced ten distinct
hardware frames, ten output callbacks, and ten released input descriptors; the
reported RGBA hash set exactly matched the software-decoded reference set. The
repeated standalone IDRs were reported with a one-picture display rotation, so
this raw-driver route proves sustained hardware decode and buffer turnover,
not conventional IDR/P/B presentation ordering. The separate stock-OMX path
below closes that gap.
Ten complete repetitions produced the same ten hashes and slot sequence with
no failures, for 100 hardware-decoded pictures in the repeatability run.

For the separate operator-present display test,
`--dump-synthetic-h264-stream-rgba FIXTURE FRAME_COUNT OUTPUT_RGBA` writes the
validated converted pictures consecutively as tightly packed RGBA8888. It is
bounded to 2–32 frames and at most 64 MiB, writes only the explicitly named
ordinary file, and never opens the framebuffer. A single-owner presenter can
then cycle those exact hardware outputs through Tater's transient renderer.

A separate normal IDR/P fixture confirmed the practical meaning of the clear
queue-input bit: the owner-local raw-driver queued-buffer route did not request
any output buffers for that stream. Experimental per-picture public calls
could be made to return changing buffers only by repeating parser
configuration, but their hashes did not match the software reference, so that
approach was rejected and is not exposed by the probe.

The stock OMX component provides the correct higher-level normal-GOP path.
`omx_probe_android.c` and `omx_native_buffer_android.cpp` exercise its standard
Android native-buffer extensions with an owner-local Android 5.1 runtime and
matching stock gralloc module. Tater Linux does not run Android's Binder
service manager, but the decoder only requests the optional MediaTek GUI
extension pool. The probe supplies a bounded local placeholder for that
singleton and reports the optional allocation unavailable; decoder memory,
ION import, VDEC submission, callbacks, and cache management remain stock.

The guarded `--decode-avc-native-stream` mode performs the complete
Loaded-to-Idle-to-Executing lifecycle, handles the component's output-port
renegotiation to MediaTek native color format `0x7f000001`, registers direct
ION-backed gralloc surfaces, and converts their 16x32-luma/16x16-UV block
layout to tightly packed YUV420P in memory. A conventional one-IDR-plus-nine-P
fixture returned all ten timestamps through EOS. The 245,760-byte output
matched the software reference byte-for-byte, with SHA-256
`2e099b0db78e6acc0cf83f41e9177694f7211d71a35506a0b6e4d7542b06a23c`.
A second `I,B,B,P,B,B,P,B,B,P` fixture also produced all ten frames in exact
software-reference order, with SHA-256
`fd14dff096322d0287976afb8b5ab33c089b803951c0f51982c9cc287b74af7c`.
This proves normal IDR/P/B decoding and presentation order. A redistributable
replacement for the proprietary component remains separate work.

## Reusable `tater-vdec` boundary

The 2026-11-12 Checkers live-call build uses an original NEON RGBA converter
with scalar clipped tails. Its exact helper SHA-256 is
`56091ad1146815d30be313be9e08cb671640033756de14f294f04ab04ef6ce91`.
A full clean Makefile rebuild with Android NDK r21e/API 21 reproduced that
identity. C flags were `-O2 -Wall -Wextra -Werror -std=c11`; C++ flags were
`-O2 -Wall -Wextra -Werror -Wno-unused-parameter -std=c++14 -fno-exceptions
-fno-rtti`. The converter rule additionally selects `-O3 -mfpu=neon
-mtune=cortex-a53`.

The public header checkouts were `android_frameworks_native`
`dd2ce3e0509deeeb3344f047d09b8661b360a968`,
`android_hardware_libhardware` `b5288579191c94da0ce1814dee0136eb49eec9ae`,
and `android_system_core` `2b5b9169a441f0ecb34651f146f37411a0a77c13`.
`OMX_CFLAGS` supplies frameworks-native `include` and `include/media/openmax`;
`ANDROID_NATIVE_CFLAGS` supplies libhardware `include`, system-core `include`,
and `libsync/include`. `ANDROID_STOCK_LIB_DIR` points only to the owner's
private reviewed link-library directory; libraries are not copied into this
repository. No extra `ANDROID_LDFLAGS` were used. See
`docs/findings/checkers-native-camera-h264.md` for sustained-test evidence.

The `android-tater-vdec` build target packages the same reviewed OMX lifecycle
behind a small process adapter instead of copying the decoder logic into
Tater. Its bounded batch mode accepts a finite, AUD-framed Annex-B H.264 batch,
2–120 pictures and at most 64 MiB of decoded output, and writes either tightly
packed YUV420P or RGBA8888 in presentation order. It accepts dimensions through
1920x1088. The component path is an argument; no vendor library or device path
is compiled into the adapter.

The connected Checkers test produced these bounded results:

| Input | Pictures | Output | Time | Reference result |
| --- | ---: | ---: | ---: | --- |
| 960x480, I/P/B | 60 | 41,472,000-byte YUV420P | 1.02 s | exact, SHA-256 `699887787639367d4eae2af20c4f50ac6e533a6667d216837c6a9b88e58bc5e8` |
| 1280x720, I/P/B | 30 | 41,472,000-byte YUV420P | 0.97 s | exact, SHA-256 `3e9d42286d51f05c33fcb9c4c0c67ffcd2973b6dbd89c1356cb48b49a3d50070` |
| 1920x1080, I/P/B | 20 | 62,208,000-byte YUV420P | 1.29 s | exact, SHA-256 `cd1b119870061bf9730a1737094b16ed0d9b1814e16d55266cf394d15235bccf` |

The 720p component surface was 1280x736 and the 1080p surface was
1920x1088. The adapter detiled the padded allocation and returned exactly the
visible 720 or 1080 rows; the host self-test covers the same crop contract.
These timings include hardware decode, cache invalidation, detiling, and file
output, but are batch throughput measurements—not yet an A/V synchronization
or long-play claim.

Direct 960x480 RGBA output decoded 30 normal I/P/B pictures in 1.04 seconds.
A two-second operator-present diagnostic passed those hardware pictures to
Tater's `PresentTransientRegions` path, completed 19 presentations, and then
restored the normal renderer. Worst recorded framebuffer conversion and pan
times were 36.543 ms and 16.043 ms. The diagnostic deliberately slept after
each frame, so its presentation count is not a maximum-frame-rate benchmark.

Streaming mode accepts repeated little-endian 32-bit length plus one complete
Annex-B access unit on standard input and emits fixed-size RGBA pictures on
standard output. A zero-length record or clean EOF ends the stream, and an
access unit is limited to 4 MiB. The host `tater-h264-packetize` fixture helper
builds this protocol from an AUD-framed stream while preserving parameter sets
before the first AUD.

Protocol v2 is selected with `--stream-h264-rgba-v2`. It retains the same
4 MiB access-unit limit but makes geometry and timing explicit. Each input
record is a 12-byte little-endian header followed by the access unit:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `uint32` | access-unit bytes; zero terminates input |
| 4 | `int64` | presentation timestamp in microseconds |

Output begins with this 32-byte little-endian negotiated stream header:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | 4 bytes | `TVO2` |
| 4 | `uint32` | header bytes, always 32 |
| 8 | `uint32` | negotiated visible width |
| 12 | `uint32` | negotiated visible height |
| 16 | `uint32` | RGBA payload bytes per picture |
| 20 | 4 bytes | `RGBA` |
| 24 | `uint32` | nominal FPS numerator |
| 28 | `uint32` | nominal FPS denominator, currently 1 |

Every picture then has a 16-byte header: `uint32 payload_bytes`, `uint32
flags`, and `int64 pts_us`, followed by the RGBA payload. A final record with
zero payload and flag bit 0 set marks clean EOS. The v1 mode remains available
for reproducing the original validation boundary.

A 30-picture 960x480 stream completed in 0.97 seconds and produced the exact
same 55,296,000 bytes as batch mode, SHA-256
`8a48b65ea4d5d01332b1a793ab92863c402ea628dccd73384ad497bee7dc913b`.
Two independent 180-picture streams both produced 331,776,000 bytes with
SHA-256
`dfeded23bf7a94c243118ddc95a04f989af275963698066729a2f2746bda992d`.
Both runs fed and copied all 180 pictures, reported no write failures, observed
EOS, and completed teardown. Streaming the same workload to `/dev/null` took
4.11 seconds, about 43.8 pictures per second; the roughly 15-second hash runs
were dominated by hashing and rereading 316 MiB on the target.

The same 30-picture fixture passed protocol v2 on Checkers. The adapter wrote
a negotiated 960x480/1,843,200-byte `TVO2` header, timestamps from 0 through
966,666 microseconds, and a clean EOS record. Removing the framing produced the
same pixel SHA-256 as v1,
`8a48b65ea4d5d01332b1a793ab92863c402ea628dccd73384ad497bee7dc913b`.

This proves a reusable picture-stream boundary, not A/V synchronization.
Tater still needs incremental demux packetization, timestamps, cancellation,
seek/pause/loop behavior, and audio-clock integration. Unsupported codecs,
missing owner-local runtime, or a failed preflight must continue to use the
current software decoder. See the
[production integration plan](../../docs/tater-vdec-integration.md).

No private library, binary-derived pseudocode, fixture, raw frame, address, or
device identifier is committed. The owner-local callback shim is isolated to
this research probe; a redistributable Tater media stack still needs a clean
public adapter or V4L2 bridge.

The converter is independent of the private decoder runtime and can be tested
on the host:

```sh
make -C probes/mt8163-vcodec host-check
```

It validates all dimensions and buffer sizes, detiles the hardware output, and
converts limited-range BT.601 YUV to RGBA8888. That output is suitable for the
existing Tater transient framebuffer presentation path; presentation itself
remains an operator-present test.

# Cronos GPU and codec bring-up

Date: 2026-11-13. Work in progress; not a release qualification.

## Latest mixed-call color investigation

The real balanced H.264 call still shows intermittent jitter and wrong colors.
Generated blue/red H.264 through Checkers' decoder gave mean RGB [0,0,255] and
[255,0,0]. Generated planar YUV through its original encoder and independent
software decode proved chroma-plane reversal: decoded U matched reference V,
and decoded V matched U. An original OMX-boundary Y,U,V-to-Y,V,U adapter reduced
U/V mean absolute error from about 42 to 0.54/0.70 levels, with luma unchanged.
The qualified corrected Checkers encoder helper SHA-256 is
`94dd2cc2edcc54b1938dff536ea44099ff3a1f7d22923dc6b137d9792279c4b4`.
These tests generated patterns, opened no camera and retained no scene pixels.
Cronos' independent generated-color test then confirmed the same layout issue:
its decoder reproduces exact mean blue/red, and its original encoded U matched
reference V (0.68 levels error) rather than U (42.28). Correcting only the OMX
input boundary gives Y/U/V error 0.682/0.537/0.680. Its corrected helper SHA-256
is `29bf2acbd3d2d2e0c69af6111d9a15d5c32404a0a9a6f1be5d4581d819d7cecc`.
Both helper sets reproduce in clean locked builds; real-call feedback remains.

Separately, concurrent local requests reproduced host event-loop stalls during
full satellite/settings snapshots. Off-loop settings hydration and lightweight
Sat Chat snapshots pass regression tests. Their effect on live jitter remains
to be measured in a call. After backend restart, concurrent tiny HTTP requests
were typically 2.5 ms (maximum 21.9 ms in 20 paired tests), while full settings
snapshots took 288 ms typically. The same tiny requests previously inherited
hundreds of milliseconds of the settings delay.

The operator authorized recoverable hardware experiments after the separate
[read-only baseline](cronos-read-only-baseline.md). Serial-numbered session
state, proprietary inputs and diagnostic logs remain in ignored local storage.
The corrected camera-clock kernel has been installed through verified TWRP,
with the original application/UI retained for that first boot. Wi-Fi, the
normal Tater connection and PCM 22 microphone capture recovered normally.
Neither recovery partition was changed. The shared GPU UI is now installed
and has passed separate system/application health trials and operator smoke
checks, as recorded below; this is still not public-release qualification.

## Mixed-device live-call checkpoint

The operator tested an updated Checkers against Cronos. Both negotiated the
H.264 profile, and the live Cronos process inventory confirmed hardware encode,
hardware decode and GPU composition. The call connected but was visibly laggy;
this does not qualify full-duplex performance. Decoder restarts were observed.

Regression tests reproduced a dependency-loss bug in the Python host relay,
Go satellite sender and local receive relay: JPEG-style one-deep queues discarded predictive H.264 frames.
The corrected bounded queues preserve short bursts, discard a whole pending
GOP on overflow and wait for a new keyframe. Startup replay also excludes
queued packets older than the priming keyframe. CPU nearest-neighbor scaling
was optimized with bit-exact padded/subimage tests; this is not GPU scaling.

The unpublished hw6 Cronos and ui.8 Checkers application builds contain these
fixes; installed component hashes on Cronos match the bundle. The operator
restarted the updated Tater test backend. Five-minute app health and mixed-call
performance retests remain pending. Proprietary drivers/runtime and the kernel
were unchanged by this lag-fix iteration.

### Subsequent cadence and screen-sized-video tests

Decoupling decoder draining from CPU display fitting improved smoothness but
did not eliminate rising backlog. Pacing independent sensor frames before
encoding at a maximum 15 FPS kept the decoder alive with an empty input queue
and mostly 196–263 ms source-PTS backlog during a several-minute real call.
The operator reported much smaller delay, but intermittent jitter remained.
These are relative decoder-backlog observations, not end-to-end latency.

Presenting ready pictures immediately instead of waiting for a menu-animation
tick brought prepared/displayed counts closer together, but the operator found
jitter unchanged. Timing diagnostics showed larger pauses already present
before presentation. No additional jitter buffer was added.

The operator then approved screen-sized H.264: Checkers 768×432 (16:9), Cronos
640×480 (4:3), preserving source field and still-photo geometry. Original
bounded converters sample the required output positions before YUV conversion.
Both hosts require explicit support from both peers; old peers retain their
previous profiles. Independent color/CFA/edge/buffer-boundary tests pass, and
both hardware bundles reproduce from their updated source locks. GPU and decoder
helpers, the private runtime libraries and kernel are unchanged.

Balanced encoder helper SHA-256 identities:

- Checkers: `c6fd51e3ae2b093972a426af8cdc18e3a7b464981e75fbe05daf5dd2f402bdc0`.
- Cronos: `9073ab2a593149c98f4f76faa3600ae233581732351ed4fb3b3309998e4d9add`.

Installed Cronos hw10 passed a 12-second in-memory hardware capture/production
relay/decode probe: 175 captured, 174 decoded, about 14.5 FPS, 640×480, first
picture 852 ms and maximum picture gap 134 ms. No scene pixels were retained.
The next real call was reported much smoother, but telemetry proved it was
JPEG fallback, not balanced H.264; it cannot qualify the new hardware profile.

Read-only USB inspection found Checkers' actual encoder manifest was schema 1,
still identifying its old shared helper. The correctly updated app helper was
therefore rejected during preflight. A regression reproduced that exact failure.
The corrected adapter accepts only the known historical target-owned manifest
identity while independently checking the current app executable, component,
tree and libraries. On-device no-capture comparison returned H.264 unavailable
with the installed adapter and available with the corrected adapter against
the same unchanged runtime. Actual balanced mixed-call testing remains open.

## Confirmed GPU milestones

- The Cronos-specific probes check the real product, exact running kernel
  release, 480x2880 framebuffer allocation and 32-bit pixel depth before
  opening Mali. They do not impersonate Checkers.
- A 32x32 EGL 1.4 pbuffer readback matched all 1,024 pixels on Mali-T720.
- Rotation/swizzle matched all 460,800 pixels. Sixty synthetic
  upload/draw/readback frames averaged 8.862 ms, maximum 16.754 ms.
- The production Go compositor, with a Cronos-specific manifest and helper,
  passed exact fade endpoints, interrupted fades and animated-text checks.
  A 120-frame retained-scene run averaged 13.896 ms, maximum 19.504 ms.
  These are offscreen timings, not live UI frame-rate claims.
- Prepared owner-local GPU libraries were added to a previously absent
  `/data/tater-gpu`. No existing runtime was overwritten. The firmware checks
  the distinct Cronos runtime ID and helper hash, preserving CPU fallback.
- The Checkers-default GPU rebuild remained byte-identical to its qualified
  helper. Only the explicitly Cronos-compiled variant adds its new gate.

Cronos production GPU helper SHA-256:
`481b74fb3a329e17039bc2012f5e616996648cf3f68a9a18956928a1d26b7ea5`.
Its library tree is the separately reviewed 34-file GPU runtime from the
baseline; proprietary libraries are not source or firmware-release inputs.

## Confirmed bounded codec milestones

The retained Cronos OMX decoder and encoder both accept parameter header
version word `1`, not the standard IL `0x00020101` used by Checkers' decoder.
This was independently tested in Loaded state before buffer/stream work.

The newer vendor ION diagnostic dependency pulls in an incompatible Android
unwinder. `libion_ulit` exports only `dumping_callstack`; the clean headless
replacement reports that the optional Android stack dump is unavailable.
Actual ION allocations, cache operations and codec work still use the owner's
libraries. This does not pretend that allocation/codec failures succeeded.
An attempted full unwinder compatibility experiment is retained privately as
a rejected path, not a production dependency.

- The encoder completed Loaded -> Idle -> Executing -> Idle -> Loaded for
  three synthetic 128x128 YUV pictures and produced 2,928 bytes of H.264.
  The helper exited successfully after explicit buffer/component teardown.
- The standard-buffer decoder (without Checkers' GraphicBuffer C++ adapter)
  decoded the three pictures, completed EOS and returned to Loaded state.
  Its 73,728 YUV420 bytes exactly matched the software reference, SHA-256
  `9e76212bafd9a953017803d447412357010af759545dd6f06f10fd82c888bc46`.
- Codec/ION ownership was released and the normal renderer/daemon and
  microphone capture continued. No real camera or microphone sample was used.
- The raw-driver capability-query experiment was rejected: its obsolete
  format query produced no useful format data and crashed during unload.
  A later component experiment exceeded its initial deadline; Ctrl-C stopped
  it without rebooting the satellite. Subsequent probes use background logs
  and an outer TERM-then-KILL deadline so they cannot monopolize the USB shell.
- Vendor DSOs remain resident for the isolated helper's lifetime. Explicit
  OMX teardown is required before `_exit`; unload-time compatibility is not
  inferred from these tests.

### Larger frames and production streaming

- Synthetic 1280×720 encoding produced 30 pictures in 0.395 seconds;
  1280×960 produced 30 in 0.467 seconds. These are burst encoder measurements,
  not sustained camera frame rates.
- Thirty 720p pictures decoded to 41,472,000 YUV bytes matching the software
  reference exactly: `af23fc05555259599cdf010c0ea9bbd85260b1da2c02daeba9a95b865da833e2`.
- The production standard-buffer client converts tight planar I420 to RGBA
  without linking Checkers' incompatible GraphicBuffer C++ ABI. It preserves
  input PTS in the existing TVO2 protocol, including a complete EOS record.
  Thirty 720p frames plus their headers/timestamps matched the scalar reference
  exactly: `7f5cc832b3aab9247316c2eee88e90de2a4c77909d767f52dd99d9bb0eb25471`.
- The NEON client repeated that complete stream hash. A separate owned-memory
  differential test checked 51,073,872 RGBA bytes across 21 black, saturated,
  random and clipped-tail cases, up to 1920×1088; every byte matched scalar.
- Kernel DTS identifies decoder SPI 212 and encoder SPI 206. Controlled jobs
  incremented their corresponding interrupt counts; this is actual codec
  activity, not merely a successfully loaded library.

Production decoder SHA-256:
`b6f9cf50e7a05aec9cf3a0cfaaee9277d0e16d8c81eda61662c6dc0331577f2d`.
Production camera encoder SHA-256:
`920bf4a08373566478c8913e5ab6e3cf161cce835137ae513164bada2fc52709`.
The separate 66-file owner-local codec tree is
`8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a`
(41,926,684 bytes). Public bundles contain original helpers only, not that tree.

## Camera and corrected kernel

Cronos' OV02B10 supplies 1600×1200 RGGB, not Checkers' 1280×720 BGGR.
The new transport preserves the full 4:3 field of view and develops a
1280×960 I420 stream. Native stills remain 1600×1200. Bayer/color processing
is CPU software; H.264 encode/decode and the UI compositor use hardware.

Read-only clock diagnostics initially found a 26 MHz CAMTG parent divided
by two: 13 MHz at the sensor, approximately 16 source fps. The exact public
kernel had the same missing CCF clock handles found independently on Checkers.
The generic clock-handle patch applies without changing Cronos' GPIO/privacy,
sensor tables, device trees, or configuration.

The complete build used the original GCC 8.3 toolchain and the captured live
configuration, SHA-256
`1967374f541393c4373beb0d4ee6bf152a9da22a6a4b0eb53526df4a419f04c6`.
All 203 Wi-Fi and 96 Bluetooth imported symbol CRCs matched the owner's modules.
The replacement preserved the original rescue ramdisk and every appended
device tree byte-for-byte. The 13,887,488-byte candidate boot image has SHA-256
`ec3868772d6c7fac421bf63bba0ae4a9c65058c020d73a2c07122d53b9764eff`.

Root TWRP access and the saved full boot/recovery/swdl hashes were verified
before writing only the independently identified physical boot partition.
Candidate readback matched; both recovery hashes remained unchanged. Normal
boot reached the retained good root slot with kernel
`4.9.337-g8d928c5176cc`, build `#2`, working wireless, Tater connection and
advancing microphone capture. No forced CPU floor or clock-proof module was used.

The new clock reads back as 24 MHz and supplies about 30 sensor fps. The
qualified CPU color path is currently slower: a bounded capture-only test
delivered 390 pictures in 20.001 seconds (19.50 fps), and the production
relay/decode test delivered 332 decoded pictures in 20.073 seconds (16.54 fps),
with first picture at 925 ms and maximum subsequent gap 114 ms. These are
development measurements, not a 30 fps full-duplex call claim. Repeated
stream cancellation completed normal encoder/sensor teardown.

## Installed workspace UI and application camera

The normal pinned ARMv7/ONNX build produced a Cronos-owned rootfs and full
application bundle from the exact target-locked helper set. Good original
system A was retained; new system B passed the real connected five-minute
health interval and automatic restart, returning root/store read-only.

Installed application `v4.2.2-cronos-hw4` (22,506,222 bytes) has SHA-256
`ed5b7014559fbc4d3149c42a167e221a25b90ce72badebf63f7aee4a586b7056`.
The production verified A/B transaction retained hw3 in B; replaced app
generations were backed up privately with gzip integrity checking. Its own
five-minute health trial committed normally. Installed daemon, renderer and
camera hashes match the bundle; pairing and device-token digests are unchanged.

The installed test exposed a supervisor-inheritance bug: the root camera
advertised H.264, but its app-slot child did not inherit encoder enablement.
The daemon now explicitly sets the app camera's qualified-target gate,
independently of older rootfs wrappers. This does not bypass runtime/helper
identity checks. The regression test also verifies nonqualified targets remain
disabled and inherited environment slices are not modified.

The operator passed menu layout, smooth fades, touch/vertical equalizer,
two wake attempts with immediate first-word capture, volume and mute/unmute.
GPU retained composition is active in the installed application. Its first
20-second production camera/relay/hardware-decoder run passed with 304 captured
and 303 decoded pictures (15.09 FPS), first picture 992 ms, maximum later gap
122 ms, successful bounded exit and released codec/sensor ownership.

A subsequent installed five-minute relay soak captured 4,639 pictures and
hardware-decoded 4,637 (15.45 FPS), with first picture at 866 ms, maximum gap
136 ms and the last picture at 299.969 seconds. It exited successfully after
300.130 seconds. Available memory samples remained around 602–608 MB under
load and recovered to 634 MB afterward; sampled CPU temperature reached 75°C.
No clock floor/thermal override was applied. These bounded observations are
not a long-duration/full-duplex or 30 FPS claim.

Three later cold restart cycles passed after allowing the normal camera's
five-second idle linger; checking closure after only one second was a test
mistake, not evidence of a sensor leak. A 20-second run deliberately dropped
16 predictive packets: 100 pictures decoded and output recovered through the
deadline, but maximum gap reached 4.459 seconds. That establishes bounded
recovery, not smooth playback under repeated loss. Native 1600×1200 JPEG
capture then passed in memory; hardware ownership was released afterward.

## Still required

Longer thermal/A/V runs, repeated-loss quality improvement, remaining operator
camera-cover/barge-in checks and application/system rollback
qualification remain open. The helper source/output locks and private-runtime
preparation are target-specific. No real two-device full-duplex call or
public-release qualification is claimed from a one-device loopback.

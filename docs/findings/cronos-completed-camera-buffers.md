# Cronos completed-camera-buffer selection

Date: 2026-11-13 local. Owner-authorized development qualification, not a
general claim about other sensors or a release approval.

## Current result: held-target handoffs

The subsequent full-duplex test added a generated downward-moving marker only
to the encoder's owned copy, after reading the real camera frame. The operator
reported the marker stayed in order while their arm jumped backward. This
separates camera acquisition chronology from the same live encoder/transport/
decoder/GPU path, unlike packet sequence numbers alone.

The accompanying bounded RAM-only trace tracked the last observed change of
each slot without logging fingerprints or pixels. In 1,800 callbacks (about
one minute), the previous-slot policy selected a slot older than the preceding
selection **406 times**. No selected slot changed during those callbacks;
there were eight observed skipped-tick intervals. Thus short-window stability
did not imply freshness or chronological order, and skipped ticks alone did
not account for the symptom. Exact silicon latch timing remains an inference.

A new candidate holds each ordinary IMGO target for two sensor ticks, then
publishes the preceding held target after another handoff. It never advances
imaginary targets for missed ticks. The selected bytes are copied into owned
memory before exposure/consumer callbacks, with the existing immutable Frame
copy retained. No new register, mapping, interrupt mode, clock, ISP job or
proprietary component is introduced. Other boards keep their old policy.

That candidate produced **0 older selections and 0 selected-buffer changes
over 1,800 callbacks** (about two minutes), while hardware encoding sustained
approximately 14.5–14.9 FPS. The operator confirmed no backward jumps initially
and after the extended call. Deterministic delayed-latch/slow-consumer models,
counter-wrap/startup tests, board gating, Linux race tests and the normal ARM
build pass. The normal firmware source contains the held-target implementation,
not the fingerprint code or generated marker.

The clean capture.2 application was installed through normal verified A/B
application OTA. All nine installed component hashes match; GPU/VDEC/VENC and
FFmpeg binaries and the private-runtime identities are unchanged. The operator
confirmed ordered motion and normal colors in the fresh full-duplex call
(approximately 105 seconds observed), then passed two immediate wake requests,
first-word capture, touch/menu and microphone mute/unmute. The normal five-minute
health trial committed without override. Normal camera ownership was restored;
codecs and sensor resources released after the call. No scene recordings,
temporary camera services, boot/rootfs slot changes or public release resulted.

This closes the reproduced backward-motion regression, not all possible
transport stutter: metadata still records isolated gaps and one opposite-
direction GOP drop. It is not a 30 FPS or zero-network-stall qualification.

## Symptom and isolation

During Cronos-to-Checkers hardware H.264 Sat Chat, slowly downward motion
appeared to jump backward. Checkers-to-Cronos looked normal. GPU/CPU display
comparisons and host relay changes did not remove this particular symptom.

Generated-only tests verified 120 ordered picture IDs through the installed
decoder, Cronos' encoder, and its normal Bayer/conversion/encoder path. The
exact Cronos-generated stream also decoded correctly on Checkers. A full
production receive/queue/GPU-overlay/display test presented 117 emitted
pictures in order in each of three sweeps; the operator saw normal motion.
These tests did not open the camera and did not prove live DMA ownership.

## Runtime evidence

The baseline camera helper was reproduced byte-for-byte:

`9fc199eddc7fd01ff04e632ad350c4426cd2a8ea8d9d3eb192d77e614f42920d`

Pinned platform source:

`3a0c2d480e281226d28c831730abc3301d605ba4`

Kernel release: `4.9.337-g8d928c5176cc`.

A bounded instrumented helper sampled small RAM fingerprints from each of the
three existing DMA slots, before and after a short interval. Only slot-change
flags, counters and timing were logged: no scene pixels, audio, fingerprints,
device identities, addresses or proprietary runtime contents are published.

- Baseline: the slot selected as completed changed in **35 of 118** checks
  whose actual observation interval was below 20 ms. The previous slot changed
  in **0 of 118** such checks.
- Candidate: selecting the previous slot and priming one additional startup
  tick produced **0 of 119** changing selected slots in the same short-window
  test.

This disproves the assumption that a Cronos TG counter tick always makes the
current IMGO slot immediately safe to read. It does not independently prove
the exact silicon address-latch timing or safety over arbitrary callback time.
No sensor register, clock, ISP setup, exposure or DMA rotation was changed.

## Controlled live result

An independently timed, reversible trial replaced only the idle Cronos camera
helper. GPU UI, overlays, installed encoder/decoder helpers, host relay and
firmware remained unchanged. The call negotiated `echo-h264-balanced`; the
operator reported **“backward jumps are gone.”** The trial was stopped and the
original installed helper restored normally. The operator initially also
reported normal motion after permanent installation, but then explicitly
reported the same older-position jumps on Checkers. Thus the stability result
does **not** establish the cause of the persistent temporal-order symptom.

The previous-slot-only hook was replaced by the held-target policy above.
Fingerprint instrumentation must not be included in the production helper.
Normal application OTA and the operator call/wake/privacy checks for the
held-target replacement were completed as recorded above.

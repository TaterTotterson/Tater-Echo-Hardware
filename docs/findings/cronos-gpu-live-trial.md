# Cronos reversible live camera GPU trial (2026-11-13)

## Pre-run authorization and safety review

After the generated-only prototype, the owner authorized a temporary Cronos
live-camera comparison, keeping Checkers unchanged. No firmware flash, app/root
slot change, recovery change, wake/audio update, or release is authorized here.

The dedicated ISP experiment stays disabled. The existing qualified camera
driver owns the sensor and its privacy handling; the trial changes only
640×480 balanced-profile Bayer color conversion inside the hardware encoder
input path. Full-resolution stills, JPEG and other profiles stay unchanged.

Original research-only adapters:

- `bayer10_stream_trial_android.c` reuses the byte-accurate ES3 integer shaders
  in a separate GPU process. It reads raw/LUT bytes from a private pipe and
  returns fixed-size I420, without opening a sensor, recording pixels or using
  a display surface. Its runtime library search path is isolated from OMX.
- `cronos_gpu_venc_trial_android.c` wraps, rather than edits, the qualified
  Cronos encoder source. Only its balanced CPU conversion call is redirected.
  Codec settings, capture PTS, input chroma swap and stream format are unchanged.
  A missing, invalid, symlinked, wrong-owner or non-private mode file selects
  CPU. GPU initialization/protocol/timeout errors latch CPU for that stream and
  reprocess the same frame. No speculative ISP retry or frame replay occurs.

Pipe headers have exact magic, payload sizes and monotonically increasing
sequence numbers. Both ends enforce bounded transfer deadlines and allocation
sizes. GPU conversion has a two-second alarm; GPU helper initialization has a
five-second alarm. A GPU helper ends after at most ten minutes/18,000 frames.
The parent kills/reaps only its own child on failure or CPU selection, and the
child has a parent-death kill signal. Other encoder device descriptors are
closed before entering the GPU runtime. A driver/kernel lockup cannot be made
safe merely by userspace timeouts, so stop on transport or device-health loss.

All test binaries, mode and numeric logs are under the uniquely created
`/tmp/tater-cronos-gpu-live-v1`, mode 0700. Local and transferred binaries are
hash-verified. Only numeric timing/count/CPU summaries are retained (capped
128 KiB), not scene or audio bytes. Generated-pattern proof may save generated
bytes; no live scene files are saved.

Before replacing a service, prove the pipe on generated patterns, then use a
separate loopback camera listener for bounded in-memory CPU/GPU comparison.
Require no active call or sensor owner first. If that succeeds, stop only the
idle slotted camera child, not the daemon, UI, audio, or rootfs camera. Start
the privately built test camera on the same loopback address, using a pinned
test helper hash; do not weaken production manifests or hash checking. No
application binary is overwritten or bind-mounted. Checkers stays untouched.

The test camera must have a separately started, time-bounded rollback
controller, able to stop only the trial camera and relaunch the original
hash-verified camera at its original address. Reboot independently restores
the installed camera because no startup configuration changes. Mode initially
selects CPU; enable GPU only for the comparison. Report any failure or fallback
honestly and restore the original service if the trial is worse.

## Results

The generated private-pipe proof matched all eight CPU-reference I420 frames
exactly. The owner's BusyBox wget truncated binary POST data at its first NUL;
the completed generated output was re-uploaded as base64 and hash-verified,
without replaying GPU work. The first overlong interactive console command
was rejected/truncated by the shell before execution; it was cancelled and
staging split into short, separately captured commands. Device health remained
normal. Camera-helper Linux race tests and seven host GPU/protocol tests passed.

Initial live checks correctly refused camera capture while the physical privacy
latch was muted. The owner physically unmuted it; no privacy override was made.
Twenty-second, in-memory capture/encoding comparisons then produced 295 frames
in each mode (14.75 FPS including startup), with no GPU fallback. CPU development
averaged roughly 29–30 ms and 51 ms aggregate process CPU per frame; GPU plus
IPC averaged roughly 43 ms, with about 3 ms encoder CPU plus 4.6 ms GPU-process
CPU. The GPU initialization caused one roughly 334 ms first-frame operation.
The separate full-decode probe was not qualified in this trial; its earlier
checks ended with preflight/capture refusals while privacy was on. Do not count
those failed checks as successful video-playback tests.

For the real call, only the idle slotted Cronos camera child was replaced by
a separate temporary executable on the existing loopback address. The original
binary was never overwritten or mounted over. A private controller provided
CPU default selection, a stop signal and 30-minute original-service rollback.
Daemon, UI, microphone/audio services and Checkers were not replaced.

The backend confirmed the Cronos→Checkers direction used `echo-h264-balanced`.
CPU source reports were approximately 29.8–29.9 FPS, encoded 15 FPS, tone/raw
preparation 11 ms and source gaps averaging 67 ms (about 98–107 ms maxima in
the sampled stable windows). After a failed mode-selection precondition was
corrected, GPU conversion was positively confirmed at 23:55:51 UTC: the same
encoder process reported `mode=gpu`, a separate GPU process was alive, and
`fallback=0`. Stable GPU conversion plus IPC averaged about 42–43 ms, versus
30 ms CPU development. Encoded cadence remained around 15 FPS. Startup/reset
windows had larger gaps and must not be described as steady-state timings.

The owner explicitly reported that the same repeated-frame stutter remained
on **Checkers' display**, which receives Cronos' camera. It was unchanged with
verified GPU conversion. Offloading Bayer development did not fix the observed
problem; this does not establish that all remaining jitter is in a particular
decoder, relay or display stage.

After the owner ended the call, CPU mode was restored and the controller
stopped. The original hash-verified Cronos camera executable was relaunched
and its health and H.264 capability checks passed. No GPU-trial/encoder process
or sensor owner remained; daemon/UI PIDs were unchanged. No flash, startup
configuration, saved setting, source version, commit, push or tag occurred.

**Outcome:** keep GPU Bayer conversion experimental. The owner's next proposed
direction is the original pre-hardware-video camera/Sat Chat pipeline, not
merely CPU Bayer conversion within the newer H.264 pipeline. That broader media
rollback has **not** been implemented by this trial. Keep it separate from the
new GPU UI/menu work and verify the older media baseline before release.

Generated-only timings and shaders are in
[the prototype findings](cronos-gpu-bayer-conversion.md).

# Tater VDEC integration plan

Date: 2026-11-11

The connected-device work proves a narrow hardware H.264 picture decoder. It
does not justify replacing Tater's working media player. The active Tater
player already owns URL handling, audio, pause, seek, loop, cancellation, a
latest-frame queue, and `PresentTransientRegions`. Production integration
should replace only H.264 picture decoding when a strict preflight succeeds.

```text
network URL
  -> unprivileged pinned FFmpeg
       -> PCM -> existing Tater audio path and clock
       -> copied Annex-B H.264 with access-unit delimiters
       -> copy-only framecrc packet-size/source-PTS side channel
            -> bounded Go packet reader
            -> little-endian length + complete access unit
            -> tater-vdec helper (no URL or network access)
            -> fixed-size RGBA pictures
            -> existing latest-frame queue
            -> PresentTransientRegions
```

## Process boundary

`tater-vdec` receives only a component path, width, height, frame rate, and
file descriptors. It must not receive a URL, credentials, or general network
access. Protocol v2 input is a sequence of little-endian `uint32 size`, `int64
pts_us`, and complete Annex-B access unit. Zero size ends the stream; one
access unit is limited to 4 MiB. Output starts with a `TVO2` header confirming
the negotiated visible geometry, RGBA payload bytes, pixel format, and nominal
frame rate. Each RGBA picture has its own payload length, flags, and
microsecond PTS; a zero-size record with the EOS flag completes the stream.
Diagnostics use standard error. The exact byte layout is documented beside
the adapter.

The Go side now has the bounded protocol-v2 reader/writer, incremental AUD
packetizer, SPS geometry parser, and process orchestration. It writes access
units and reads pictures concurrently, kills both process groups on
cancellation, and treats a short record, oversized access unit, short frame,
helper exit, or stalled startup as a hardware-path failure. Audio is held in a
1 MiB startup gate until the first validated hardware picture. A failure before
that picture discards the held PCM and restarts through the existing software
decoder; a failure after presentation begins is reported instead of replaying
the timeline from zero.

## FFmpeg requirement

The prior pinned Tater FFmpeg exposed raw-video output but not the H.264 muxer
and bitstream filters needed for a copied elementary stream. The source build
now adds only the H.264 and `framecrc` muxers plus `h264_mp4toannexb` and
`h264_metadata`;
the player will normalize with `h264_metadata=aud=remove` followed by
`h264_metadata=aud=insert`. This avoids duplicate delimiters when an input
already contains AUDs while still supplying them when it does not. A second
copy-only `framecrc` output supplies the exact packet byte count and source
PTS; the Go side drains raw H.264 concurrently, reads exactly that count, and
preserves non-monotonic decode-order B-frame PTS until the helper returns
presentation order. The updated artifact has been rebuilt from the
checksum-pinned source and remains
unprivileged under the existing `techo5-video` account. The Go packetizer is
incremental; the host
`tater-h264-packetize` tool is a deterministic fixture helper, not a
production whole-file buffer.

## Privilege and runtime

On the current image `/dev/Vcodec` and `/dev/ion` are root-owned mode `0660`.
The first bounded integration can run only the narrow decoder helper with the
required device access while FFmpeg and URL handling remain unprivileged. A
dedicated video group and explicit device-node ownership is preferable before
release. The proprietary component, OMX, gralloc, and compatibility libraries
remain owner-local and must be staged by an allowlist of exact hashes; none may
be committed to this repository or copied into a public release artifact.
The open helper is linked for `/data/tater-vdec/linker`, exports the bounded
headless compatibility symbols needed by the stock component, and accepts the
allowlisted gralloc path through `TATER_VDEC_GRALLOC`; it no longer embeds the
research-only `/tmp/omx-runtime` location.

## Selection and fallback

Select hardware decode only when all of these are true:

- the device is the identified MT8163 Checkers target;
- the video codec is H.264 and visible dimensions do not exceed 1920x1088;
- the complete owner-local runtime passes dependency and hash preflight;
- the updated pinned FFmpeg reports the required muxer and bitstream filters;
- the helper capability check and a bounded startup handshake succeed.

Every other codec, target, resolution, missing dependency, malformed stream,
or helper failure uses the existing software path. Software decode remains the
release-safe default until long-play validation is complete.

The Tater path remains disabled unless `TATER_VDEC_ENABLE=1`. A Checkers
rootfs wrapper sets that flag only when the separately installed owner-local
runtime manifest exists; the Go preflight still validates the helper's exact
protocol-v2 capability document, the two device nodes, the
FFmpeg muxer and bitstream filters, the first access unit's SPS geometry, and
the helper's negotiated output header. `TATER_VDEC_REQUIRE=1` is a diagnostic
mode that rejects fallback so a hardware test cannot silently pass in
software.

## Connected orchestration result

An operator-present Checkers run served a one-second 960x480 H.264/AAC MP4
over HTTP to the normal unprivileged FFmpeg boundary. The Go player normalized
30 access units, launched the narrow helper only after parsing a matching SPS,
and received 30 protocol-v2 RGBA pictures. The 55,296,000 pixel bytes had
SHA-256
`8a48b65ea4d5d01332b1a793ab92863c402ea628dccd73384ad497bee7dc913b`,
the same exact hash as the earlier direct hardware reference. The guarded
audio path delivered 96,256 PCM bytes with SHA-256
`ec19deb85f21f500a04f348210a5acceb897f2ffa20c5c419562a108a9f9b811`.

A second run deliberately requested 1280x720 for that 960x480 stream. SPS
validation rejected hardware before opening a picture and the software path
completed. Its 28 delivered frames and 96,256 PCM bytes matched a software-only
control at both frame and audio SHA-256, proving the failed attempt leaked no
pixel or audio data. Both runs returned Vcodec and ION to idle and did not stop
or replace the live renderer.

The final connected path then completed a 40-second, 1,200-picture 960x480
H.264/AAC soak. It validated and SHA-256 hashed 2,211,840,000 RGBA bytes and
3,840,000 PCM bytes, preserved the B-frame PTS sequence, and released Vcodec,
ION, and M4U after both success and a forced five-second cancellation. The
diagnostic took 114.721 seconds because it deliberately hashed every RGBA byte
on ARM; the product renderer does not do that work.

An end-to-end six-second run then used the real framebuffer renderer and the
real Checkers ALSA music plane. The renderer held 29.8–30.0 fps, polled a
loopback-only session/stream-qualified clock backed by PCM periods actually
removed by the ALSA consumer, and completed 141 periods / 288,768 frames with
zero underruns. The final frame remained visible through the queued PCM tail.
The first natural-EOS trial also found and fixed an orchestration bug: the
owner-local helper emits normal vendor diagnostics on stderr even on success,
so stderr is now retained only as a bounded failure detail when the helper
exits nonzero; protocol EOS plus exit zero is the success contract.

The same path was then built into a complete clean rootfs, installed into the
inactive Checkers system slot, and committed only after the new voice, display,
and camera services remained connected and healthy for five minutes. After the
commit reboot, a second six-second run set `TATER_VDEC_REQUIRE=1` and
`TATER_VDEC_TRACE=1`. The live process tree showed the committed
`/usr/local/bin/tater-show` feeding copied H.264 and source PTS through the
pinned FFmpeg into `/data/tater-vdec/tater-vdec`; the output trace restored
presentation order, ALSA consumed 288,768 frames over 141 periods with zero
underruns, and Vcodec/ION/M4U returned idle before normal services reconnected.
The image and slot evidence are recorded in
[the ground-up firmware trial](findings/checkers-ground-up-firmware-trial.md).

## Validation still required

- publish and independently reproduce the rebuilt FFmpeg artifact;
- fuzz the implemented incremental Go access-unit parser and wire reader;
- test pause, resume, seek, loop, silent media, live media, and
  corrupt input;
- kill the helper at each lifecycle phase and confirm automatic recovery;
- extend the completed 40-second soak into long-duration thermal, memory,
  latency, and dropped-frame qualification;
- repeat renderer, audio, camera, Vcodec, ION, and M4U ownership checks for the
  remaining pause/seek/corrupt-input paths.

An open V4L2 or otherwise redistributable decoder remains the long-term goal.
The owner-local adapter is useful for development, but it is not a substitute
for that clean release boundary.

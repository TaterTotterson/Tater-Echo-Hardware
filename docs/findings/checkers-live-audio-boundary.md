# Checkers live audio boundary

Date: 2026-11-11

The connected Checkers unit already exposes its microphone array and
synchronized playback reference through standard ALSA. This finding separates
that open transport boundary from the processing choices above it. No PCM
device was opened for this work, no microphone sample was read, and no audio
was saved.

## Live metadata-only proof

`probes/mt8163-audio/status_probe.sh` is hard-gated to the Checkers kernel
project and reads only `/proc`, sysfs, and the live device tree. Against the
running Tater service it reported:

```json
{"pcm":"hw:0,22","state":"RUNNING","format":"S24_3LE","channels":4,"rate_hz":16000,"period_frames":512,"buffer_frames":2560,"owner":{"comm":"arecord","parent_comm":"tater-echo","pipe_connected":true},"two_second_delta":{"hardware_frames":32604,"owner_cpu_ticks":0,"parent_cpu_ticks":146},"device_tree_mic_downmix":false,"pcm_opened":false,"samples_read":false,"audio_saved":false}
```

A separate 10.03-second observation advanced the ALSA hardware pointer by
160,262 frames, or 15,978 frames per second, while the application pointer
kept pace and the stream remained `RUNNING`. The small interval and procfs
sampling explain the difference from exactly 16,000. The `arecord` transport
used about 1.1 MiB RSS; the complete 17-thread `tater-echo` process used about
36.7 MiB RSS. Its five-second aggregate CPU delta was 369 ticks at 100 Hz, or
about 74% of one core, but that number includes wake inference and every other
daemon responsibility and must not be attributed to capture alone.

## Hardware and kernel responsibilities

The public kernel describes the complete transport:

- a TLV320AIC3101 converts the two microphones;
- the Amazon/MediaTek audio FPGA combines those two microphone channels with
  left and right playback-loopback channels;
- `amzn-mt-spi-pcm` moves four packed 24-bit channels over SPI; and
- ALSA exposes that stream as capture device 22.

The stock Checkers device tree contains `amzn,mic-downmix`. When present, the
public driver averages channels 0 and 1 and writes the same result back to both
slots. The live Tater device tree omits that property, so the two microphone
channels remain distinct. Channels 2 and 3 are the sample-synchronized speaker
reference needed for echo cancellation. Per-unit `miccal` entries exist in
IDME, but their contents remain owner-local and were neither read nor copied.

## Software responsibilities

The open Tater path already assigns each higher-level operation in userspace:

- center or two-microphone averaging reduces the array to mono;
- analog gain is applied through public ALSA mixer controls;
- a built-in adaptive filter or optional WebRTC helper performs AEC from the
  two loopback channels;
- optional noise reduction and level control run after mixing/AEC;
- VAD and wake-word inference consume the same final mono stream; and
- the physical mute path remains independent of the software pipeline.

No private Amazon microphone service is required for full capture access.
Amazon's stock userspace contains fixed-beamformer, echo-cancellation, EQ, and
limiter configuration, but those owner-local files are research references,
not releasable dependencies. The correct next comparison is a repeatable,
operator-spoken test set scored through the current Tater frontend and any
candidate tuning; ambient recording should not be collected merely to prove
transport, because transport is already proven.

## Runtime headroom snapshot

During the metadata-only check the four CPU cores reported 1.3 GHz. The CPU
thermal zone was 51.6 °C, other reported sensors ranged from 43.0 to 51.6 °C,
and Linux reported roughly 672 MiB available from 990 MiB total with no swap.
These are an idle-service snapshot, not a stress limit. Decoder, display,
camera-pattern, and audio work all returned their temporary owners and clocks
before this snapshot was taken.

# Audio status probes

## Checkers capture status

`status_probe.sh` measures the already-running Tater capture path without
opening `/dev/snd`, consuming a sample, or changing ALSA state. It is hard-gated
to a live kernel whose project is `checkers` and requires the explicit
`--read-only-status` mode:

```sh
sh probes/mt8163-audio/status_probe.sh \
  --target checkers --read-only-status
```

The probe reads the public ALSA status and hardware-parameter files twice over
two seconds. It reports the stream shape, hardware-pointer movement, owner and
consumer relationship, CPU ticks, and whether the live device tree still asks
the kernel to downmix the two microphone channels. It reports
`pcm_opened:false`, `samples_read:false`, and `audio_saved:false` explicitly.

On the connected unit, Tater already owns `hw:0,22` through `arecord`, using
four-channel `S24_3LE` at 16 kHz. The recorder's stdout pipe terminates in
`tater-echo`, and the hardware pointer advanced at the expected 16,000 frames
per second without changing ALSA ownership. The live Tater device tree omits
`amzn,mic-downmix`, preserving the two microphone channels instead of replacing
both with the same kernel average. Channels 2 and 3 remain the playback
loopback reference supplied by the audio FPGA.

This proves that an open replacement does not need a private microphone API:
standard ALSA already exposes the raw microphones and synchronized echo
reference. Beamforming/mixing, AEC, noise suppression, gain leveling, VAD, and
wake inference belong above that interface. Per-unit calibration exists and
must be treated as owner-local device data, not copied into this repository.

## Biscuit mixer inventory

`biscuit_mixer_probe.c` inventories selected ALSA mixer controls without opening
a PCM stream, recording audio, or writing a control. It requires hostname
`tater-biscuit`, kernel release `3.18.19-bt`, ALSA card ID `mtsndcard`, and the
explicit `--read-only-mixer` mode. A different target needs its own safety review
and qualification; do not weaken the guards to use this on another satellite.

Build in a Linux ARMv7 environment with Linux ALSA UAPI headers:

```sh
cc -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -static \
  biscuit_mixer_probe.c -o biscuit_mixer_probe
./biscuit_mixer_probe --self-test
```

After owner approval, copy the executable to a private temporary directory on
the qualified device, verify its SHA-256, and run:

```sh
./biscuit_mixer_probe --read-only-mixer
```

Only `/dev/snd/controlC0` is opened, with `O_RDONLY`. The ioctl allowlist is
`CARD_INFO`, `ELEM_LIST`, `ELEM_INFO`, and `ELEM_READ`. Values are read only for
named ADC gain/mute/input-route and playback scalar controls. Other DSP-like
controls, if present, are described without reading their values. No raw
coefficients, calibration, microphone samples, or device identifiers are saved.
The JSON inventory is bounded and reports any control-read failures. Delete the
temporary executable and its empty directory after the session.

### Observed baseline, 2026-11-14

The owner's Biscuit was running Tater Linux v4.2.2 on `3.18.19-bt`.
The strict static ARM build and allowlist self-test passed. The live probe
inventoried 241 controls, read 48 selected controls, and reported zero errors.
Capture stayed owned by the same recorder on `hw:0,24` (nine channels,
`S24_3LE`, 16 kHz); playback stayed owned by Tater on `hw:0,23` (stereo,
`S16_LE`, 48 kHz). Capture telemetry reported no stalls or subscriber drops
through approximately 16 minutes of uptime. Available RAM was about 436 MiB,
with Tater resident memory about 30 MiB. This was an idle baseline, not a
playback/barge-in or audio-quality qualification.

All four ADCs exposed the expected differential input routes and unmuted
channels. Their reported microphone PGA setting was zero, while Tater's log
reported its separate 40 dB software gain. This is a gain-staging investigation
lead, **not** evidence that an analog-gain increase is safe or necessary. The
related 3.18 driver returns cached register values, so mixer reads alone do not
prove the physical gain or DSP state. No dedicated DSP/processing selectors
were exposed by this inventory. Neither observation proves unused hardware
beamforming/AEC is available. No gain, filter, audio route, firmware, or CPU
policy was changed.

The older automatic USB Ethernet probe was temporarily paused with explicit
owner approval to keep the console stable, then resumed; the original capture
and playback owners were still running. The temporary helper was removed.
The pending connection-state guard is a separate firmware update: this device
reported vendor gadget state `CONFIGURED` even though its generic UDC state
said `not attached`. A positive vendor state must therefore protect the USB
console regardless of that generic negative reading.

## Owner-approved Biscuit gain-staging trial

`biscuit_gain_trial.c` is a separate **mutating experiment**, not part of the
read-only mixer probe or production firmware. Do not run it without explicit
owner approval and a target-specific safety review. Build it with the same
strict Linux ARMv7 command as the mixer probe, substituting its source and
executable names. `--self-test` uses a fake mixer only; it tests the signed
volume mapping, apply/restore ordering, partial writes, rollback retries, and
cancellation without opening hardware.

On the qualified Biscuit, `--owner-approved-10db-trial` adds 10 dB of analog
PGA gain and subtracts 10 dB with the ADC's own digital volume control. Using
on-chip compensation avoids changing Tater's software gain, gain-normalized
VAD/DOA thresholds, saved settings, or the running voice service. This is a
nominally level-matched experiment, not a demonstrated noise-floor improvement.
It does not enable AGC, change filters, open a PCM, or save audio.

The tool only accepts the exact eight expected stereo controls with the
observed PGA baseline `0,0` and digital baseline `88,88`. A notable vendor
driver inconsistency is pinned explicitly: digital control metadata advertises
maximum 64, despite reading the current unity-gain value as 88. In the related
3.18 source, `snd_soc_info_volsw` reports `max-min = 104-40`, while
`snd_soc_get/put_volsw_sx` maps `(value+40)&127`; 88 means register zero,
and 68 means signed -20 half-dB steps (-10 dB). TI's SLAS553B documents the
ADC digital volume and PGA encodings. Do not use the driver's negative-step
TLV annotation to derive the trial values. The tool first requires successful
no-op writes and readbacks of **every original value**, proving that the live
restore API accepts this precise baseline before any gain change is made.
This exception must not become a generic bypass for other controls or boards.

All digital attenuation is applied before any preamp increase. On the
five-minute deadline, TERM/INT/HUP, error, or unexpected readback, all preamps
are restored before digital attenuation is removed. Restore retries continue
across every control and verify both channels. An exclusive `/run` lock
prevents overlapping trials and remains on restore failure as a visible guard.
No process can guarantee rollback after SIGKILL, a kernel/process crash, or
power loss; abnormal termination requires explicit baseline verification and
restoration. Keep a recovery path available and never kill the test with
SIGKILL. Remove temporary executables/logs after verifying restoration.

Qualification requires original-vs-trial wake/first-word checks, quiet/far
speech, reply interruption, clipping and capture-drop telemetry, followed by
the same checks after restoration. A functional pass or mixer readback alone
does not justify changing production defaults.

The 2026-11-14 owner-approved trial passed its ARM self-tests, nine safety
contract tests, live original-value write/readback preflight, trial readback,
and independent restoration checks. The same capture/playback processes stayed
running; capture reported zero stalls and subscriber drops. The owner initially
reported better quiet/far pickup, but repeating that check after restoring the
original settings felt about the same. This does **not** establish a repeatable
improvement, so the original production gain and filters were retained. Reply
interruption was not qualified in this comparison because that test's wording
was unclear to the owner. No firmware or saved audio configuration was changed.

### Full hardware gain follow-up

The owner subsequently requested an all-hardware microphone-gain comparison.
`--owner-approved-hardware40-trial` uses PGA `80,80` (+40 dB), leaving ADC
digital volume at `88,88` (unity). This mode refuses to run unless a live
temporary `/tmp/tater-biscuit-hw40.*/tater-echo-hw40-trial` process exposes
`MIC_GAIN_DB=0` and the explicit trial marker. Do not run this mode against an
ordinary daemon: +40 dB hardware combined with +40 dB software would be unsafe
gain staging.

The target-specific `biscuit_hardware40_runner.sh` pauses the original voice
supervisor, stops its daemon, starts the temporary zero-software-gain build,
and only then arms hardware gain. It verifies the exact original binary and
session PIDs before proceeding; it is an audited single-bench session tool,
not a general installer. On cleanup, verified analog restoration precedes
resuming the original software-gain daemon. Normal cleanup also resumes the
temporarily paused Ethernet supervisor. Never substitute a new device's PIDs
or executable without a fresh baseline and safety review.

The temporary daemon was built from pinned `v4.2.3` source with separate
audio gain (unity) and total-gain threshold calibration (100). The latter
scales only speech/activity thresholds, not audio samples. Synthetic ARM
tests verified equivalent PCM, all six wake beams, unchanged echo reference,
and matching VAD/DOA decisions. This is release-source provenance, not proof
that every build setting matches the installed binary.

In the short live trial, the owner reported better normal/quiet/far pickup;
first words, wake during a spoken reply, and closer speech all passed. Capture
reported zero stalls/drops, and the hardware echo reference remained active.
Original gains and services were restored, and the owner confirmed normal
operation afterward. This is a promising Biscuit-only candidate, not a measured
SNR improvement or completed ADC-headroom qualification. Longer loud-playback
and raw-rail/headroom checks remain before changing release defaults. No
permanent firmware/configuration change or microphone recording was made.

## Independent ADC3101 high-pass experiment

`adc3101_filter_lab.py` is an offline source audit and filter-design tool. It
accepts only the pinned public Biscuit/Radar codec C/header hashes, summarizes
the normal processing topology without emitting vendor tuning coefficients,
and designs Tater's own Q1.15 first-order 80 Hz profile. The coefficient model
uses [TI's ADC3101 datasheet](https://www.ti.com/lit/ds/symlink/tlv320adc3101.pdf),
not decompiled firmware. The quantized response differs from the existing
software one-pole by less than 0.004 dB over 1 Hz–Nyquist. That is a math check,
not a live SNR, latency or AEC improvement claim.

The exact public C/header hashes are respectively
`4d05170eb53df0a2b5a4c98b791ba2a18e667e38d2a18f3b1cda36e5bafde1b6`
and `6c5e57d6132f1de8619a4afa51bc96f55e9bd45b4058abcffc2abb3bc178ebb2`.
Both boards' files match. Their normal PRB_R2 tuning already uses a vendor
biquad; the first-order stage is the independent experiment's only addition.
The on-chip programmable DSP is per stereo ADC, not a verified whole-array
AEC/beamforming engine or wake-inference accelerator.

`prepare_adc3101_hpf_trial.py --target biscuit --source-root PRIVATE_GPL_ROOT
--output NEW_PRIVATE_DIRECTORY --prepare-offline-trial` creates a **default-off**
source overlay and patch. It refuses changed source or an existing output
directory. It does not contact hardware, build, install, or flash anything.
Keep GPL inputs and generated boot images in ignored private storage.

The independent `tater_adc3101_hpf_trial.h` runs only for Biscuit/Radar at
16 kHz, with diagnostic mode and ADC AGC off and all ADC clocks stopped. It
holds the existing codec mutex, snapshots the six first-order bytes in all
eight slots, writes only Tater's fixed profile, physically reads every slot
back, restores page selection, and publishes `[1,80,255,16000]` only on full
success. Partial writes trigger restoration; any failure prevents capture.
The default-off path adds no I2C transfers. The explicit boot-only switch is
`snd_soc_tlv320aic3101.tater_hpf80=1` (runtime permission 0444).

`adc3101_hpf_selftest.c` compiles the exact same helper against a fake I2C
backend. It tests default-off, target/rate/AGC/IDC/stopped-clock guards, stereo
slots, partial writes, readback failures and rollback/page restoration errors.
No self-test opens hardware. The mixer inventory's additional allowlist entries
read only the scalar `IDC Enable` and four-integer `Tater ADC HPF Status`.

On the owner-approved Biscuit, the default-off kernel built with original
AOSP GCC 4.8, preserved the original Linux ramdisk/header, and passed the owner's
immediate first-word/reply check. The enabled boot reported the exact verified
eight-slot status and healthy nine-channel capture; IDC was off and original
gain staging was unchanged. A pinned v4.2.3 filter-only application is used for
listening qualification, keeping this independent of the pending gain changes.
The original boot image is restored on disk before that temporary app starts,
so its bounded cleanup or a power-cycle returns to the original kernel. This
is not a Radar pass or a production kernel default. No proprietary image,
calibration or audio recording belongs in this repository.

## Production-source gain follow-up

After the trial was restored, the owner approved making hardware-only gain
permanent and extending it to Radar if the capture hardware matched. The
firmware repository's pending implementation now pairs the four +40 dB PGAs
with unity ADC digital/software gain on Biscuit and Radar. Their documented
four-TLV320ADC3101 capture topology matches; Radar's distinct speaker circuitry
is not changed. Startup verifies every stereo gain control before opening
capture and attempts rollback of partial writes. DOA/VAD keep the original
total-gain calibration; legacy software-gain settings cannot double the boost.

That production-source work does not install the firmware or expand the
historical trial results: Radar still needs a physical listening/privacy test,
and extended clipping/loud-playback and final OTA checks remain pending on both
targets. A manually started older software-gain binary requires a reboot first
to clear inherited PGA state. No new hardware DSP/filter/AGC offload is claimed.

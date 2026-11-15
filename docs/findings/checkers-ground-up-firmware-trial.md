# Checkers ground-up Tater firmware trial

Date: 2026-11-11

This trial moved the Checkers work from individual hardware probes to one
complete Tater Linux image running from a recoverable system slot. The public
image contains the independently built Tater services, the pinned FFmpeg
features, the verified Checkers kernel identity, and the open camera/display/
audio integration. It does not contain the owner-local MediaTek decoder, OMX,
gralloc, wireless firmware, calibration, keys, or device backups.

## Reproducible image

The checked build produced:

- release identity `Tater Linux Checkers v4.2.3-hardware-dev.1`
  (`TECHO5 platform v0.9.26`);
- rootfs artifact SHA-256
  `5f7ca3e7f1a351d12c1d156d0f83b21ad638caa96dce08fb6a82c410ce3cb03a`;
- application OTA SHA-256
  `a393a597dbbfd364dd882fb9804c275f47599c7aeea46c3b51c3172fefec612f`;
- pinned FFmpeg SHA-256
  `0c499f6eebd836ba489dfd7f43491b2b1459eaedbfa692e3a955c2d3503b3992`.

The extracted rootfs contained 1,914 files and no `tater-vdec`, private OMX,
private gralloc, or transplanted Android compatibility library. Its Checkers
service wrapper enables the separately installed owner-local decoder only when
its manifest exists; the Go preflight still verifies the complete allowlisted
runtime identity before opening Vcodec.

## Recoverable slot trial

The image was installed with `slotctl install` into inactive slot B while slot
A remained a known-good v4.0.2 fallback. A persistent v4.0.2 application-slot
override was preserved under a backup name rather than deleted, so the trial
actually exercised the new system binaries without losing the prior app.

The first B boot reported the expected kernel release
`4.9.337-g8d928c5176cc`, ran `tater-echo`, `tater-show`, and `tater-camera`
directly from `/usr/local/bin`, and exported both
`TATER_FIRMWARE_TARGET=checkers` and `TATER_VDEC_ENABLE=1` only to the display
service. The slot watcher required all three services plus a real authenticated
Tater connection for five continuous minutes. It then committed B and used the
normal direct reboot path. After the commit reboot:

- active and booted slot: B, `good`;
- rollback slot: A, still `good` at v4.0.2;
- `/run/tater-connected`: present with the new firmware identity;
- all three service executables: the new `/usr/local/bin` binaries.

No health marker was fabricated and no rollback try was bypassed.

## Strict hardware playback

A post-commit six-second 960x480/30 fps H.264/AAC fixture was played through
the real framebuffer and ALSA path. The diagnostic set
`TATER_VDEC_REQUIRE=1`, so software fallback could not make the run pass, and
enabled bounded packet/frame tracing. During playback the process tree showed:

```text
tater-show
  tater-ffmpeg   (copied Annex-B H.264 + framecrc PTS + PCM)
  tater-vdec     (--stream-h264-rgba-v2, 960x480/30)
```

The trace recorded decode-order B-frame input PTS and monotonic presentation-
order output PTS. ALSA consumed 288,768 frames over 141 periods with zero
underruns; the receive span was 5.198 seconds and the largest delivery gap was
47 ms. The final frame remained displayed through the queued audio tail.
Vcodec, ION, and M4U had no test owner after completion, and normal display,
voice, and camera services resumed and reconnected.

The temporary exclusive-hardware window used a bind-mounted maintenance
`inittab` with only the Tater voice/display respawn entries omitted. An
independent watchdog restored the original table. The rootfs file itself was
never changed.

## Remaining hardware checks

The committed image also completed these live checks:

- the camera endpoint returned HTTP 200 and a valid 17,059-byte JPEG, then the
  sensor logged a normal idle stop; no captured pixels are kept in this repo;
- the Checkers microphone clock advanced 60.2 seconds of audio over 60.0
  seconds of wall time with zero stalls and zero subscriber drops;
- the verified built-in Hey Tater MWW/OWW/NWW/boundary package installed and
  the native MWW engine became active;
- the framebuffer renderer returned to 30 fps and reopened Goodix touch and
  the ambient-light input;
- Wi-Fi associated at 433 Mb/s, BLE passive scanning resumed, and the native
  satellite authenticated to Tater;
- approximately 727 MiB remained available, `/data` had about 3.2 GiB free,
  and the highest readable thermal-zone sample was below 48 C after the test.

The first development image was intentionally unpublished, so its optional
release-runtime download returned 404 and that original trial used the built-in
MWW path. The later integrated image packages the reviewed ONNX runtime and
companion assets in the rootfs, refreshes stale image-supplied files in
persistent `/data` at boot, and became active as `mww_nww` without a release
download. User-installed packages keep distinct names and are not overwritten.

## Integrated GPU, wake, and live-video follow-up

The follow-up image `v4.2.3-hardware-dev.3`, SHA-256
`94c56a6876c873eabc3451d60fd988f1c4a5660a01a38b2562d1a6aa742c96a5`,
was built after the shared hardware-aware camera path, dual-wake failure guard,
and hybrid GPU compositor were integrated. Its public rootfs still contains no
private Mali or VDEC userspace. The display wrapper enables each separately
installed owner-local runtime only on Checkers, and both Go preflights require
the exact reviewed manifest and critical file hashes.

The image was installed into inactive slot A while the earlier hardware image
remained good in slot B. The ordinary authenticated five-minute health gate
committed A and performed its direct reboot. After that reboot both slots were
`good`, A remained active, and the live process tree contained the new
`tater-echo`, `tater-show`, `tater-camera`, and the bounded
`tater-gpu-rotate` child. The display log reported the GPU compositor active,
Goodix touch and ALS reopened, the satellite authenticated to Tater, microphone
capture started, and MWW+NWW reached its continuous dual-wake-ready state.

A post-install hardware-only media probe decoded the exact 30-picture fixture
to 55,296,000 RGBA bytes with the established SHA-256
`8a48b65ea4d5d01332b1a793ab92863c402ea628dccd73384ad497bee7dc913b`.
It also produced the expected 96,256 decoded PCM bytes, and software fallback
was disabled. Camera/ISP service ownership and listening state were verified;
no unattended ambient camera capture was requested during this follow-up.

## Boundary of the result

This is a fully booting and recoverable Tater firmware image on the connected
hardware, with working voice transport, display, touch, audio, camera,
wireless, and strict hardware H.264 playback. It is not yet a completely open
firmware distribution: MT8163 VDEC/GPU userspace and MT7668 wireless still
cross owner-local or vendor-firmware boundaries documented in the hardware
matrix. The public rootfs remains clean, and removing those final boundaries
is separate portability work rather than an unrecorded dependency of this
trial.

# Checkers hardware-access matrix

Updated: 2026-11-12

"Hardware access" has two different meanings in this project:

1. an independent Tater process can use the hardware on an owner-operated
   device; and
2. a redistributable, open Tater image can provide that access without private
   vendor userspace.

Checkers has reached the first definition for the main display, audio, GPU,
video-decoder, and privacy-safe camera paths. The second definition is not
complete for GPU, VDEC, or wireless. Keeping those claims separate avoids
mistaking a successful lab transplant for a shippable firmware stack.

| Block | Independent live proof | Current boundary | Open Tater release work |
| --- | --- | --- | --- |
| Display/panel | Tater owns `fb0`, rotates 960x480 content, and flips the native three-page 480x960 framebuffer; ten distinct hardware-decoded pictures completed a bounded 39-frame moving transient presentation | Public MT8163 fb/display kernel driver | Already usable; keep one renderer owner and use `PresentTransientRegions` for video |
| Mali-T720 GPU | EGL pbuffer render and exact 460,800-pixel Checkers rotation/swizzle, with GPU/job IRQ movement; Tater's live hybrid compositor reduced a continuously changing full-frame conversion from 25 ms on CPU to 17 ms on Mali while Go remained the sole framebuffer owner | Open bounded `tater-gpu-rotate` helper plus public kbase kernel source and an exact owner-local r26p0 DDK/Android 9 VNDK runtime; the public rootfs carries no private runtime and selects it only after manifest and critical-file hash preflight | The current hybrid is usable on owner-operated Checkers and retains automatic CPU fallback; upstream Panfrost supports generic T720, but MT8163 still needs a board/SoC DRM port and Mesa before this stack is fully open or redistributable |
| VDEC | Normal IDR/P/B batches match software output byte-for-byte at 960x480, 1280x720, and 1920x1080; a 1,200-picture HTTP H.264/AAC soak completed through Tater; the real renderer held 29.8–30.0 fps with source PTS, ALSA-clock synchronization, clean EOS, and zero audio underruns; and both the committed fullscreen path and the shared live-camera reader select `tater-vdec` for compatible H.264 | Clean bounded `tater-vdec` adapter over the public `/dev/Vcodec` kernel ABI plus owner-local ARM32 decoder/OMX/gralloc libraries and a headless GUI-extension shim; the public rootfs carries no private runtime and selects it only after exact manifest/hash preflight | Keep software fallback and finish pause/seek/loop/corrupt-input qualification; then replace the private component with an open adapter or V4L2 bridge |
| JPEG | Standalone decoder inference was disproved live | Hardware encoder exists; no decoder at the inferred window | Use VDEC/CPU paths; never revive the rejected decoder patch |
| Camera / VENC | OV9734 internal color bars traversed the sensor/ISP path; the corrected kernel boots with a 24 MHz sensor clock; native 720p hardware encoding plus local hardware decode sustained 28.4 fps for five minutes with no saved scene pixels | Public sensor/ISP kernel source, open camera/encoder adapters, software Bayer/tone preparation, and an exact owner-local H.264 encoder component | Finish final-candidate physical privacy tests, extended thermal and two-device calls, then qualify kernel migration and runtime provisioning; consider V4L2 for portability |
| Microphone capture | Metadata-only live proof follows `arecord` on `hw:0,22` into `tater-echo`; the four-channel 24-bit 16 kHz pointer remains continuous and the live DT preserves two distinct mics plus stereo loopback | Public ASoC, TLV320AIC3101, SPI-audio FPGA, and ALSA; mixing, AEC, denoise, VAD, and wake inference are open Tater userspace; per-unit calibration remains owner-local | Standard ALSA access is already releasable; compare frontend changes only with repeatable operator-spoken tests before retuning |
| Speaker/playback | Existing Tater playback through the RT5616 route | Public ASoC and codec paths | Already usable; retain pop-safe mixer and power sequencing |
| Touch/keys/privacy | Goodix touch, GPIO keys, accessory detect, ALS/proximity, and lens-cover switch are exposed | Standard Linux input plus MediaTek sensor devices | Build small open adapters around standard input/sysfs interfaces |
| Thermal/backlight | Ten thermal zones and LCD backlight class interface are live | Public thermal and LED-class drivers | Already readable/controllable; preserve thermal policy and safe brightness limits |
| Wi-Fi/Bluetooth | Current vendor modules run and their complete imported-symbol CRC sets match the rebuilt kernel | MT7668 vendor modules/firmware | Keep the exact ABI or replace with supported upstream drivers/firmware where licensing permits |
| Recovery/boot | Retained TWRP works; the corrected kernel is installed. RC.6 passed the real connected trial in A, retired the old cached redirect, stayed in native TWRP through 113 seconds, and returned to good A with GPU UI and microphone capture; good RC.5 remains in B | MediaTek boot chain and owner-local recovery assets; TWRP intentionally redirects its recovery link to swdl. Volume Up keeps native TWRP; Tater Recovery remains explicit USB repair. The old hardware-dev.3 rootfs is archived privately | Preserve the fallback; make helper/manifest versions transactional with rollback, qualify failed-trial recovery and the installer/kernel migration path |

The product media paths now have explicit boundaries. Fullscreen compatible
H.264 and Home Assistant/integration-camera H.264 use the hardware decoder;
MJPEG and unsupported codecs use the bounded software path. The unpublished
Sat Chat update negotiates hardware H.264 only between two qualified Checkers
peers and updated hosts. Mixed devices, older hosts, and unqualified runtimes
keep JPEG. The hardware H.264 encoder and optimized decoder have passed a
five-minute local relay test, but a real two-device call and final installer
path remain release gates. Bayer development, JPEG encode/decode, and scale
remain CPU work; Checkers has no usable standalone JPEG decoder. The exact
boundaries and measurements are in
[the native camera findings](findings/checkers-native-camera-h264.md).

Later qualification exposed an intermittent startup/whole-unit hang; it must
not be hidden by the earlier successful soak. Owned Sat Chat snapshots, a
one-GOP startup queue, and graceful codec cancellation now have regression
coverage. After operator power-cycle recovery, the corrected wrappers passed
three short concurrent starts and a new five-minute 28.80 fps relay/decode run,
then deliberate-loss recovery. The exact RC.5 rootfs was reproduced twice,
booted with the qualified open helper versions, and passed the automatic
connected health commit. The operator confirmed wake/first words, animated
text, touch/menu/volume, mute/unmute, spoken-response interruption, and the
physical camera cover. An installed-service 20-second camera/relay/decode
check also passed without helper overrides. The earlier whole-unit failure's
precise cause is still unconfirmed; real two-peer calls, extended final-system
A/V, version-compatible runtime rollback and distribution remain release
gates. Physical recovery routing and manual good-A-to-good-B return have now
passed; automatic failed-trial rollback was not fault-injected. Only one
Checkers is available for the current qualification.

The operator subsequently chose native TWRP for the screen-device Volume Up
default. RC.6 implements that policy and passed its actual connected health
commit and normal reboot in A. The cached Tater redirect was removed without
touching other recovery commands or flashing boot/recovery. A native USB
recovery request stayed in TWRP through 113 seconds with a stable boot ID,
then normal boot restored A's GPU UI, camera and microphone capture. The new
stay/return test used USB entry, not a fresh physical Volume Up press; the
unchanged bootloader's physical TWRP entry was already observed. Biscuit and
Radar recovery work is deferred. This does not close the other release gates.

## Practical architecture

The shortest safe route to a substantially better Tater firmware is layered:

```text
Tater UI / voice / media services
        |
clean Tater hardware adapters
        |
Linux standard APIs where available
(ALSA, input, fbdev/DRM, V4L2, thermal, LED)
        |
small target-specific bridges for legacy MT8163 private ABIs
        |
public Checkers kernel + retained recovery
```

This lets Tater improve the whole experience now without requiring every
legacy MediaTek block to be replaced at once. Private owner-local libraries
remain research inputs only; they are not committed or treated as releasable
dependencies.

The Checkers renderer now follows that split literally. Small dirty rectangles
remain on the cache-friendly CPU framebuffer writer. Frames covering at least
half the display are uploaded to a long-lived off-screen Mali compositor,
rotated and channel-swizzled there, read into a reusable normal-RAM staging
frame, and copied once into the next framebuffer page before the existing pan.
The helper cannot open the framebuffer or display manager. A malformed runtime,
protocol mismatch, startup error, or stream failure disables it and retries on
the established CPU path.

For the longer-term open stack, Linux maintains the Panfrost DRM kernel driver
and its public UAPI under `drivers/gpu/drm/panfrost`, while standard hardware
codecs use V4L2 memory-to-memory devices. Those are the right public API shapes
for a future kernel port, but the current MT8163 vendor kernel does not expose
either one for these blocks.

The exact upstream gap and reproducible audit are recorded in
[the Checkers upstream Linux gap](findings/checkers-upstream-gap.md).
The userspace source boundary and its separate reproducible audit are recorded
in
[MT8163 decoder source availability](findings/mt8163-vdec-source-availability.md).

- [Linux Panfrost driver documentation](https://docs.kernel.org/gpu/drivers.html)
- [Linux 4.9 V4L2 codec interface](https://www.kernel.org/doc/html/v4.9/media/uapi/v4l/dev-codec.html)

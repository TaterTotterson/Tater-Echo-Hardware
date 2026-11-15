# Checkers camera inventory

This probe extracts the camera contract from the public MT8163 kernel source
and an exact Checkers kernel configuration. It does not open a device node,
power a sensor, read a pixel, or access a microphone.

The Checkers configuration selects only `ov9734_mipi_raw`. The corresponding
public driver describes a 1280x720, 30 fps, one-lane MIPI RAW sensor. Checkers
does not expose a V4L2 `/dev/video*` capture node: the kernel presents the
sensor and ISP through MediaTek private character devices.

Run it against the public kernel commit and a captured Checkers configuration:

```sh
python3 probes/mt8163-camera/checkers_inventory.py \
  /path/to/android_kernel_amazon_mt8163 /path/to/checkers.config
```

The JSON explicitly records `camera_powered:false` and
`capture_performed:false`. The later operator-present internal-pattern proof
described below is a separate, privacy-sensitive milestone and is not implied
by this inventory.

## Private ABI map

The public kernel source makes the bring-up sequence explicit even though it
does not expose V4L2:

- `KDIMGSENSORIOC_X_SET_DRIVER` selects/scans the compiled sensor table;
- `KDIMGSENSORIOC_T_OPEN` applies the Checkers-specific power/reset sequence
  and initializes the OV9734;
- `KDIMGSENSORIOC_X_GETINFO2` and `...GETRESOLUTION2` return the sensor modes;
- `KDIMGSENSORIOC_X_FEATURECONCTROL` carries sensor feature operations,
  including the sensor-ID check and test-pattern selection;
- `KDIMGSENSORIOC_X_CONTROL` selects preview/capture/video scenarios; and
- `KDIMGSENSORIOC_T_CLOSE` performs the matching close and power-down.

Frame transport and processing are separate: `/dev/camera-isp`,
`/dev/camera-pipemgr`, `/dev/camera-sysram`, ION/M4U, and CMDQ form the ISP
path. A sensor-ID-only test would still alter camera power and reset state, so
it is intentionally not folded into this read-only inventory. The first active
camera milestone should require an operator, verify the lens-cover switch,
select the sensor's built-in test pattern before starting ISP output, and close
through every error path.

`lens_cover_query.c` supplies the non-mutating part of that gate. It searches
the input devices read-only, verifies the `SW_CAMERA_LENS_COVER` capability,
and reports its current kernel state. It never reads input events, opens a
camera node, or changes the switch. Build it with a static ARMv7 musl compiler:

```sh
make -C probes/mt8163-camera CC=arm-linux-musleabihf-gcc
```

The same build also produces `checkers-camera-abi-dump`. It compiles directly
against the exact public MT8163 camera headers and emits the ARM32 ioctl values,
sensor-feature IDs, and structure sizes needed by a future clean adapter. It
uses the same `kd_imgsensor_define.h` configuration switch as the running
kernel. The exact live config has `CONFIG_CHECKERS` unset, despite the product
name, so the default enum is live: register set/get are 3011/3012 and test
pattern is 3043. The nominal Checkers-only header is shifted by one after the
omitted `SENSOR_FEATURE_SET_DUAL_GAIN`; using it silently selects the wrong
operations. The executable contains no
`open(2)`, `ioctl(2)`, `mmap(2)`, or write path and therefore cannot power or
configure camera hardware. Keeping this as an executable ABI check catches
pointer-size and ioctl-size mistakes before an active, operator-present test.

`test_pattern_state.c` is the original, hardware-independent orchestration
layer for that future test. It requires both operator presence and an explicit
test-pattern-only gate, enables the sensor pattern before streaming, requests
only one frame, and unwinds stream, pipe, buffers, and sensor ownership in
reverse order on every failure. Its mock backend verifies both the success and
partial-initialization rollback sequences:

```sh
make -C probes/mt8163-camera host-check
```

## Operator-present internal-pattern proof

With the owner present, the active proof stopped only the two existing camera
helpers, verified the lens-cover input and idle camera/ISP nodes, selected the
OV9734 standard color bars, and read register `0x5080` back as `0x10` before
enabling ISP DMA. Eight bounded 640x360 frames were consumed in memory; only
hashes and eight vertical-band RGB means for the last four were reported. No
raw or rendered frame was saved. The four band profiles were stable while the
frame hashes changed slightly with the sensor/ISP path.

The proof then disabled the pattern, stopped the ISP, closed and powered down
the sensor, destroyed ION state, resumed the camera helpers, and verified that
`/dev/kd_camera_hw`, `/dev/camera-isp`, `/dev/ion`, and `/proc/m4u` were idle.
The kernel log independently showed `PowerON`, control, close, `PowerDown`, ION
destruction, and the final ISP clock disable. This establishes independent
camera transport without retaining any scene pixels. The hardware path came
from Tater's existing MIT-licensed camera implementation and is already
releasable; the uninstalled diagnostic only added a forced internal pattern and
no-save summaries.

## Cronos ISP experiment — not qualified

`isp_runtime_probe_android.c` checks the owner's retained ISP runtime and its
postprocess lifecycle. `isp_synthetic_probe_android.c` is a separate ARM32
generated-RAW10 experiment, not a production camera adapter. It never opens a
sensor or saves scene pixels. Neither file nor its tests imply working color
processing.

**Do not run hardware processing from this experiment.** The current job
triggered an IMGI DMA fault and later device lockups requiring a power cycle.
Processing is quarantined by default at compile time. External process
timeouts did not prevent the device-level failure. Configuration/lifecycle
results are not a reason to bypass that quarantine. See
[the investigation and safety review](../../docs/findings/cronos-isp-bringup.md).

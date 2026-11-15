# Checkers camera ABI boundary

Date: 2026-11-11

This record narrows the Checkers camera work from an Android camera-service
problem to a small, auditable ARM32 hardware-adapter boundary. The initial ABI
inventory opened no camera node and powered no sensor. A later bounded,
operator-present proof used only the OV9734 internal color bars and retained no
image or ambient audio. Private owner-local libraries and Ghidra projects
remain ignored.

## Exact public ARM32 contract

`probes/mt8163-camera/camera_abi_dump.c` was compiled as a static ARM32
executable against the exact public Checkers/MT8163 kernel headers and run on
the device. It reported a four-byte pointer and these structure sizes:

| Structure | Bytes |
| --- | ---: |
| `SENSOR_DRIVER_INDEX_STRUCT` | 8 |
| `IMAGESENSOR_GETINFO_STRUCT` | 12 |
| `ACDK_SENSOR_PRESOLUTION_STRUCT` | 8 |
| `ACDK_SENSOR_FEATURECONTROL_STRUCT` | 16 |
| `ACDK_SENSOR_CONTROL_STRUCT` | 16 |
| `ISP_REG_IO_STRUCT` | 8 |
| `ISP_WAIT_IRQ_STRUCT` | 16 |
| `ISP_BUFFER_CTRL_STRUCT` | 20 |
| `ISP_REF_CNT_CTRL_STRUCT` | 12 |
| `CAM_PIPE_MGR_LOCK_STRUCT` | 8 |
| `CAM_PIPE_MGR_MODE_STRUCT` | 12 |
| `SYSRAM_ALLOC_STRUCT` | 20 |

The target also evaluated the exact request numbers for sensor selection,
open, mode/feature control, close, ISP register and buffer operations, pipe
locking, and SYSRAM allocation. The ABI-dump executable's SHA-256 was
`3de92687ca544430d1958df3b9e8e57947fbf0ea0ad0acfcda4960669a6b3091`.
It contains no `open`, `ioctl`, `mmap`, or write call and reports all of
`device_opened`, `ioctl_issued`, `camera_powered`, and `capture_performed` as
false.

This verifies that the first adapter must be ARM32 or explicitly use the
kernel's compat layouts. It also gives build-time constants for every state
transition before active hardware testing begins.

The live kernel config contains `# CONFIG_CHECKERS is not set`. Consequently
the running image includes `kd_imgsensor_define_default.h`, not the tempting
product-named `kd_imgsensor_define_checkers.h`. The default enum retains
`SENSOR_FEATURE_SET_DUAL_GAIN`, shifting later feature IDs by one: live
register set/get are 3011/3012 and test-pattern selection is 3043. Active
readback of OV9734 register `0x5080` exposed this distinction. The tracked ABI
dump now includes the generic configuration selector and emits these IDs so a
future adapter cannot repeat the mismatch.

## Owner-local implementation evidence

Four ARM32 stock libraries were analyzed only to identify implementation
boundaries and cross-check the public driver contract:

| Role | SHA-256 |
| --- | --- |
| Android camera HAL entry | `5abbfff6d804201dbcf094972c7d1fecc357e68e588fec685460d027103c737a` |
| sensor HAL | `a06f38db45fd99bf71f278f40dc83d1b7ede54fbbb309431a6cbf4528c8e44bd` |
| core ISP wrapper | `6f14a895e85d66c690332d519302d6567fbc94bf802d83bd3a8750a8f561e4ea` |
| FrameB I/O pipe | `0d92b52f15c322e460b43e1c0a5afb8fc2e066f3c5e5a3a4ede4ba8866efeb85` |

The sensor wrapper's exported operations reduce to initialize, select the
active compiled sensor, open, feature control, mode control, close, and
uninitialize. Its direct ioctl use matches the public `kd_imgsensor` request
set. The ISP wrapper opens `/dev/camera-isp`, maps the public ISP register
window, allocates command/buffer memory, and exposes reset, IRQ, register,
real-time-buffer, and reference-count operations. The FrameB pipe adds the
queue, start, stop, enqueue, and dequeue state machine around those primitives.

Starting from the sensor and FrameB pipe roots, the owner-local ARM32 runtime
dependency closure contains 43 inventoried ELF files and has no unresolved
SONAME. That proves the stock path is reproducible for private lab comparison,
but the private closure is not a releasable Tater dependency.

## Clean adapter boundary

The releasable adapter should preserve five explicit phases:

1. privacy gate: require an operator and read the lens-cover input;
2. sensor: select OV9734, power/open, enable its internal color-bar pattern,
   and select the 1280x720 preview scenario;
3. memory: allocate bounded ION/M4U frame buffers and any required ISP SYSRAM;
4. ISP: acquire the CamIO pipe, configure the FrameB input/output path,
   enqueue buffers, start, wait for one bounded completion, then stop; and
5. teardown: stop streaming, release every queue and mapping, close the
   sensor, and verify power/clock/IRQ return to the idle baseline on every
   error path.

The OV9734 public driver leaves streaming disabled after sensor initialization
and enables streaming only when a preview/capture/video scenario is selected.
That permits the color-bar feature to be enabled before the first streaming
mode. The first active proof therefore validated ISP transport using only
sensor-generated pixels, without recording the scene.

## Operator-present internal-pattern result

The bounded active run first verified the camera nodes were idle, the owner was
present, and the lens-cover switch was inactive. It stopped the existing camera
helpers for exclusive ownership, selected the OV9734, enabled its standard
color bars, selected preview mode, and required register `0x5080` to read back
as `0x10` before ISP DMA was enabled. It consumed eight 640x360 frames and
reported only SHA-256 values and eight vertical-band RGB means for the final
four. No image or raw frame was saved. The band means remained stable across
all four frames, providing a content-level internal-pattern check in addition
to the register readback.

Teardown disabled the pattern, stopped capture, released the ISP/ION/M4U
resources, closed and powered down the sensor, and resumed both helpers. All
camera, ISP, ION, and M4U owner checks returned idle, the privacy state remained
unchanged, and the log recorded the final ISP clock disable. This completes the
privacy-safe live camera milestone. The executable used for the active run was
uninstalled and ignored, but its hardware path came from Tater's existing
MIT-licensed MT8163 camera implementation. That open adapter already satisfies
the releasable userspace boundary on the vendor kernel; a V4L2 bridge remains a
portability improvement rather than an access blocker.

Tater's existing open camera package is the small userspace adapter around
these public character-device ABIs. It already handles bounded ION buffers,
sensor/ISP ownership, frame delivery, exposure, and reverse teardown without a
private Android camera library. A V4L2 capture bridge would still be the more
portable public interface, but it is substantially larger because buffer
ownership, format negotiation, streaming, and error recovery all need to be
represented correctly.

# Cronos ISP color-processing bring-up

Date: 2026-11-13. Experimental; live camera integration is not qualified.

The existing camera adapter uses the MT8163 ISP's sensor receiver, timing
generator, packer and IMGO DMA to obtain packed RAW10. It disables the color
pipeline. CPU Bayer development is therefore a separate cost from H.264
encoding, which already runs on the video encoder.

## Evidence and implementation boundary

The exact public kernel exposes camera-isp, camera-pipemgr, camera-sysram,
ION/M4U and CMDQ. The owner's retained Cronos image-processing libraries expose
a memory-input postprocessing pipe, CFA/demosaic and RGB-to-YUV configuration,
and a DpIspStream connection to the MDP output path. Symbol availability and
decompiler output identify interfaces; neither proves correct color output or
working frame processing.

Owner-local SHA-256 identities checked against the connected device:

| Library | SHA-256 |
| --- | --- |
| libimageio_FrmB.so | 40d0dc7066236bd6242697ce41c88abba3a9ca95210f464808a1ec81cdff4291 |
| libimageio_plat_drv_FrmB.so | 2af76f531a9ec97e298ba342566262cf4c278a20083aa9d596eb6dabe8dc7df1 |
| libcamdrv_FrmB.so | d6d04a27dddc5cba49ca99117434dacca953ac70a6e7abb8d34567ed9bd39335 |
| libdpframework.so | 1b66dc624f4968cd21bcd2180962def11dcddc92b05fa6322b5b6cb382c11564 |

The small original `isp_runtime_probe_android.c` loaded the ISP/MDP runtime
under Tater and resolved all four requested ABI symbols. It called no
processing method, opened no sensor and retained no image. Proprietary
libraries and decompilation remain in the ignored workspace; they are not
added to either repository's source or release artifacts.

## Target-specific lifecycle review

The next bounded step invokes only the postprocess factory, initialization,
uninitialization and destruction methods from the same exact Cronos runtime.
It does not call pipe configuration, enqueue, start, sensor controls, or direct
register access. This establishes whether the hardware adapter can release its
resources normally before introducing a synthetic frame.

The test is gated to ARM32 Cronos on kernel `4.9.337-g8d928c5176cc`. The wrapper
verifies no call/camera is active and no process owns the camera/ISP nodes,
temporarily pauses only the idle camera helpers to prevent a competing open,
and resumes them with a shell trap. A 10-second internal alarm and an external
15-second deadline bound the isolated process. No system/app slot, boot image,
recovery, pairing, calibration, or persistent enablement changes are needed.

Success must include matching API returns, normal process exit, released ISP
ownership, unchanged privacy state, and the normal Tater services still alive.
That remains a lifecycle result, not a RAW-to-YUV processing result. The next
processing proof must use generated Bayer color patterns in bounded memory,
check output color/layout and frame timing, and release every DMA/pipe owner.
Only after that proof should live camera processing or encoder integration be
enabled.

## Lifecycle result and synthetic-frame review

The connected Cronos completed factory, initialize, uninitialize and destroy
with both API returns equal to 1 and normal exit. The kernel logged the ISP
clock returning to zero users; no camera/ISP owner remained, privacy stayed
unchanged, and the normal daemon/renderer/camera helpers retained their PIDs.
The kernel also logged an ION compat-free error during initialization, so API
return values alone must not be treated as complete allocator qualification.

The original synthetic adapter uses two bounded ION allocations: a 20,480-byte
packed RAW10 input and a 24,576-byte planar color output. Public ARM32 ION
ioctl layouts and the observed Cronos port descriptor offsets define its ABI.
All optional tuning/security/vector fields remain disabled. The input is four
generated red/green/blue/gray vertical bands, never sensor data. The output
path is memory IMGI through the ISP to memory DISPO/MDP. No camera sensor API,
timing-generator start, or handwritten register recipe is used.

Configuration-only qualification precedes calling start. Both modes have
internal/external deadlines and the same exclusive idle-camera wrapper. On
normal cleanup, stop/uninitialize/destroy precede unmapping/freeing the buffers.
Only changed-byte counts, inner-band channel means and timings may be printed;
no image is written. Configuration success is not processing success, and a
changed buffer alone is not a correct-color pass.

## Synthetic processing investigation

The first configured job could not start because the memory-input MDP crop
was zero. This is a second crop descriptor, separate from the input DMA crop;
supplying a full-frame crop at the verified PortInfo offsets `0x7c..0x90`
allowed `start()` to return 1. An immediate frame-end call still left every
output byte at the sentinel value. It was not a processing pass.

Explicit input/output enqueue and CQ descriptor flushing were added. An
experimental dequeue invocation then faulted inside the runtime. Reviewing
the ARM instructions showed that the opaque integer arguments were ordered
CQ/duplicate/burst/timeout; the timeout had incorrectly occupied a burst
selector. Correcting that argument order is required before another job.
The PortID argument occupies four ARM32 words; the queue buffer's vector
begins at offset `0x0c`.

The faulting process released its kernel ISP ownership, but Cronos later
became unreachable over USB and Wi-Fi. The owner power-cycled it. The cause
of that later loss of responsiveness has not been established. After boot,
the normal daemon/renderer/camera helpers were running, no ISP or sensor
owner remained, and the privacy switch reported unmuted. No persistent
firmware, slot, recovery, or setting had been changed by these tests.

Further tests remain generated-pattern jobs only, with corrected selectors,
bounded downloads and execution, and numeric crash diagnostics. This is
unqualified hardware bring-up, not an enabled camera feature.

### Input DMA translation review

Source dequeue exercised the ISP but produced IMGI DMA error `0xffff0010`
and an error interrupt storm. Stop/uninitialize released ownership and the
ISP interrupt count remained stable afterward. The normal services stayed
alive. This is a failed job, regardless of any dequeue return convention.

The current probe allocated translated ION addresses without configuring the
IMGI M4U port. The existing raw capture path explicitly configures its IMGO
port. The next adapter revision reads only LARB2's MMU/secure-enable
configuration registers (`0x15001f00/04`, not interrupt/status registers),
refuses a secure IMGI port, and uses public `MTK_M4U_T_CONFIG_PORT` for port 27
(IMGI, LARB2 port 3). If originally bypassed, it enables translation for the
bounded job and restores bypass after stopping the pipe, checking readback.
No other port, sensor, clock recipe, or register write is introduced. Standard
nonsecure domain/prefetch settings are used by the kernel configuration API.

The exact owner ABI returns a signed dequeue status, not a success boolean;
zero is allowed. Configuration already enqueues the port buffers once, so a
second enqueue is unnecessary for this single-frame proof. Actual changed
output/color and clean DMA completion remain required for qualification.

### Current result: quarantined, not installed

The initialization-only readback showed IMGI already translated and nonsecure.
The missing-translation hypothesis is therefore ruled out for this Cronos;
no M4U configuration change was needed or executed. The speculative port
configuration branch was removed from the prototype.

A subsequent diagnostic did not return over USB. Network ping still answered,
but the owner confirmed that the display/menu and wake word were also
unresponsive. This is a device lockup, not simply a lost console. The precise
failure point was not recovered, and it must not be attributed conclusively
to a particular MMIO read or driver instruction. No further live ISP jobs
should be attempted without a separate durable kernel trace channel and a
review of the IMGI DMA/command reset path.

`--process-synthetic` now refuses execution in default builds through
`TATER_ISP_UNSAFE_LAB_JOB=0`. Post-job MMIO readback was removed: clock/reset
state after a failed job is not established. Even an overridden lab build
cannot report a color-processing pass merely from changed output bytes.
The existing firmware's CPU Bayer development, hardware H.264 codec and GPU
display paths are unchanged. The probes were temporary executables only;
no system/app slot, boot image, service configuration, or release was updated.

### Recovery verified after the second power cycle

The owner restarted Cronos. Read-only USB checks again completed normally;
the daemon, renderer and both camera helpers were running and not stopped.
No process owned the ISP or either sensor node. The ISP interrupt count was
zero in two separate checks, so the earlier error storm was not continuing
after reboot. These are service/ownership checks, not a new camera or wake-word
qualification test. No ISP job was run after this recovery.

The running kernel enables PSTORE, PSTORE_CONSOLE and PSTORE_RAM, but the
current `/sys/fs/pstore` listing returned no files. That alone does not prove
the existence of a configured persistent backend or that all possible crash
records have been checked. Establishing a durable trace channel remains a
prerequisite for any later unsafe lab-job override.

## Durable trace preparation (no processing retry)

Further read-only checks confirmed that pstore is mounted and the ramoops
platform device is present. The reported backend has 917,504 bytes, including
a 65,536-byte console region and 4,096-byte crash records. There were no crash
files after the owner's power cycles. These checks do not test retention
across a fault or prove that a forced power loss preserves RAM.

The previous serial helper discarded partial output if its end marker never
arrived. The original `scripts/serial_trace_capture.py` instead exclusively
creates a mode-0600 owner-local log, incrementally saves/syncs incoming bytes,
and returns an explicit incomplete/timeout/limit result without replaying the
command. Tests cover lost end markers, split markers, command echo, log limits
and refusing to overwrite an existing trace.

`isp_kmsg_trace.c` opens only `/dev/kmsg` read-only, seeks to new records and
filters camera/ISP/CMDQ/M4U/fault diagnostics. It changes no console setting,
clears no ring buffer, opens no sensor/ISP node, and is bounded to 15 seconds,
128 records and 1,024 bytes per diagnostic. Before any future hardware job,
its standalone 1.5-second read-only check must deliver ready/finished markers
to a durable Mac log while Tater's normal services stay running. This tests
the logging mechanism only, not ISP processing or fault recovery. A host log
can preserve messages already delivered; it cannot make a kernel lockup safe.

### Prepared-buffer inspection review

The standalone trace check delivered ready/finished markers to the Mac, with
no ISP job, no new ISP interrupts and unchanged normal service PIDs.

The next configuration-only revision removes direct hardware-register reads.
It uses the owner's exported `IspDrv::getCQVirBufVirAddr(3,0,0)` to inspect the
prepared CQ shadow buffer in CPU memory. The observed implementation only
locks the driver object, resolves a real queue index and returns a stored
virtual-buffer pointer; it does not start DMA or access a sensor. The pointer
must cover the whole inspected range in a readable/writable mapping, and
known `/dev/camera-isp` and `/dev/mem` mappings are rejected. Only selected
enable/format/input-address/geometry words are printed, not tuning tables.

This remains `--configure-synthetic`: start/dequeue/flush are not invoked,
processing stays quarantined, the helpers are resumed on exit, and the new
durable capture preserves partial output if normal completion fails.

The configuration-only inspection then completed with normal cleanup, zero
new ISP interrupts and unchanged service PIDs. Its prepared input MVA matched
the allocated buffer, row byte width/stride were 160, and height was 128 for
the 128×128 packed RAW10 synthetic input. This rules out those basic address
and geometry mismatches, not all DMA/CMDQ setup. Zero words in the base enable
slots do not prove missing enables: this runtime also uses SET/CLR aliases.
No processing job was retried.

The owner chose to prototype GPU color conversion instead of pursuing further
native ISP jobs. See [GPU Bayer results](cronos-gpu-bayer-conversion.md). The
native ISP remains disabled; GPU success must not be reported as an ISP pass.

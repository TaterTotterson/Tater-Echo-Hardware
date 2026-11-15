# Checkers bounded runtime headroom

Date: 2026-11-11

A two-second anonymous-memory/CPU probe now supplies a small regression
baseline without opening hardware devices or writing persistent storage. This
is not a general benchmark and does not establish worst-case thermal limits.

## Bounded result

At a reported 1.3 GHz on all four cores, the connected unit completed:

- 51 copies of a 32 MiB buffer in the one-second memory phase, 1.584 GiB/s;
- 84.110 million scalar xorshift iterations per second in the CPU phase; and
- a CPU-zone change from 51.1 °C to 53.8 °C.

Available memory changed from 667,564 KiB to 600,972 KiB while both 32 MiB
anonymous buffers were intentionally still resident. Immediately after process
exit it returned to 667,968 KiB, and five seconds later remained at 667,760
KiB. The CPU zone was already 50.6 °C immediately after exit and 50.7 °C after
five seconds. The two short phases did not trigger throttling; CPU0 reported
1.3 GHz both before and after.

## Service recovery

The metadata-only audio probe was run immediately after the load. Capture
remained `RUNNING`, its hardware pointer advanced 31,819 frames during the
two-second observation, and the same `arecord`-to-`tater-echo` pipe remained
connected. The normal owners were also intact:

- `tater-show` still held `/dev/fb0`;
- `tater-echo`/`arecord` still held capture and playback;
- both camera helpers remained running; and
- the camera, ISP, ION, and M4U paths had no temporary test owner.

Together with the decoded-frame presentation timing (44 presentations in
6.101 seconds; worst conversion 92.809 ms and pan 21.646 ms) and the verified
camera reverse teardown, this completes the first CPU, memory-bandwidth,
thermal, latency, and recovery baseline. Longer soak and concurrent-media tests
remain release-qualification work, not a prerequisite for proving access.

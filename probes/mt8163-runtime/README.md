# Checkers bounded runtime probe

This probe runs two fixed one-second phases: repeated copies between two 32 MiB
anonymous buffers and a scalar xorshift loop. It records CPU frequency,
available memory before allocation and while the two buffers remain resident,
and the CPU thermal zone before and after. It does not open a hardware device
or write persistent storage. The process frees both buffers on exit; a separate
post-exit observation is the recovery check.

The command refuses a target without the expected Checkers ALSA topology and
requires an explicit bounded-load acknowledgement:

```sh
./checkers-runtime-probe \
  --target checkers --acknowledge bounded-load
```

The numbers are a reproducible regression baseline, not a general-purpose CPU
benchmark. Run the read-only audio status probe afterward to verify capture
continuity, and verify the Tater renderer and camera helpers still own their
normal resources.

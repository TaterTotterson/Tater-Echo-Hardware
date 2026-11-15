# MT8163 display compat fix

This directory contains a narrow fix for the public Checkers kernel commit
`8d928c5176cc`. A live 32-bit read-only display query proved that session info,
capability, framebuffer, and LCM-index requests work, while
`DISP_IOCTL_GET_IS_DRIVER_SUSPEND` returns `EFAULT`.

The public compat handler confirms the cause: it passes a four-byte value to
the native ioctl where a user pointer is required. The patch allocates a
native user-space slot, passes that slot to the existing native handler, and
copies the returned value back. It changes no display mode, timing, buffer,
plane, or rendering behavior.

The patch also passes a complete `Image.gz-dtb` build with the exact live
configuration and pinned GCC 8.3 toolchain. The result preserves release
`4.9.337-g8d928c5176cc`, has a byte-identical `Module.symvers` to the earlier
vendor-module-checked build, and excludes the rejected JPEG decoder experiment.
See the
[build record](../../docs/findings/checkers-display-compat-kernel-build.md).

This remains source work, not a flashed device change. Validate it against an
unmodified source tree with:

```sh
kernel-patches/mt8163-display/check.sh /path/to/android_kernel_amazon_mt8163
```

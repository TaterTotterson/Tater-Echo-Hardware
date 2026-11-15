# MT8163 JPEG control probe

This probe records the bounded Checkers JPEG experiment. Live testing rejected
the inferred standalone decoder window: a real decode timed out without an
interrupt or output change while the known encoder window showed coherent
state. These tools remain useful for reproducing that negative result, not as
a production decoder path. See
[`docs/findings/checkers-live-media-bringup.md`](../../docs/findings/checkers-live-media-bringup.md).

The basic probe validates the smallest confirmed part of the Checkers JPEG
driver contract.

The default `--describe` mode is host-safe and prints the fixed ARM32 ABI
values without opening a device. `--inspect` verifies a running Checkers target
and stats `/proc/mtk_jpeg` without opening it. It also reports whether the
running kernel contains the decoder implementation.

```sh
make -C probes/mt8163-jpeg
probes/mt8163-jpeg/build/mt8163-jpeg-probe --describe
```

The active INIT/DEINIT cycle is intentionally guarded by all three arguments:

```text
--execute-init-cycle --target checkers --acknowledge-hardware-ioctl
```

That mode must not be run merely because it builds. Before execution, review
the connected-device evidence, confirm recovery and stable power, ensure no
decoder operation is active, and obtain explicit operator approval. It only
opens `/proc/mtk_jpeg` read-only as the exact live vendor library does, issues
INIT followed immediately by DEINIT, and closes the descriptor. It refuses
before opening the node when the running kernel lacks the expected decoder
symbols. Termination signals are blocked across the bounded sequence.

The ABI constants come from the independently recorded interface in
`docs/findings/checkers-v4.0.2-read-only-baseline.md` and are corroborated by
the public MT8163 kernel header. No decompiled source is included here.

`mt8163-ion-inspect` is a separate read-only companion. It opens `/dev/ion`,
uses the public `ION_IOC_HEAP_QUERY` ABI, and prints the available heap names,
types, and IDs without allocating or mapping a buffer.

The ARM32 `mt8163-ion-buffer-probe` takes the next bounded step: it allocates
two private 4 KiB buffers from the multimedia heap, maps them to the JPEG
decoder's bitstream/output M4U ports, verifies CPU mappings and cache sync, and
then releases them. It never configures or starts the JPEG engine. Its guarded
invocation is:

```sh
./mt8163-ion-buffer-probe --execute-alloc-cycle --target checkers \
  --acknowledge-m4u-mapping private-buffers-only
```

`kernel-diagnostics/mt8163_jpeg_regs.c` is a removable, read-only diagnostic
module for the trial kernel. It exposes a named register snapshot at
`/proc/mt8163_jpeg_regs`; it has no write handler and performs no register
writes. Generated `.ko` files remain local build products.

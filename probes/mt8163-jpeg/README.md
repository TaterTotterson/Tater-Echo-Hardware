# MT8163 JPEG control probe

This probe validates the smallest confirmed part of the Checkers JPEG driver
contract. It does not configure buffers or decode an image yet.

The default `--describe` mode is host-safe and prints the fixed ARM32 ABI
values without opening a device. `--inspect` verifies a running Checkers target
and stats `/proc/mtk_jpeg` without opening it.

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
opens `/proc/mtk_jpeg`, issues INIT followed immediately by DEINIT, and closes
the descriptor. Termination signals are blocked across that bounded sequence.

The ABI constants come from the independently recorded interface in
`docs/findings/checkers-v4.0.2-read-only-baseline.md` and are corroborated by
the public MT8163 kernel header. No decompiled source is included here.

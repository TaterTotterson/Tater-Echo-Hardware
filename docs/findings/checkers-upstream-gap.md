# Checkers upstream Linux gap

Date: 2026-11-11

The current upstream Linux tree was audited to determine whether Checkers can
replace its owner-local GPU and video userspace by moving directly to a modern
kernel. The inspected revision was
`067c5c24cef23d913b67eaaf71ae67e7169a6620` from Linus Torvalds' official
repository. The checkout remains ignored local reference material.

The result is mixed:

- Panfrost supports the generic `arm,mali-t720` GPU and its binding describes
  the standard three-interrupt, clock, power-domain, and reset contract.
- Upstream has no MT8163 device tree, no `mediatek,mt8163-mali` platform data,
  and therefore no ready Checkers board description.
- The MediaTek V4L2 decoder supports several later SoCs, beginning with
  MT8173, but has no `mediatek,mt8163-vcodec-dec` match.
- Checkers' proven decoder is the older direct `/dev/Vcodec` generation. The
  later upstream driver delegates codec work to a MediaTek VPU/SCP firmware
  interface and cannot be assumed compatible with it.

This means a modern-kernel switch is not a shortcut to a complete open image.
Panfrost is a credible long-term GPU endpoint, but first requires an MT8163
SoC/board port covering clocks, power, IOMMU, display, audio, storage, input,
wireless, and suspend. The VDEC still needs either a clean bridge around the
proven legacy hardware contract or a new upstream implementation for this
generation.

A separate public Android hardware-source audit reached the same boundary from
the other side: the tree includes the OMX core, Stagefright plugin, and MT8163
VAL types, but delegates AVC decode to an absent `libMtkOmxVdec.so` component.
See [MT8163 decoder source availability](mt8163-vdec-source-availability.md).

For current Tater development, the practical layers are therefore:

1. use the proven owner-local GPU and decoder runtime only on the owner's
   extracted firmware, never as redistributable repository content;
2. keep clean conversion, renderer integration, policy, and IPC code fully
   open; and
3. pursue Panfrost and a standard V4L2 boundary as separate long-term ports,
   without blocking the usable Checkers firmware on an all-at-once kernel
   replacement.

The deterministic audit can be rerun against any upstream checkout:

```sh
python3 scripts/audit_mt8163_upstream.py /path/to/linux
```

Its JSON distinguishes generic Mali-T720 support from actual MT8163 platform
and decoder support so those claims cannot be conflated.

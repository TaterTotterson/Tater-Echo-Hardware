# Experimental Athena AP codec adapters

Original GPL-2.0-only kernel source, not production firmware or an installation
recipe. No proprietary implementation, device library, firmware or calibration
is included. This is the processor-controlled route; it does not start SCP or
change access permissions. The earlier SCP experiment is a separate path.

Build against the exact reviewed Linux 6.18.56 tree, configuration and complete
symbol versions. For an external-module build, use this directory as `M` and a
separate ignored build directory as `MO`. A successful compile is not hardware
qualification. Never insert these modules into another kernel or board.

Both modules refuse by default. They additionally require the reviewed Athena/
MT8183 identity and exact trial-kernel build identity; opening a control node
requires `CAP_SYS_RAWIO`. Only one controller may own each engine. Separate
read-only module parameters gate register mapping. There is no arbitrary
physical-memory, security, firmware or legacy-ioctl interface.

The fixed ABI in `athena_ap.h` exposes engine identity, native power control,
owned coherent DMA allocation, actual bounded IRQ waits, software IRQ clearing,
and one decoder-specific DCM operation. Allocations are limited to 8 MiB each
and 64 MiB per engine, with 32-bit IOMMU addresses and owned anonymous fds.
MMIO and DMA mappings are non-executable. Decoder mappings reject wrapped PFNs;
the shared clock page is read-only and cannot gain write permission later.

Native Linux clocks, runtime power, DT resources, IRQs and DMA APIs retain
ownership. Decoder DCM updates touch only the reviewed bit and restore it at
cleanup. A buffer closed during a powered session is retired, not freed until
the controller quiesces/resets and powers the engine down. Remaining live fds/
VMAs pin their files and module. A fresh controller refuses outstanding memory.

Generated-pixel and process-exit evidence is recorded in
`docs/findings/athena-codec-prerequisites.md`. Production helper integration,
long-running concurrent encoding/decoding, real calls, privacy and matched system
packaging are separate gates. Do not enable capability advertisement merely
because these nodes exist. Keep independently verified recovery/rollback and
the owner's working GPU/voice system available during every hardware trial.

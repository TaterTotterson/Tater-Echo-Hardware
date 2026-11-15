# Athena codec research trial

Unreleased and not production codec qualification. Do not use MT8163 Android
helpers on this MT8183 board, or enable H.264 advertisement from driver inventory.

`patch_scp_trial.py` applies only to the reviewed Linux 6.18.56 SCP source hash.
Without `--apply` it only checks the source. On exact Athena/MT8183 it adds an
off-by-default `athena_trial` module parameter, disables remoteproc automatic
boot/recovery and quiesces SCP/reset/watchdog before installing its IRQ handler.
Other boards retain upstream behavior. This patch is not a firmware-compatibility
check and is not permission to operate any connected device.

`athena-scp-trial.dtso` adds only the fixed DRAM pool and SCP node properties.
The driver's 5 MiB coherent allocation needs an 8 MiB power-of-two pool.
`scripts/athena_scp_trial_dtb.py` verifies that the applied overlay preserves
every unrelated DT property, rejects overlaps/phandle collisions, and never
prints private calibration bytes. It still reports hardware as unqualified.

Controlled owner-device trials verified physical TWRP recovery, original
ramdisk/tuning, normal kernel boot, default-off refusal, manual offline probe,
SCP readiness and codec capability queries, with owner audio/touch checks.
Generated encode/decode, Request API/MM21 integration and live privacy/call
qualification remain required. A manually flashed kernel can be replaced by
the rootfs's normal pinned-kernel updater; final system artifacts must carry
their matching kernel. Never disable recovery/trial protections to hide this.

All device backups, filled images, firmware/runtime files and calibration stay
in ignored owner-local storage. This directory contains original source-only
research tooling, not firmware binaries or device-specific images.

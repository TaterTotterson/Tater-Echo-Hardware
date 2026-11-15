// SPDX-License-Identifier: GPL-2.0-only
/* Original fixed-width experiment ABI; no legacy physical-memory ioctls. */
#ifndef TATER_ATHENA_AP_H
#define TATER_ATHENA_AP_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define AP_MAGIC 0xa7
#define AP_INFO _IOR(AP_MAGIC, 0, struct ap_info)
#define AP_POWER _IOW(AP_MAGIC, 1, __u32)
#define AP_ALLOC _IOWR(AP_MAGIC, 2, struct ap_alloc)
#define AP_WAIT _IOWR(AP_MAGIC, 3, struct ap_wait)
#define AP_CLEAR _IO(AP_MAGIC, 4)
#define AP_DCM _IO(AP_MAGIC, 5) /* Decoder-only bounded DCM-disable request. */
struct ap_info { __u32 version, engine, mmio_enabled, powered; };
struct ap_alloc { __u64 bytes, dma; __s32 fd; __u32 reserved; };
struct ap_wait { __u32 timeout_ms, status; };
#endif

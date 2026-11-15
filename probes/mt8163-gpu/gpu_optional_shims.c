#include <stddef.h>

/*
 * The r26 vendor DDK links optional GED logging, gralloc-extra, and GPU-aux
 * conversion helpers. A private pbuffer uses none of those native-window
 * paths. These fail-closed exports keep that optional graph disabled while
 * leaving Mali allocation, command submission, and readback in the DDK.
 */

__attribute__((visibility("default")))
void *ged_log_connect(void) { return NULL; }
__attribute__((visibility("default")))
int ged_log_disconnect(void) { return 0; }
__attribute__((visibility("default")))
int ged_log_tpt_print(void) { return 0; }

__attribute__((visibility("default")))
void *GpuAuxCreateContext(void) { return NULL; }
__attribute__((visibility("default")))
int GpuAuxDestoryContext(void) { return 0; }
__attribute__((visibility("default")))
int GpuAuxDoConversionIfNeed(void) { return 0; }
__attribute__((visibility("default")))
void *GpuAuxGetCurrentAuxBuffer(void) { return NULL; }
__attribute__((visibility("default")))
int GpuAuxGetCurrentIdx(void) { return 0; }
__attribute__((visibility("default")))
int GpuAuxIsSupportFormat(void) { return 0; }
__attribute__((visibility("default")))
int GpuAuxIsUIPQFormat(void) { return 0; }
__attribute__((visibility("default")))
int GpuAuxPrepareBuffer(void) { return 0; }

__attribute__((visibility("default")))
int gralloc_extra_perform(void) { return -1; }
__attribute__((visibility("default")))
int gralloc_extra_query(void) { return -1; }
__attribute__((visibility("default")))
int gralloc_extra_sf_set_status(void) { return -1; }

// SPDX-License-Identifier: MIT

#ifndef TATER_OMX_PROBE_ANDROID_H
#define TATER_OMX_PROBE_ANDROID_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runs the clean OMX lifecycle implementation used by the diagnostic probe.
 * Keeping the lifecycle behind one entry point lets the bounded tater-vdec
 * adapter reuse the already-tested state, port, buffer, and teardown path
 * without copying it. The argument contract is documented by the probe CLI.
 */
int tater_omx_probe_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif

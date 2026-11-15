/* SPDX-License-Identifier: MIT
 * Preserve the original Checkers encoder's Bayer geometry, tone LUT, planar
 * chroma ordering, stream framing and deadlines. Its independently observed
 * version-word-1 parameter ABI also matches the retained Lineage component.
 * The new target guard and unload policy belong only to this candidate.
 */
#include "checkers_target.h"
#include <unistd.h>

int tater_checkers_encoder_main(int argc, char **argv);

/* --wrap=dlclose applies to this executable's calls, not vendor internals.
 * Explicit OMX teardown still runs in tater_checkers_encoder_main. Android
 * DSO finalizers stay isolated until process exit, as in the other Shows.
 */
int __wrap_dlclose(void *library) {
    (void)library;
    return 0;
}

int main(int argc, char **argv) {
    int result;
    if (!checkers_lineage_target_allowed()) {
        fputs("refusing: Checkers Lineage kernel/product/panel preflight failed\n", stderr);
        return 2;
    }
    result = tater_checkers_encoder_main(argc, argv);
    fflush(NULL);
    _exit(result);
}

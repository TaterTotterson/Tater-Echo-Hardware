/* SPDX-License-Identifier: MIT
 * Headless replacement for libion_ulit's sole diagnostic export. Actual ION
 * allocation/cache ioctls remain in the owner's libion/libion_mtk. This does
 * not suppress their errors or replace any allocator/codec operation.
 */
#include <stdio.h>

void dumping_callstack(void) {
    fputs("Tater ION: diagnostic call stack unavailable in headless runtime\n", stderr);
}

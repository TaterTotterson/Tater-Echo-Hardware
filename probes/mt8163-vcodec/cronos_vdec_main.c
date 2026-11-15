/* SPDX-License-Identifier: MIT
 * Guarded Cronos decoder qualification. Uses standard OpenMAX buffers first;
 * no Checkers GraphicBuffer C++ ABI is linked into this helper.
 */
#include <stdio.h>
#include <unistd.h>
#include "omx_probe_android.h"
#include "../mt8163-gpu/cronos_target.h"

int main(int argc, char **argv) {
    if (!cronos_gpu_target_allowed()) {
        fputs("refusing: Cronos decoder kernel/product/panel preflight failed\n", stderr);
        return 2;
    }
    int result = tater_omx_probe_main(argc, argv);
    fflush(NULL);
    _exit(result);
}

/* SPDX-License-Identifier: MIT
 * Athena mainline metadata-only guard. No register or device writes.
 */
#ifndef TATER_ATHENA_GPU_TARGET_H
#define TATER_ATHENA_GPU_TARGET_H
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static int athena_gpu_target_allowed(void) {
    struct utsname identity;
    char value[256];
    const char *paths[] = {"/proc/device-tree/compatible",
        "/sys/class/graphics/fb0/virtual_size",
        "/sys/class/graphics/fb0/bits_per_pixel",
        "/sys/class/drm/renderD128/device/uevent"};
    int product = 0;
    if (uname(&identity) || strcmp(identity.release, "6.18.56-techo5-athena")) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        FILE *file = fopen(paths[i], "rb");
        size_t count;
        if (!file) return 0;
        count = fread(value, 1, sizeof(value)-1, file);
        int complete = !ferror(file) && fgetc(file) == EOF;
        fclose(file);
        if (!complete || !count) return 0;
        value[count] = 0;
        if (i == 0) {
            for (size_t at = 0; at < count; at += strlen(value+at)+1)
                if (!strcmp(value+at, "amzn,athena")) product = 1;
            if (!product) return 0;
        } else if (i == 1 && strcmp(value, "800,2560\n")) return 0;
        else if (i == 2 && strcmp(value, "32\n")) return 0;
        else if (i == 3 && strncmp(value, "DRIVER=panfrost\n", 16)) return 0;
    }
    return 1;
}
#endif

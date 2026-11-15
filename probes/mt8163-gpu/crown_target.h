/* SPDX-License-Identifier: MIT
 * Crown metadata-only guard. No register, framebuffer or device writes.
 */
#ifndef TATER_CROWN_GPU_TARGET_H
#define TATER_CROWN_GPU_TARGET_H
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static int crown_read_metadata(const char *path, char *value, size_t capacity) {
    FILE *file = fopen(path, "r");
    size_t length;
    int complete;
    if (!file || capacity < 2) {
        if (file) fclose(file);
        return 0;
    }
    length = fread(value, 1, capacity - 1, file);
    complete = !ferror(file) && fgetc(file) == EOF;
    fclose(file);
    value[length] = '\0';
    while (length && (value[length-1] == '\n' || value[length-1] == '\r'))
        value[--length] = '\0';
    return complete && length != 0;
}

static int crown_gpu_metadata_allowed(const char *command_path,
                                      const char *size_path,
                                      const char *depth_path) {
    char command[4096], size[64], depth[32];
    char *token;
    int product = 0;
    if (!crown_read_metadata(command_path, command, sizeof(command)) ||
        !crown_read_metadata(size_path, size, sizeof(size)) ||
        !crown_read_metadata(depth_path, depth, sizeof(depth))) return 0;
    for (token = strtok(command, " \t"); token; token = strtok(NULL, " \t")) {
        if (!strncmp(token, "androidboot.product=", 20)) {
            if (product || strcmp(token, "androidboot.product=crown")) return 0;
            product = 1;
        }
    }
    return product && !strcmp(size, "800,3840") && !strcmp(depth, "32");
}

static int crown_gpu_target_allowed(void) {
    struct utsname identity;
    return uname(&identity) == 0 &&
        !strcmp(identity.release, "4.9.337-g8d928c5176cc") &&
        crown_gpu_metadata_allowed("/proc/cmdline",
            "/sys/class/graphics/fb0/virtual_size",
            "/sys/class/graphics/fb0/bits_per_pixel");
}
#endif

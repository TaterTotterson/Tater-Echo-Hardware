/* SPDX-License-Identifier: MIT
 * Metadata-only guard for the 2017 Spot. Does not open hardware devices.
 */
#ifndef TATER_ROOK_GPU_TARGET_H
#define TATER_ROOK_GPU_TARGET_H
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static int rook_read_metadata(const char *path, char *value, size_t capacity) {
    FILE *file = fopen(path, "r");
    if (!file || capacity < 2) {
        if (file) fclose(file);
        return 0;
    }
    size_t length = fread(value, 1, capacity - 1, file);
    int complete = !ferror(file) && fgetc(file) == EOF;
    fclose(file);
    value[length] = 0;
    while (length && (value[length-1] == '\n' || value[length-1] == '\r')) value[--length] = 0;
    return complete && length;
}

static int rook_gpu_metadata_allowed(const char *command_path, const char *size_path, const char *depth_path) {
    char command[4096], size[64], depth[32];
    int hardware = 0, panel = 0, linux_boot = 0;
    if (!rook_read_metadata(command_path, command, sizeof(command)) ||
        !rook_read_metadata(size_path, size, sizeof(size)) ||
        !rook_read_metadata(depth_path, depth, sizeof(depth))) return 0;
    for (char *token = strtok(command, " \t"); token; token = strtok(NULL, " \t")) {
        if (!strncmp(token, "androidboot.product=", 20) && strcmp(token, "androidboot.product=rook")) return 0;
        if (!strncmp(token, "androidboot.hardware=", 21)) {
            if (hardware || strcmp(token, "androidboot.hardware=mt8163")) return 0;
            hardware = 1;
        }
        if (!strncmp(token, "lcm=", 4)) {
            if (panel || strcmp(token, "lcm=1-hx8379c_wvga_dsi_vdo")) return 0;
            panel = 1;
        }
        if (!strcmp(token, "techo5=linux")) linux_boot = 1;
    }
    return hardware && panel && linux_boot && !strcmp(size, "480,1440") && !strcmp(depth, "32");
}

static int rook_gpu_target_allowed(void) {
    struct utsname identity;
    return uname(&identity) == 0 && !strcmp(identity.release, "4.9.337-g4174e0b4d0e2") &&
        rook_gpu_metadata_allowed("/proc/cmdline", "/sys/class/graphics/fb0/virtual_size", "/sys/class/graphics/fb0/bits_per_pixel");
}
#endif

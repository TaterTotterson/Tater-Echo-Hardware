#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int bit_is_set(const unsigned char *bits, unsigned int bit) {
    return (bits[bit / 8U] & (1U << (bit % 8U))) != 0;
}

static int target_is_checkers(void) {
    char model[64] = {0};
    char version[64] = {0};
    FILE *file = fopen("/proc/device-tree/model", "rb");
    size_t count;

    if (file == NULL)
        return 0;
    count = fread(model, 1, sizeof(model) - 1, file);
    fclose(file);
    if (count == 0 || strcmp(model, "MT8163") != 0)
        return 0;

    file = fopen("/proc/device-tree/version", "rb");
    if (file == NULL)
        return 0;
    count = fread(version, 1, sizeof(version) - 1, file);
    fclose(file);
    return count > 0;
}

int main(int argc, char **argv) {
    unsigned int index;

    if (argc != 4 || strcmp(argv[1], "--read-only-query") != 0 ||
        strcmp(argv[2], "--target") != 0 || strcmp(argv[3], "checkers") != 0) {
        fprintf(stderr,
                "usage: %s --read-only-query --target checkers\n",
                argv[0]);
        return 2;
    }
    if (!target_is_checkers()) {
        fprintf(stderr, "refusing non-Checkers target\n");
        return 3;
    }

    for (index = 0; index < 32; ++index) {
        char path[64];
        char capability_path[96];
        char name[128] = {0};
        unsigned char state[(SW_MAX / 8U) + 1] = {0};
        unsigned long capabilities = 0;
        FILE *capability_file;
        int fd;

        (void)snprintf(path, sizeof(path), "/dev/input/event%u", index);
        (void)snprintf(capability_path, sizeof(capability_path),
                       "/sys/class/input/event%u/device/capabilities/sw", index);
        capability_file = fopen(capability_path, "r");
        if (capability_file == NULL)
            continue;
        if (fscanf(capability_file, "%lx", &capabilities) != 1) {
            fclose(capability_file);
            continue;
        }
        fclose(capability_file);
        if ((capabilities & (1UL << SW_CAMERA_LENS_COVER)) == 0)
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            fprintf(stderr, "could not open %s read-only: %s\n", path,
                    strerror(errno));
            continue;
        }
        if (ioctl(fd, EVIOCGSW(sizeof(state)), state) >= 0) {
            int active = bit_is_set(state, SW_CAMERA_LENS_COVER);
            (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            close(fd);
            printf("{\"input\":\"%s\",\"name\":\"%s\","
                   "\"switch\":\"SW_CAMERA_LENS_COVER\","
                   "\"active\":%s,\"camera_touched\":false}\n",
                   path, name, active ? "true" : "false");
            return 0;
        }
        fprintf(stderr, "could not query %s switch state: %s\n", path,
                strerror(errno));
        close(fd);
    }

    fprintf(stderr, "camera lens-cover switch not found\n");
    return 4;
}

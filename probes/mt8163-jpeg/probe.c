#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define JPEG_NODE "/proc/mtk_jpeg"
#define JPEG_INIT ((unsigned long)0x00007801)
#define JPEG_CONFIG_ARM32 ((unsigned long)0x40d07802)
#define JPEG_START ((unsigned long)0x00007804)
#define JPEG_WAIT_ARM32 ((unsigned long)0xc0087805)
#define JPEG_DEINIT ((unsigned long)0x00007806)
#define JPEG_RESET ((unsigned long)0x00007807)
#define JPEG_RESUME_ARM32 ((unsigned long)0x4010780b)

struct jpeg_dec_output_arm32 {
    int32_t timeout;
    uint32_t result_address;
};

_Static_assert(sizeof(struct jpeg_dec_output_arm32) == 8,
               "ARM32 JPEG wait ABI must remain eight bytes");

enum action {
    ACTION_NONE,
    ACTION_DESCRIBE,
    ACTION_INSPECT,
    ACTION_INIT_CYCLE,
};

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage:\n"
            "  %s --describe\n"
            "  %s --inspect\n"
            "  %s --execute-init-cycle --target checkers "
            "--acknowledge-hardware-ioctl\n",
            program, program, program);
}

static bool file_contains(const char *path, const char *needle) {
    unsigned char buffer[8192];
    size_t used = 0;
    size_t needle_size = strlen(needle);
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0 || needle_size == 0 || needle_size > sizeof(buffer)) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return false;
    }
    for (;;) {
        ssize_t count = read(descriptor, buffer + used, sizeof(buffer) - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        used += (size_t)count;
        if (used == sizeof(buffer)) {
            break;
        }
    }
    close(descriptor);
    if (used < needle_size) {
        return false;
    }
    for (size_t offset = 0; offset <= used - needle_size; ++offset) {
        if (memcmp(buffer + offset, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static int verify_checkers(void) {
    if (!file_contains("/proc/device-tree/compatible", "mediatek,mt8163")) {
        fprintf(stderr, "refusing: device is not compatible with mediatek,mt8163\n");
        return -1;
    }
    if (!file_contains("/proc/cmdline", "androidboot.product=checkers")) {
        fprintf(stderr, "refusing: boot target is not checkers\n");
        return -1;
    }
    return 0;
}

static int verify_node(void) {
    struct stat details;
    if (stat(JPEG_NODE, &details) != 0) {
        fprintf(stderr, "could not stat %s: %s\n", JPEG_NODE, strerror(errno));
        return -1;
    }
    if (!S_ISREG(details.st_mode)) {
        fprintf(stderr, "refusing: %s is not the expected proc file\n", JPEG_NODE);
        return -1;
    }
    return 0;
}

static int inspect_node(void) {
    if (verify_checkers() != 0 || verify_node() != 0) {
        return 2;
    }
    printf("{\"target\":\"checkers\",\"compatible\":\"mediatek,mt8163\","
           "\"node\":\"%s\",\"node_opened\":false,"
           "\"hardware_ioctl_issued\":false}\n",
           JPEG_NODE);
    return 0;
}

static int execute_init_cycle(void) {
    sigset_t blocked;
    sigset_t previous;
    int descriptor = -1;
    int initialized = 0;
    int deinit_attempted = 0;
    int result = 2;

    if (verify_checkers() != 0 || verify_node() != 0) {
        return 2;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "refusing: active probe requires root\n");
        return 2;
    }

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        fprintf(stderr, "could not block termination signals: %s\n", strerror(errno));
        return 2;
    }

    descriptor = open(JPEG_NODE, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        fprintf(stderr, "could not open %s: %s\n", JPEG_NODE, strerror(errno));
        goto cleanup;
    }
    if (ioctl(descriptor, JPEG_INIT) != 0) {
        fprintf(stderr, "JPEG INIT failed: %s\n", strerror(errno));
        goto cleanup;
    }
    initialized = 1;
    deinit_attempted = 1;
    if (ioctl(descriptor, JPEG_DEINIT) != 0) {
        fprintf(stderr, "JPEG DEINIT failed: %s\n", strerror(errno));
        goto cleanup;
    }
    initialized = 0;
    printf("{\"target\":\"checkers\",\"node\":\"%s\","
           "\"init\":\"ok\",\"deinit\":\"ok\"}\n",
           JPEG_NODE);
    result = 0;

cleanup:
    if (initialized && !deinit_attempted) {
        if (ioctl(descriptor, JPEG_DEINIT) != 0) {
            fprintf(stderr, "cleanup JPEG DEINIT failed: %s\n", strerror(errno));
        }
    }
    if (descriptor >= 0) {
        close(descriptor);
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        fprintf(stderr, "could not restore signal mask: %s\n", strerror(errno));
        return 2;
    }
    return result;
}

int main(int argc, char **argv) {
    enum action action = ACTION_NONE;
    bool target_checkers = false;
    bool acknowledged = false;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--describe") == 0) {
            if (action != ACTION_NONE) {
                usage(stderr, argv[0]);
                return 2;
            }
            action = ACTION_DESCRIBE;
        } else if (strcmp(argv[index], "--inspect") == 0) {
            if (action != ACTION_NONE) {
                usage(stderr, argv[0]);
                return 2;
            }
            action = ACTION_INSPECT;
        } else if (strcmp(argv[index], "--execute-init-cycle") == 0) {
            if (action != ACTION_NONE) {
                usage(stderr, argv[0]);
                return 2;
            }
            action = ACTION_INIT_CYCLE;
        } else if (strcmp(argv[index], "--target") == 0 && index + 1 < argc) {
            target_checkers = strcmp(argv[++index], "checkers") == 0;
        } else if (strcmp(argv[index], "--acknowledge-hardware-ioctl") == 0) {
            acknowledged = true;
        } else {
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (action == ACTION_NONE) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (action == ACTION_DESCRIBE) {
        printf("{\"schema_version\":1,\"abi\":\"mt8163-arm32\","
               "\"output_size\":%zu,\"config_size\":208,"
               "\"resume_size\":16,\"init\":\"0x%08lx\","
               "\"config\":\"0x%08lx\",\"start\":\"0x%08lx\","
               "\"wait\":\"0x%08lx\",\"deinit\":\"0x%08lx\","
               "\"reset\":\"0x%08lx\",\"resume\":\"0x%08lx\"}\n",
               sizeof(struct jpeg_dec_output_arm32), JPEG_INIT,
               JPEG_CONFIG_ARM32, JPEG_START, JPEG_WAIT_ARM32, JPEG_DEINIT,
               JPEG_RESET, JPEG_RESUME_ARM32);
        return 0;
    }
    if (action == ACTION_INSPECT) {
        return inspect_node();
    }
    if (action == ACTION_INIT_CYCLE && target_checkers && acknowledged) {
        return execute_init_cycle();
    }

    fprintf(stderr, "refusing: active mode requires the exact target and acknowledgement\n");
    usage(stderr, argv[0]);
    return 2;
}

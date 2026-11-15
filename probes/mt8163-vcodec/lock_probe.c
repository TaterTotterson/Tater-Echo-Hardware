#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define VCODEC_NODE "/dev/Vcodec"
#define VCODEC_LOCKHW _IOW('M', 0x0d, unsigned int)
#define VCODEC_UNLOCKHW _IOW('M', 0x33, unsigned int)
#define VAL_DRIVER_TYPE_H264_DEC 6U
#define VAL_RESULT_INVALID_ISR 4
#define VCODEC_LEGACY_IOCTL_OK 255

struct compat_val_hw_lock {
    uint32_t handle;
    uint32_t handle_size;
    uint32_t lock;
    uint32_t timeout_ms;
    uint32_t reserved;
    uint32_t reserved_size;
    uint32_t driver_type;
    uint8_t secure_instance;
    uint8_t padding[3];
};

_Static_assert(sizeof(struct compat_val_hw_lock) == 32,
               "unexpected ARM32 Vcodec lock ABI size");

static int describe(void) {
    printf("{\"node\":\"%s\",\"lock_request\":\"0x%08lx\","
           "\"unlock_request\":\"0x%08lx\",\"lock_size\":%zu,"
           "\"driver_type\":%u}\n",
           VCODEC_NODE, (unsigned long)VCODEC_LOCKHW,
           (unsigned long)VCODEC_UNLOCKHW,
           sizeof(struct compat_val_hw_lock), VAL_DRIVER_TYPE_H264_DEC);
    return 0;
}

static int execute_lock_cycle(void) {
    struct compat_val_hw_lock request;
    struct stat node_stat;
    sigset_t blocked;
    sigset_t previous;
    volatile uint32_t handle_marker = 0x54415452U;
    int fd = -1;
    int lock_result;
    int unlock_result;
    int locked = 0;
    int result = 2;

    if (stat(VCODEC_NODE, &node_stat) != 0 || !S_ISCHR(node_stat.st_mode)) {
        fprintf(stderr, "target check failed: %s is not a character device\n",
                VCODEC_NODE);
        return result;
    }

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        fprintf(stderr, "signal guard failed: %s\n", strerror(errno));
        return result;
    }

    memset(&request, 0, sizeof(request));
    request.handle = (uint32_t)(uintptr_t)&handle_marker;
    request.handle_size = sizeof(handle_marker);
    request.timeout_ms = 1000U;
    request.driver_type = VAL_DRIVER_TYPE_H264_DEC;

    fd = open(VCODEC_NODE, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        goto cleanup_signals;
    }
    errno = 0;
    lock_result = ioctl(fd, VCODEC_LOCKHW, &request);
    if (lock_result != 0 && lock_result != VAL_RESULT_INVALID_ISR &&
        lock_result != VCODEC_LEGACY_IOCTL_OK) {
        fprintf(stderr, "VDEC lock failed: return=%d errno=%d (%s)\n",
                lock_result, errno, strerror(errno));
        goto cleanup_fd;
    }
    locked = 1;
    if (lock_result == VAL_RESULT_INVALID_ISR)
        puts("VDEC lock returned the MT8163 first-use status after acquisition");
    puts("VDEC hardware lock acquired; holding for two seconds");
    fflush(stdout);
    sleep(2);
    errno = 0;
    unlock_result = ioctl(fd, VCODEC_UNLOCKHW, &request);
    if (unlock_result != 0 && unlock_result != VCODEC_LEGACY_IOCTL_OK) {
        fprintf(stderr, "VDEC unlock failed: return=%d errno=%d (%s)\n",
                unlock_result, errno, strerror(errno));
        goto cleanup_fd;
    }
    locked = 0;
    puts("VDEC hardware lock released");
    result = 0;

cleanup_fd:
    if (locked) {
        errno = 0;
        unlock_result = ioctl(fd, VCODEC_UNLOCKHW, &request);
        if (unlock_result != 0 &&
            unlock_result != VCODEC_LEGACY_IOCTL_OK) {
            fprintf(stderr,
                    "cleanup unlock failed: return=%d errno=%d (%s)\n",
                    unlock_result, errno, strerror(errno));
            result = 2;
        }
    }
    close(fd);
cleanup_signals:
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0)
        return 2;
    return result;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--describe") == 0)
        return describe();
    if (argc == 8 && strcmp(argv[1], "--execute-lock-cycle") == 0 &&
        strcmp(argv[2], "--codec") == 0 &&
        strcmp(argv[3], "h264-dec") == 0 &&
        strcmp(argv[4], "--target") == 0 &&
        strcmp(argv[5], "checkers") == 0 &&
        strcmp(argv[6], "--acknowledge-vdec-power-cycle") == 0 &&
        strcmp(argv[7], "bounded-no-register-access") == 0)
        return execute_lock_cycle();

    fprintf(stderr, "refusing: use --describe or the complete guarded "
                    "Checkers invocation\n");
    return 2;
}

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define JPEG_NODE "/proc/mtk_jpeg"
#define JPEG_ENCODER_INIT ((unsigned long)0x0000780b)
#define JPEG_ENCODER_DEINIT ((unsigned long)0x0000780e)

int main(int argc, char **argv) {
    sigset_t blocked;
    sigset_t previous;
    int fd;
    int initialized = 0;
    int result = 2;

    if (argc != 6 || strcmp(argv[1], "--hold-encoder-clock") != 0 ||
        strcmp(argv[2], "--seconds") != 0 || strcmp(argv[3], "15") != 0 ||
        strcmp(argv[4], "--target") != 0 ||
        strcmp(argv[5], "checkers") != 0) {
        fprintf(stderr, "refusing: guarded diagnostic invocation required\n");
        return result;
    }
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        return result;
    }
    fd = open(JPEG_NODE, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        goto cleanup_signals;
    }
    if (ioctl(fd, JPEG_ENCODER_INIT, 0UL) != 0) {
        fprintf(stderr, "encoder INIT failed: %s\n", strerror(errno));
        goto cleanup_fd;
    }
    initialized = 1;
    puts("encoder clock held");
    fflush(stdout);
    sleep(15);
    result = 0;

cleanup_fd:
    if (initialized && ioctl(fd, JPEG_ENCODER_DEINIT, 0UL) != 0) {
        fprintf(stderr, "encoder DEINIT failed: %s\n", strerror(errno));
        result = 2;
    }
    close(fd);
cleanup_signals:
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        return 2;
    }
    return result;
}

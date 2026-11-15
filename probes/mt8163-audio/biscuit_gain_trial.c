/* SPDX-License-Identifier: MIT
 * Owner-approved, bounded Biscuit gain-staging trial. No PCM opens or samples.
 * The only writable controls are the four ADC PGA and digital volume pairs.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sound/asound.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

enum { DIGITAL = 0, PGA = 1, CONTROLS = 8, TRIAL_SECONDS = 300 };
struct control {
    struct snd_ctl_elem_id id;
    long original[2];
};
static volatile sig_atomic_t stop_requested;
static struct control controls[CONTROLS];
static bool fake;
static bool hardware40;
static long fake_values[CONTROLS][2];
static int fake_writes, fail_at = -1, order[64], order_count;

static void stop_trial(int signo) { (void)signo; stop_requested = 1; }

/* ALSA's volsw_sx mapping uses min=0x28, mask=0x7f. It does NOT use
 * the misleading negative-step TLV scale in this vendor driver.
 * Digital control 88 -> register 0 (0dB); 68 -> 108 (-10dB).
 * PGA control 20 is +10dB, in half-dB units. No AGC is enabled.
 */
static long target_value(int index) {
    if (hardware40) return index % 2 == DIGITAL ? 88 : 80;
    return index % 2 == DIGITAL ? 68 : 20;
}
static long baseline_value(int index) { return index % 2 == DIGITAL ? 88 : 0; }

static bool hardware40_daemon_ready(void) {
    const char *pid_text = getenv("TATER_GAIN_TRIAL_DAEMON_PID");
    char *end = NULL, path[128], executable[512], environment[65536];
    if (!pid_text) return false;
    long pid = strtol(pid_text, &end, 10);
    if (!*pid_text || *end || pid <= 1 || pid > 2147483647) return false;
    snprintf(path, sizeof(path), "/proc/%ld/exe", pid);
    ssize_t size = readlink(path, executable, sizeof(executable) - 1);
    if (size < 0) return false;
    executable[size] = 0;
    const char *base = strrchr(executable, '/');
    if (strncmp(executable, "/tmp/tater-biscuit-hw40.", strlen("/tmp/tater-biscuit-hw40.")) || !base ||
        strcmp(base, "/tater-echo-hw40-trial")) return false;
    snprintf(path, sizeof(path), "/proc/%ld/environ", pid);
    int envfd = open(path, O_RDONLY | O_CLOEXEC);
    if (envfd < 0) return false;
    size = read(envfd, environment, sizeof(environment) - 1);
    close(envfd);
    if (size <= 0 || size >= (ssize_t)sizeof(environment) - 1) return false;
    environment[size] = 0;
    bool software_zero = false, explicit_trial = false;
    for (size_t i = 0; i < (size_t)size;) {
        const char *value = environment + i;
        size_t length = strnlen(value, (size_t)size - i);
        if (length == (size_t)size - i) return false;
        if (!strcmp(value, "MIC_GAIN_DB=0")) software_zero = true;
        if (!strcmp(value, "TATER_BISCUIT_HW_GAIN_TRIAL=40")) explicit_trial = true;
        i += length + 1;
    }
    return software_zero && explicit_trial;
}

static int read_pair(int fd, int index, long out[2]) {
    if (fake) {
        memcpy(out, fake_values[index], 2 * sizeof(long));
        return 0;
    }
    struct snd_ctl_elem_value value = {0};
    value.id = controls[index].id;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &value)) return -1;
    out[0] = value.value.integer.value[0];
    out[1] = value.value.integer.value[1];
    return 0;
}

static int write_pair(int fd, int index, const long pair[2]) {
    if (fake) {
        if (order_count < 64) order[order_count++] = index;
        if (fake_writes++ == fail_at) {
            /* Simulate a driver failing after writing the first channel. */
            fake_values[index][0] = pair[0];
            return -1;
        }
        memcpy(fake_values[index], pair, 2 * sizeof(long));
    } else {
        struct snd_ctl_elem_value value = {0};
        value.id = controls[index].id;
        value.value.integer.value[0] = pair[0];
        value.value.integer.value[1] = pair[1];
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value)) return -1;
    }
    long check[2];
    return read_pair(fd, index, check) || check[0] != pair[0] || check[1] != pair[1] ? -1 : 0;
}

static int apply_trial(int fd) {
    /* Attenuate EVERY converter before increasing ANY analog gain. */
    for (int kind = DIGITAL; kind <= PGA; ++kind) {
        for (int i = kind; i < CONTROLS; i += 2) {
            long value[2] = {target_value(i), target_value(i)};
            if (stop_requested || write_pair(fd, i, value)) return -1;
        }
    }
    return 0;
}

static int verify_restore_api(int fd) {
    /* Vendor info advertises digital max=64, but its signed get/put callback
     * represents the existing 0dB value as 88. Test the exact no-op restore
     * operation before making any gain change. This is not a general range
     * override: target, callback shape and both baseline values are pinned. */
    for (int i = 0; i < CONTROLS; ++i)
        if (stop_requested || write_pair(fd, i, controls[i].original)) return -1;
    return 0;
}

static int restore(int fd) {
    int errors = 0;
    /* Restore ALL preamps first, then remove digital attenuation. Continue
     * through errors and retry each write: even a failed apply may have
     * changed one channel. Never overwrite unrelated controls. */
    for (int kind = PGA; kind >= DIGITAL; --kind) {
        for (int i = kind; i < CONTROLS; i += 2) {
            bool restored = false;
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (!write_pair(fd, i, controls[i].original)) { restored = true; break; }
            }
            if (!restored) {
                fprintf(stderr, "restore failed for ADC_%c %s\n", 'A' + i / 2,
                        kind == PGA ? "PGA" : "digital volume");
                ++errors;
            }
        }
    }
    return errors ? -1 : 0;
}

static bool values_restored(void) {
    for (int i = 0; i < CONTROLS; ++i)
        for (int j = 0; j < 2; ++j)
            if (fake_values[i][j] != controls[i].original[j]) return false;
    return true;
}

static void fake_reset(void) {
    fake = true;
    fake_writes = order_count = 0;
    fail_at = -1;
    stop_requested = 0;
    for (int i = 0; i < CONTROLS; ++i)
        for (int j = 0; j < 2; ++j)
            fake_values[i][j] = controls[i].original[j] = baseline_value(i);
}

static int self_test(void) {
    fake_reset();
    if (verify_restore_api(-1) || !values_restored()) return 1;
    fake_reset();
    if (((target_value(DIGITAL) + 0x28) & 0x7f) != 108 ||
        target_value(PGA) + (((target_value(DIGITAL) + 0x28) & 0x7f) - 128) != 0)
        return 1;
    if (apply_trial(-1) || order_count != 8) return 1;
    for (int i = 0; i < 4; ++i) if (order[i] != i * 2 || order[i + 4] != i * 2 + 1) return 1;
    order_count = 0;
    if (restore(-1) || !values_restored()) return 1;
    for (int i = 0; i < 4; ++i) if (order[i] != i * 2 + 1 || order[i + 4] != i * 2) return 1;
    for (int failure = 0; failure < CONTROLS; ++failure) {
        fake_reset();
        fail_at = failure;
        if (!apply_trial(-1) || restore(-1) || !values_restored()) return 1;
    }
    fake_reset();
    if (apply_trial(-1)) return 1;
    fail_at = fake_writes;
    if (restore(-1) || !values_restored()) return 1;
    fake_reset();
    stop_requested = 1;
    if (!apply_trial(-1) || restore(-1) || !values_restored()) return 1;
    fake_reset();
    hardware40 = true;
    if (target_value(DIGITAL) != 88 || target_value(PGA) != 80 || apply_trial(-1)) return 1;
    for (int i = 0; i < CONTROLS; ++i)
        for (int j = 0; j < 2; ++j)
            if (fake_values[i][j] != target_value(i)) return 1;
    if (restore(-1) || !values_restored()) return 1;
    hardware40 = false;
    puts("gain trial self-test passed: mapping, ordering, partial writes, rollback retry, cancellation");
    return 0;
}

static int inventory(int fd) {
    struct snd_ctl_card_info card = {0};
    if (ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &card) || strcmp((char *)card.id, "mtsndcard")) return -1;
    struct snd_ctl_elem_list list = {0};
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) || list.count > 4096) return -1;
    struct snd_ctl_elem_id *ids = calloc(list.count ? list.count : 1, sizeof(*ids));
    if (!ids) return -1;
    unsigned count = list.count;
    list.space = count;
    list.pids = ids;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) || list.used > count) { free(ids); return -1; }
    int error = 0;
    for (int i = 0; i < CONTROLS; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "ADC_%c %s", 'A' + i / 2,
                 i % 2 == DIGITAL ? "Digital Volume Control" : "MICPGA Volume Ctrl");
        unsigned matches = 0;
        for (unsigned j = 0; j < list.used; ++j) {
            if (!strncmp((char *)ids[j].name, name, sizeof(ids[j].name))) {
                controls[i].id = ids[j];
                ++matches;
            }
        }
        struct snd_ctl_elem_info info = {0};
        info.id = controls[i].id;
        if (matches != 1 || ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) ||
            info.type != SNDRV_CTL_ELEM_TYPE_INTEGER || info.count != 2 ||
            !(info.access & SNDRV_CTL_ELEM_ACCESS_READ) || !(info.access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
            info.value.integer.min != 0 || info.value.integer.max != (i % 2 == DIGITAL ? 64 : 80) ||
            read_pair(fd, i, controls[i].original) ||
            controls[i].original[0] != baseline_value(i) || controls[i].original[1] != baseline_value(i)) {
            fprintf(stderr, "refusing unexpected control or baseline: %s (matches=%u type=%u count=%u access=%u min=%ld max=%ld values=%ld,%ld)\n",
                    name, matches, info.type, info.count, info.access, info.value.integer.min,
                    info.value.integer.max, controls[i].original[0], controls[i].original[1]);
            error = -1;
            break;
        }
    }
    free(ids);
    return error;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test();
    hardware40 = argc == 2 && !strcmp(argv[1], "--owner-approved-hardware40-trial");
    if (argc != 2 || (!hardware40 && strcmp(argv[1], "--owner-approved-10db-trial"))) {
        fputs("usage: biscuit_gain_trial --owner-approved-10db-trial | --owner-approved-hardware40-trial\n", stderr);
        return 2;
    }
    struct utsname kernel;
    char hostname[128] = {0};
    if (geteuid() || gethostname(hostname, sizeof(hostname) - 1) || uname(&kernel) ||
        strcmp(hostname, "tater-biscuit") || strcmp(kernel.release, "3.18.19-bt")) {
        fputs("refusing: expected owner-approved Biscuit on 3.18.19-bt\n", stderr);
        return 1;
    }
    if (hardware40 && !hardware40_daemon_ready()) {
        fputs("refusing: hardware40 requires the guarded temporary daemon with MIC_GAIN_DB=0\n", stderr);
        return 1;
    }
    /* A per-boot exclusive file prevents overlapping trials; no settings are
     * persisted. On an abnormal exit, leave this lock as an investigation guard. */
    int lock = open("/run/tater-biscuit-gain-trial.lock", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (lock < 0) { perror("trial lock"); return 1; }
    close(lock);
    int fd = open("/dev/snd/controlC0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || inventory(fd)) {
        if (fd >= 0) close(fd);
        unlink("/run/tater-biscuit-gain-trial.lock");
        return 1;
    }
    struct sigaction action = {0};
    action.sa_handler = stop_trial;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
    /* Real-time alarm bounds the trial even if the host/USB disappears. */
    alarm(TRIAL_SECONDS);
    int failed = verify_restore_api(fd);
    if (!failed) failed = apply_trial(fd);
    if (!failed) {
        printf("{\"phase\":\"trial\",\"analog_delta_db\":%d,\"digital_delta_db\":%d,\"software_managed_by_runner\":%s,\"deadline_seconds\":300,\"samples_saved\":false}\n",
               hardware40 ? 40 : 10, hardware40 ? 0 : -10, hardware40 ? "true" : "false");
        fflush(stdout);
        while (!stop_requested) {
            struct timespec tick = {.tv_sec = 1};
            nanosleep(&tick, NULL);
            if (hardware40 && !hardware40_daemon_ready()) {
                failed = 1;
                stop_requested = 1;
            }
            /* Another writer or a capture reset invalidates the comparison. */
            for (int i = 0; i < CONTROLS; ++i) {
                long value[2];
                if (read_pair(fd, i, value) || value[0] != target_value(i) || value[1] != target_value(i)) {
                    failed = 1;
                    stop_requested = 1;
                    break;
                }
            }
        }
    }
    alarm(0);
    int restored = restore(fd);
    printf("{\"phase\":\"restored\",\"verified\":%s,\"trial_error\":%s}\n",
           restored ? "false" : "true", failed ? "true" : "false");
    close(fd);
    if (!restored) unlink("/run/tater-biscuit-gain-trial.lock");
    return failed || restored ? 1 : 0;
}

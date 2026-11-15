// SPDX-License-Identifier: MIT
// Bounded Checkers CPU/memory headroom probe. It touches only anonymous memory
// and read-only proc/sysfs metadata; no hardware device is opened.

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_BYTES (32U * 1024U * 1024U)
#define PHASE_NS UINT64_C(1000000000)

static uint64_t monotonic_ns(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static long read_long(const char *path, const char *prefix)
{
    FILE *stream = fopen(path, "re");
    char line[256];
    long value = -1;

    if (stream == NULL)
        return -1;
    while (fgets(line, sizeof(line), stream) != NULL) {
        char *start = line;

        if (prefix != NULL) {
            size_t length = strlen(prefix);

            if (strncmp(line, prefix, length) != 0)
                continue;
            start = line + length;
        }
        errno = 0;
        value = strtol(start, NULL, 10);
        if (errno == 0)
            break;
        value = -1;
    }
    fclose(stream);
    return value;
}

static int file_contains(const char *path, const char *needle)
{
    FILE *stream = fopen(path, "re");
    char line[256];

    if (stream == NULL)
        return 0;
    while (fgets(line, sizeof(line), stream) != NULL) {
        if (strstr(line, needle) != NULL) {
            fclose(stream);
            return 1;
        }
    }
    fclose(stream);
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --target checkers --acknowledge bounded-load\n",
            program);
    exit(2);
}

int main(int argc, char **argv)
{
    unsigned char *source;
    unsigned char *destination;
    uint64_t start;
    uint64_t elapsed;
    uint64_t memory_iterations = 0;
    uint64_t cpu_iterations = 0;
    volatile uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    volatile uint64_t checksum = 0;
    long temperature_before;
    long temperature_after;
    long frequency_before;
    long frequency_after;
    long available_before;
    long available_after;
    double memory_gib_per_second;
    double cpu_million_iterations;

    if (argc != 5 || strcmp(argv[1], "--target") != 0 ||
        strcmp(argv[2], "checkers") != 0 ||
        strcmp(argv[3], "--acknowledge") != 0 ||
        strcmp(argv[4], "bounded-load") != 0)
        usage(argv[0]);

    if (!file_contains("/proc/asound/cards", "mt-snd-card") ||
        !file_contains("/proc/asound/card0/pcm22c/info", "TLV320AIC3101")) {
        fprintf(stderr, "refusing target without the Checkers audio topology\n");
        return 1;
    }

    temperature_before =
        read_long("/sys/class/thermal/thermal_zone10/temp", NULL);
    frequency_before = read_long(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", NULL);
    available_before = read_long("/proc/meminfo", "MemAvailable:");

    if (posix_memalign((void **)&source, 64, BUFFER_BYTES) != 0 ||
        posix_memalign((void **)&destination, 64, BUFFER_BYTES) != 0) {
        fprintf(stderr, "unable to allocate bounded anonymous buffers\n");
        return 1;
    }
    memset(source, 0x5a, BUFFER_BYTES);
    memset(destination, 0, BUFFER_BYTES);

    start = monotonic_ns();
    do {
        size_t offset = (size_t)((memory_iterations * 4096U) % BUFFER_BYTES);

        source[offset] ^= (unsigned char)memory_iterations;
        memcpy(destination, source, BUFFER_BYTES);
        checksum += destination[offset];
        memory_iterations++;
        elapsed = monotonic_ns() - start;
    } while (elapsed < PHASE_NS);
    memory_gib_per_second =
        ((double)memory_iterations * (double)BUFFER_BYTES) /
        ((double)elapsed / 1e9) / (1024.0 * 1024.0 * 1024.0);

    start = monotonic_ns();
    elapsed = 0;
    do {
        uint64_t value = state;

        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
        state = value;
        cpu_iterations++;
        if ((cpu_iterations & UINT64_C(0xffff)) == 0)
            elapsed = monotonic_ns() - start;
    } while (elapsed < PHASE_NS);
    elapsed = monotonic_ns() - start;
    cpu_million_iterations =
        ((double)cpu_iterations / 1000000.0) / ((double)elapsed / 1e9);

    temperature_after =
        read_long("/sys/class/thermal/thermal_zone10/temp", NULL);
    frequency_after = read_long(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", NULL);
    available_after = read_long("/proc/meminfo", "MemAvailable:");

    printf("{");
    printf("\"schema_version\":1,");
    printf("\"target\":\"checkers\",");
    printf("\"memory\":{\"buffer_mib\":32,\"iterations\":%" PRIu64
           ",\"gib_per_second\":%.3f},",
           memory_iterations, memory_gib_per_second);
    printf("\"cpu\":{\"xorshift_million_iterations_per_second\":%.3f},",
           cpu_million_iterations);
    printf("\"thermal_millicelsius\":{\"before\":%ld,\"after\":%ld},",
           temperature_before, temperature_after);
    printf("\"cpu0_khz\":{\"before\":%ld,\"after\":%ld},",
           frequency_before, frequency_after);
    printf("\"mem_available_kib\":{\"before\":%ld,"
           "\"with_buffers_resident\":%ld},",
           available_before, available_after);
    printf("\"checksum\":%" PRIu64 ",", (uint64_t)(checksum ^ state));
    printf("\"duration_seconds\":2,");
    printf("\"device_opened\":false,\"persistent_write\":false");
    printf("}\n");

    free(destination);
    free(source);
    return 0;
}

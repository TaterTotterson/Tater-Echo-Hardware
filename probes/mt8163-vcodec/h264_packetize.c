// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_INPUT_BYTES (64U * 1024U * 1024U)
#define MAX_PACKET_BYTES (4U * 1024U * 1024U)

static int write_all(int fd, const uint8_t *contents, size_t size) {
    size_t written = 0;

    while (written < size) {
        ssize_t result = write(fd, contents + written, size - written);

        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static int write_record(int fd, const uint8_t *contents, size_t size,
                        uint32_t protocol, uint64_t pts_us) {
    uint8_t encoded[12] = {0};
    size_t header_size = protocol == 2U ? sizeof(encoded) : 4U;
    uint32_t byte_index;

    if (size > UINT32_MAX || size > MAX_PACKET_BYTES)
        return -1;
    encoded[0] = (uint8_t)size;
    encoded[1] = (uint8_t)(size >> 8);
    encoded[2] = (uint8_t)(size >> 16);
    encoded[3] = (uint8_t)(size >> 24);
    for (byte_index = 0; byte_index < 8U; ++byte_index)
        encoded[4U + byte_index] =
            (uint8_t)(pts_us >> (byte_index * 8U));
    return write_all(fd, encoded, header_size) == 0 &&
           (size == 0 || write_all(fd, contents, size) == 0) ? 0 : -1;
}

static int start_code(const uint8_t *contents, size_t size, size_t offset,
                      size_t *prefix_size) {
    if (offset + 5U <= size && contents[offset] == 0 &&
        contents[offset + 1U] == 0 && contents[offset + 2U] == 0 &&
        contents[offset + 3U] == 1) {
        *prefix_size = 4U;
        return 1;
    }
    if (offset + 4U <= size && contents[offset] == 0 &&
        contents[offset + 1U] == 0 && contents[offset + 2U] == 1) {
        *prefix_size = 3U;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct stat status;
    uint8_t *contents = NULL;
    size_t size;
    size_t offset;
    size_t record_start = 0;
    uint32_t records = 0;
    uint32_t protocol = 1U;
    uint32_t fps = 0;
    const char *input_path;
    const char *output_path;
    int input = -1;
    int output = -1;
    int failed = 0;

    if (argc == 5 && strcmp(argv[1], "--v2") == 0) {
        char *end = NULL;
        unsigned long parsed;

        errno = 0;
        parsed = strtoul(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' ||
            parsed == 0 || parsed > 120U) {
            fputs("v2 FPS must be between 1 and 120\n", stderr);
            return 2;
        }
        protocol = 2U;
        fps = (uint32_t)parsed;
        input_path = argv[3];
        output_path = argv[4];
    } else if (argc == 3) {
        input_path = argv[1];
        output_path = argv[2];
    } else {
        fputs("usage: tater-h264-packetize [--v2 FPS] INPUT.h264 OUTPUT.tvp\n",
              stderr);
        return 2;
    }
    if (strcmp(input_path, output_path) == 0) {
        fputs("input and output must differ\n", stderr);
        return 2;
    }
    input = open(input_path, O_RDONLY);
    if (input < 0 || fstat(input, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        (uint64_t)status.st_size > MAX_INPUT_BYTES) {
        fputs("input must be a non-empty regular file at most 64 MiB\n",
              stderr);
        if (input >= 0)
            close(input);
        return 2;
    }
    size = (size_t)status.st_size;
    contents = malloc(size);
    if (contents == NULL) {
        close(input);
        return 1;
    }
    {
        size_t received = 0;

        while (received < size) {
            ssize_t result = read(input, contents + received,
                                  size - received);

            if (result > 0) {
                received += (size_t)result;
                continue;
            }
            if (result < 0 && errno == EINTR)
                continue;
            failed = 1;
            break;
        }
    }
    close(input);
    if (failed) {
        free(contents);
        return 1;
    }
    output = open(output_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        fputs("refusing to overwrite packet output\n", stderr);
        free(contents);
        return 2;
    }

    for (offset = 0; offset + 4U < size; ++offset) {
        size_t prefix_size = 0;
        uint8_t nal_type;

        if (!start_code(contents, size, offset, &prefix_size))
            continue;
        nal_type = contents[offset + prefix_size] & 0x1fU;
        if (nal_type != 9U)
            continue;
        if (records != 0) {
            if (write_record(
                    output, contents + record_start,
                    offset - record_start, protocol,
                    protocol == 2U ?
                        ((uint64_t)(records - 1U) * 1000000U) / fps : 0U) !=
                0) {
                failed = 1;
                break;
            }
        }
        ++records;
        if (records > 1U)
            record_start = offset;
        offset += prefix_size;
    }
    if (!failed && records != 0 &&
        write_record(
            output, contents + record_start, size - record_start,
            protocol,
            protocol == 2U ?
                ((uint64_t)(records - 1U) * 1000000U) / fps : 0U) != 0)
        failed = 1;
    if (!failed && write_record(output, NULL, 0, protocol, 0U) != 0)
        failed = 1;
    if (close(output) != 0)
        failed = 1;
    free(contents);
    if (failed || records == 0) {
        unlink(output_path);
        fputs("input must contain bounded AUD-framed access units\n",
              stderr);
        return 1;
    }
    printf("{\"access_units\":%u,\"protocol\":%u}\n", records,
           protocol);
    return 0;
}

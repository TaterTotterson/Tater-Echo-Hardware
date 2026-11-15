#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define ION_NODE "/dev/ion"
#define ION_HEAP_MULTIMEDIA_MASK (1U << 10)
#define ION_CMD_SYSTEM 0U
#define ION_CMD_MULTIMEDIA 1U
#define ION_MM_CONFIG_BUFFER 0U
#define ION_SYS_GET_PHYS 1U
#define M4U_PORT_JPGDEC_WDMA 37U
#define M4U_PORT_JPGDEC_BSDMA 38U
#define BUFFER_SIZE 4096U
#define JPEG_NODE "/proc/mtk_jpeg"
#define JPEG_INIT ((unsigned long)0x00007801)
#define JPEG_CONFIG_ARM32 ((unsigned long)0x40d07802)
#define JPEG_START ((unsigned long)0x00007804)
#define JPEG_WAIT_ARM32 ((unsigned long)0xc0087805)
#define JPEG_DEINIT ((unsigned long)0x00007806)

#ifndef INIT_DIAGNOSTIC_HOLD_SECONDS
#define INIT_DIAGNOSTIC_HOLD_SECONDS 0
#endif

#ifndef START_DIAGNOSTIC_HOLD_SECONDS
#define START_DIAGNOSTIC_HOLD_SECONDS 0
#endif

/* Original 8x8 grayscale baseline fixture generated with libjpeg-turbo. */
static const uint8_t gray_fixture[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
    0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09,
    0x09, 0x08, 0x0a, 0x0c, 0x14, 0x0d, 0x0c, 0x0b, 0x0b, 0x0c, 0x19, 0x12,
    0x13, 0x0f, 0x14, 0x1d, 0x1a, 0x1f, 0x1e, 0x1d, 0x1a, 0x1c, 0x1c, 0x20,
    0x24, 0x2e, 0x27, 0x20, 0x22, 0x2c, 0x23, 0x1c, 0x1c, 0x28, 0x37, 0x29,
    0x2c, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1f, 0x27, 0x39, 0x3d, 0x38, 0x32,
    0x3c, 0x2e, 0x33, 0x34, 0x32, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x08,
    0x00, 0x08, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01,
    0x00, 0x00, 0x3f, 0x00, 0x2b, 0xff, 0xd9,
};

struct ion_allocation_data {
    uint32_t len;
    uint32_t align;
    uint32_t heap_id_mask;
    uint32_t flags;
    int32_t handle;
};

struct ion_handle_data {
    int32_t handle;
};

struct ion_fd_data {
    int32_t handle;
    int32_t fd;
};

struct ion_custom_data {
    uint32_t cmd;
    uint32_t arg;
};

struct ion_mm_config_buffer_param {
    int32_t handle;
    int32_t module_id;
    uint32_t security;
    uint32_t coherent;
    uint32_t reserve_iova_start;
    uint32_t reserve_iova_end;
};

struct ion_mm_data {
    uint32_t mm_cmd;
    union {
        struct ion_mm_config_buffer_param config_buffer_param;
        uint8_t reserved[68];
    } data;
};

struct ion_sys_get_phys_param {
    int32_t handle;
    uint32_t phy_addr;
    uint32_t len;
};

struct ion_sys_data {
    uint32_t sys_cmd;
    union {
        struct ion_sys_get_phys_param get_phys_param;
        uint8_t reserved[48];
    } data;
};

struct private_buffer {
    int32_t handle;
    int fd;
    void *mapping;
    uint32_t mva;
    uint32_t length;
};

struct jpeg_dec_config_arm32 {
    uint32_t decode_mode;
    uint32_t register_operation_mode;
    uint32_t register_dump_enabled;
    uint32_t total_mcu;
    uint32_t component_zero_data_units;
    uint32_t membership_list;
    uint8_t luma_horizontal_decimate;
    uint8_t luma_vertical_decimate;
    uint8_t chroma_horizontal_decimate;
    uint8_t chroma_vertical_decimate;
    uint32_t source_base;
    uint32_t source_size;
    uint32_t source_write_pointer;
    uint32_t output_bank_zero[3];
    uint32_t output_bank_one[3];
    uint32_t component_count;
    uint32_t component_id[3];
    uint32_t horizontal_sampling[3];
    uint32_t vertical_sampling[3];
    uint32_t quantization_table[3];
    uint32_t dma_mcu_in_group;
    uint32_t dma_group_count;
    uint32_t dma_last_mcu_count;
    uint32_t gmc_enabled;
    uint32_t component_image_stride[3];
    uint32_t component_memory_stride[3];
    uint32_t component_tile_stride[3];
    uint32_t mcu_per_row;
    uint32_t pause_row_enabled;
    uint32_t pause_row_count;
    uint32_t pause_mcu;
    uint32_t tile_buffer_row_count;
    uint32_t buffer_y_address;
    uint32_t buffer_cb_address;
    uint32_t buffer_cr_address;
    uint32_t buffer_y_row_size;
    uint32_t buffer_chroma_row_size;
};

struct jpeg_dec_output_arm32 {
    int32_t timeout_ms;
    uint32_t result_address;
};

#define ION_IOC_ALLOC _IOWR('I', 0, struct ion_allocation_data)
#define ION_IOC_FREE _IOWR('I', 1, struct ion_handle_data)
#define ION_IOC_MAP _IOWR('I', 2, struct ion_fd_data)
#define ION_IOC_CUSTOM _IOWR('I', 6, struct ion_custom_data)
#define ION_IOC_SYNC _IOWR('I', 7, struct ion_fd_data)

_Static_assert(sizeof(struct ion_allocation_data) == 20,
               "ARM32 ION allocation ABI must remain 20 bytes");
_Static_assert(sizeof(struct ion_custom_data) == 8,
               "ARM32 ION custom ABI must remain 8 bytes");
_Static_assert(sizeof(struct ion_mm_data) == 72,
               "ARM32 MediaTek multimedia ABI must remain 72 bytes");
_Static_assert(sizeof(struct ion_sys_data) == 52,
               "ARM32 MediaTek system ABI must remain 52 bytes");
_Static_assert(sizeof(struct jpeg_dec_config_arm32) == 208,
               "ARM32 JPEG config ABI must remain 208 bytes");
_Static_assert(sizeof(struct jpeg_dec_output_arm32) == 8,
               "ARM32 JPEG wait ABI must remain eight bytes");

static void release_buffer(int ion_fd, struct private_buffer *buffer) {
    if (buffer->mapping != MAP_FAILED && buffer->mapping != NULL) {
        (void)munmap(buffer->mapping, buffer->length);
    }
    if (buffer->fd >= 0) {
        (void)close(buffer->fd);
    }
    if (buffer->handle > 0) {
        struct ion_handle_data free_data = {.handle = buffer->handle};
        (void)ioctl(ion_fd, ION_IOC_FREE, &free_data);
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->fd = -1;
    buffer->mapping = MAP_FAILED;
}

static int configure_port(int ion_fd, int32_t handle, uint32_t port) {
    struct ion_mm_data mm_data = {0};
    struct ion_custom_data custom = {0};

    mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
    mm_data.data.config_buffer_param.handle = handle;
    mm_data.data.config_buffer_param.module_id = (int32_t)port;
    custom.cmd = ION_CMD_MULTIMEDIA;
    custom.arg = (uint32_t)(uintptr_t)&mm_data;
    return ioctl(ion_fd, ION_IOC_CUSTOM, &custom);
}

static int get_mva(int ion_fd, int32_t handle, uint32_t *mva,
                   uint32_t *length) {
    struct ion_sys_data sys_data = {0};
    struct ion_custom_data custom = {0};

    sys_data.sys_cmd = ION_SYS_GET_PHYS;
    sys_data.data.get_phys_param.handle = handle;
    custom.cmd = ION_CMD_SYSTEM;
    custom.arg = (uint32_t)(uintptr_t)&sys_data;
    if (ioctl(ion_fd, ION_IOC_CUSTOM, &custom) != 0) {
        return -1;
    }
    *mva = sys_data.data.get_phys_param.phy_addr;
    *length = sys_data.data.get_phys_param.len;
    return 0;
}

static int allocate_buffer(int ion_fd, uint32_t port,
                           struct private_buffer *buffer) {
    struct ion_allocation_data allocation = {
        .len = BUFFER_SIZE,
        .align = BUFFER_SIZE,
        .heap_id_mask = ION_HEAP_MULTIMEDIA_MASK,
        .flags = 0,
        .handle = 0,
    };
    struct ion_fd_data map_data = {0};

    if (ioctl(ion_fd, ION_IOC_ALLOC, &allocation) != 0) {
        return -1;
    }
    buffer->handle = allocation.handle;
    buffer->length = BUFFER_SIZE;
    if (configure_port(ion_fd, buffer->handle, port) != 0) {
        return -1;
    }
    if (get_mva(ion_fd, buffer->handle, &buffer->mva, &buffer->length) != 0) {
        return -1;
    }
    map_data.handle = buffer->handle;
    if (ioctl(ion_fd, ION_IOC_MAP, &map_data) != 0) {
        return -1;
    }
    buffer->fd = map_data.fd;
    buffer->mapping = mmap(NULL, buffer->length, PROT_READ | PROT_WRITE,
                           MAP_SHARED, buffer->fd, 0);
    if (buffer->mapping == MAP_FAILED) {
        return -1;
    }
    return 0;
}

static int sync_buffer(int ion_fd, const struct private_buffer *buffer) {
    struct ion_fd_data sync_data = {
        .handle = buffer->handle,
        .fd = buffer->fd,
    };
    return ioctl(ion_fd, ION_IOC_SYNC, &sync_data);
}

static uint32_t fnv1a32(const uint8_t *data, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < length; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

static int decode_fixture(int ion_fd, const struct private_buffer *source,
                          const struct private_buffer *output,
                          uint32_t *decode_result, uint32_t *output_hash,
                          uint32_t *changed_bytes) {
    struct jpeg_dec_config_arm32 config = {0};
    struct jpeg_dec_output_arm32 wait_data = {0};
    sigset_t blocked;
    sigset_t previous;
    uint8_t *source_bytes = source->mapping;
    uint8_t *output_bytes = output->mapping;
    int jpeg_fd = -1;
    int initialized = 0;
    int signals_blocked = 0;
    int result = -1;

    memset(source_bytes, 0, source->length);
    memcpy(source_bytes, gray_fixture, sizeof(gray_fixture));
    memset(output_bytes, 0x5a, output->length);
    if (sync_buffer(ion_fd, source) != 0 || sync_buffer(ion_fd, output) != 0) {
        fprintf(stderr, "pre-decode ION sync failed: %s\n", strerror(errno));
        return -1;
    }

    config.decode_mode = 1U;
    config.register_operation_mode = 0U;
    config.total_mcu = 1U;
    config.component_zero_data_units = 1U;
    config.source_base = source->mva;
    config.source_size = 384U;
    config.source_write_pointer = source->mva + 336U;
    config.output_bank_zero[0] = output->mva;
    config.output_bank_zero[1] = output->mva + 64U;
    config.output_bank_zero[2] = output->mva + 128U;
    memcpy(config.output_bank_one, config.output_bank_zero,
           sizeof(config.output_bank_one));
    config.component_count = 1U;
    config.component_id[0] = 1U;
    config.horizontal_sampling[0] = 1U;
    config.vertical_sampling[0] = 1U;
    config.dma_mcu_in_group = 1U;
    config.dma_group_count = 1U;
    config.dma_last_mcu_count = 1U;
    config.component_image_stride[0] = 8U;
    config.component_memory_stride[0] = 16U;
    config.component_tile_stride[0] = 16U;
    config.mcu_per_row = 1U;
    config.pause_mcu = 1U;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        fprintf(stderr, "could not block termination signals: %s\n",
                strerror(errno));
        return -1;
    }
    signals_blocked = 1;

    jpeg_fd = open(JPEG_NODE, O_RDONLY | O_CLOEXEC);
    if (jpeg_fd < 0) {
        fprintf(stderr, "could not open %s: %s\n", JPEG_NODE, strerror(errno));
        goto cleanup;
    }
    if (ioctl(jpeg_fd, JPEG_INIT, 0UL) != 0) {
        fprintf(stderr, "JPEG INIT failed: %s\n", strerror(errno));
        goto cleanup;
    }
    initialized = 1;
    if (INIT_DIAGNOSTIC_HOLD_SECONDS > 0) {
        sleep(INIT_DIAGNOSTIC_HOLD_SECONDS);
    }
    if (ioctl(jpeg_fd, JPEG_CONFIG_ARM32, &config) != 0) {
        fprintf(stderr, "JPEG CONFIG failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (ioctl(jpeg_fd, JPEG_START, 0UL) != 0) {
        fprintf(stderr, "JPEG START failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (START_DIAGNOSTIC_HOLD_SECONDS > 0) {
        sleep(START_DIAGNOSTIC_HOLD_SECONDS);
    }
    wait_data.timeout_ms = 500;
    wait_data.result_address = (uint32_t)(uintptr_t)decode_result;
    if (ioctl(jpeg_fd, JPEG_WAIT_ARM32, &wait_data) != 0) {
        fprintf(stderr, "JPEG WAIT failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if ((*decode_result & 0xffU) != 0U ||
        ((*decode_result >> 8) & 0x01U) == 0U) {
        fprintf(stderr, "JPEG hardware returned status 0x%08x\n", *decode_result);
        goto cleanup;
    }
    result = 0;

cleanup:
    if (initialized && ioctl(jpeg_fd, JPEG_DEINIT, 0UL) != 0) {
        fprintf(stderr, "JPEG DEINIT failed: %s\n", strerror(errno));
        result = -1;
    }
    if (jpeg_fd >= 0) {
        close(jpeg_fd);
    }
    if (signals_blocked && sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        fprintf(stderr, "could not restore signal mask: %s\n", strerror(errno));
        result = -1;
    }
    if (sync_buffer(ion_fd, output) != 0) {
        fprintf(stderr, "post-decode ION sync failed: %s\n", strerror(errno));
        return -1;
    }
    *changed_bytes = 0;
    for (size_t index = 0; index < 64U; ++index) {
        if (output_bytes[index] != 0x5aU) {
            ++*changed_bytes;
        }
    }
    *output_hash = fnv1a32(output_bytes, 64U);
    if (result == 0 && *changed_bytes == 0U) {
        fprintf(stderr, "JPEG completed but output buffer did not change\n");
        result = -1;
    }
    return result;
}

static int allocation_arguments_are_guarded(int argc, char **argv) {
    return argc == 6 && strcmp(argv[1], "--execute-alloc-cycle") == 0 &&
           strcmp(argv[2], "--target") == 0 &&
           strcmp(argv[3], "checkers") == 0 &&
           strcmp(argv[4], "--acknowledge-m4u-mapping") == 0 &&
           strcmp(argv[5], "private-buffers-only") == 0;
}

static int decode_arguments_are_guarded(int argc, char **argv) {
    return argc == 6 && strcmp(argv[1], "--execute-decode-fixture") == 0 &&
           strcmp(argv[2], "--target") == 0 &&
           strcmp(argv[3], "checkers") == 0 &&
           strcmp(argv[4], "--acknowledge-hardware-decode") == 0 &&
           strcmp(argv[5], "private-buffers-only") == 0;
}

static int file_contains(const char *path, const char *needle) {
    uint8_t buffer[4096];
    size_t used = 0;
    size_t needle_length = strlen(needle);
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0 || needle_length == 0U || needle_length > sizeof(buffer)) {
        if (fd >= 0) {
            close(fd);
        }
        return 0;
    }
    while (used < sizeof(buffer)) {
        ssize_t count = read(fd, buffer + used, sizeof(buffer) - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        used += (size_t)count;
    }
    close(fd);
    if (used < needle_length) {
        return 0;
    }
    for (size_t offset = 0; offset <= used - needle_length; ++offset) {
        if (memcmp(buffer + offset, needle, needle_length) == 0) {
            return 1;
        }
    }
    return 0;
}

static int verify_checkers(void) {
    if (!file_contains("/proc/device-tree/compatible", "mediatek,mt8163") ||
        !file_contains("/proc/cmdline", "androidboot.product=checkers")) {
        fprintf(stderr, "refusing: running target is not Checkers/MT8163\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct private_buffer source = {.fd = -1, .mapping = MAP_FAILED};
    struct private_buffer output = {.fd = -1, .mapping = MAP_FAILED};
    uint32_t decode_result = 0;
    uint32_t output_hash = 0;
    uint32_t changed_bytes = 0;
    int decode_mode = decode_arguments_are_guarded(argc, argv);
    int ion_fd = -1;
    int result = 2;

    if (!allocation_arguments_are_guarded(argc, argv) && !decode_mode) {
        fprintf(stderr,
                "refusing: use the exact guarded allocation or decode "
                "invocation documented in README.md\n");
        return result;
    }
    if (geteuid() != 0 || verify_checkers() != 0) {
        fprintf(stderr, "refusing: active probe requires root on Checkers\n");
        return result;
    }
    ion_fd = open(ION_NODE, O_RDWR | O_CLOEXEC);
    if (ion_fd < 0) {
        fprintf(stderr, "could not open %s: %s\n", ION_NODE, strerror(errno));
        return result;
    }
    if (allocate_buffer(ion_fd, M4U_PORT_JPGDEC_BSDMA, &source) != 0) {
        fprintf(stderr, "source allocation/mapping failed: %s\n",
                strerror(errno));
        goto cleanup;
    }
    if (allocate_buffer(ion_fd, M4U_PORT_JPGDEC_WDMA, &output) != 0) {
        fprintf(stderr, "output allocation/mapping failed: %s\n",
                strerror(errno));
        goto cleanup;
    }
    if (decode_mode) {
        if (decode_fixture(ion_fd, &source, &output, &decode_result,
                           &output_hash, &changed_bytes) != 0) {
            goto cleanup;
        }
        printf("{\"target\":\"checkers\",\"fixture\":\"gray-8x8\","
               "\"fixture_bytes\":%u,\"decode_result\":%u,"
               "\"irq_eof\":true,\"output_changed_bytes\":%u,"
               "\"output_fnv1a32\":%u,\"hardware_decode\":\"ok\"}\n",
               (unsigned int)sizeof(gray_fixture), decode_result,
               changed_bytes, output_hash);
        result = 0;
        goto cleanup;
    }
    memset(source.mapping, 0xa5, source.length);
    memset(output.mapping, 0x5a, output.length);
    if (sync_buffer(ion_fd, &source) != 0 ||
        sync_buffer(ion_fd, &output) != 0) {
        fprintf(stderr, "ION buffer sync failed: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("{\"target\":\"checkers\",\"heap_id\":10,"
           "\"source\":{\"port\":%u,\"mva\":%u,\"length\":%u},"
           "\"output\":{\"port\":%u,\"mva\":%u,\"length\":%u},"
           "\"hardware_decode_started\":false}\n",
           M4U_PORT_JPGDEC_BSDMA, source.mva, source.length,
           M4U_PORT_JPGDEC_WDMA, output.mva, output.length);
    result = 0;

cleanup:
    release_buffer(ion_fd, &output);
    release_buffer(ion_fd, &source);
    close(ion_fd);
    return result;
}

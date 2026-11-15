#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mtk_yuv420_block.h"

#define VDEC_GET_DISPLAY_FRAME_BUFFER 2U
#define VDEC_SET_PRIVATE_OUTPUT_CALLBACK 22U
#define VDEC_RESULT_OK 0
#define VDEC_FRAME_TYPE_YUV420_BLOCK_16X1 1U
#define VAL_MEM_CODEC_FOR_VDEC 1U
#define VAL_MEM_TYPE_FOR_HW_CACHEABLE 1U
#define VAL_MEM_ALIGN_64 (1U << 6)
#define MAX_CAPTURE_FRAMES 32U
#define MAX_CAPTURE_DIMENSION 256U

struct val_mem_address {
    uint64_t virtual_address;
    uint64_t hardware_address;
    uint64_t size;
};

union val_word64 {
    unsigned long word;
    uint64_t extended;
};

union val_pointer64 {
    void *pointer;
    uint64_t extended;
};

struct val_memory {
    uint32_t memory_type;
    union val_word64 memory_size;
    union val_pointer64 memory_va;
    union val_pointer64 memory_pa;
    uint32_t alignment;
    union val_pointer64 aligned_memory_va;
    union val_pointer64 aligned_memory_pa;
    uint32_t memory_codec;
    uint32_t ion_share_fd;
    union val_pointer64 ion_buffer_handle;
    union val_pointer64 reserved;
    union val_word64 reserved_size;
};

struct vdec_frame_buffer {
    struct val_mem_address base;
    struct val_mem_address post_process;
    uint32_t buffer_width;
    uint32_t buffer_height;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_pitch;
    uint32_t timestamp;
    uint32_t aspect_ratio_width;
    uint32_t aspect_ratio_height;
    uint32_t frame_buffer_type;
    uint32_t picture_structure;
    uint32_t frame_buffer_status;
    uint32_t is_ufo_encoded;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t secure_memory_handle;
    uint32_t ree_virtual_address;
    uint32_t frame_buffer_share_handle;
};

typedef int32_t (*get_decoder_param_fn)(void *, uint32_t, void *, void *);
typedef int32_t (*set_decoder_param_fn)(void *, uint32_t, void *, void *);
typedef int32_t (*decode_ex_fn)(void *, void *, void *, void *);
typedef int32_t (*invalidate_cache_fn)(struct val_memory *, uint32_t,
                                       uint32_t);
typedef int32_t (*private_output_callback_fn)(
    unsigned long, struct vdec_frame_buffer **, uint32_t, int8_t, void *);

struct vdec_private_output_callback {
    void *context;
    void *reserved;
    private_output_callback_fn callback;
};

_Static_assert(sizeof(struct val_memory) == 88,
               "unexpected public VAL_MEMORY_T ABI size");
_Static_assert(sizeof(struct vdec_frame_buffer) == 120,
               "unexpected public VDEC frame ABI size");

static get_decoder_param_fn real_get_decoder_param;
static set_decoder_param_fn real_set_decoder_param;
static decode_ex_fn real_decode_ex;
static invalidate_cache_fn invalidate_cache;
static volatile int capture_lock;
static int capture_fd = -2;
static uint32_t capture_limit;
static uint32_t capture_count;
static uint32_t observed_get_param_calls;
static uint32_t observed_decode_ex_calls;
static uint32_t observed_output_callback_calls;
static struct vdec_private_output_callback original_output_callback;
static struct vdec_private_output_callback wrapped_output_callback;
static struct vdec_frame_buffer *supplied_frames[MAX_CAPTURE_FRAMES];
static uint32_t supplied_hashes[MAX_CAPTURE_FRAMES];
static uint32_t supplied_frame_count;

static uint32_t fnv1a32(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261U;
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

static void resolve_symbols(void) {
    void *symbol;

    if (real_get_decoder_param == NULL) {
        symbol = dlsym(RTLD_NEXT, "eVDecDrvGetParam");
        if (symbol != NULL &&
            sizeof(real_get_decoder_param) == sizeof(symbol))
            memcpy(&real_get_decoder_param, &symbol,
                   sizeof(real_get_decoder_param));
    }
    if (invalidate_cache == NULL) {
        symbol = dlsym(RTLD_NEXT, "eVideoInvalidateCache");
        if (symbol != NULL && sizeof(invalidate_cache) == sizeof(symbol))
            memcpy(&invalidate_cache, &symbol, sizeof(invalidate_cache));
    }
    if (real_set_decoder_param == NULL) {
        symbol = dlsym(RTLD_NEXT, "eVDecDrvSetParam");
        if (symbol != NULL &&
            sizeof(real_set_decoder_param) == sizeof(symbol))
            memcpy(&real_set_decoder_param, &symbol,
                   sizeof(real_set_decoder_param));
    }
    if (real_decode_ex == NULL) {
        symbol = dlsym(RTLD_NEXT, "eVDecDrvDecodeEx");
        if (symbol != NULL && sizeof(real_decode_ex) == sizeof(symbol))
            memcpy(&real_decode_ex, &symbol, sizeof(real_decode_ex));
    }
}

static int write_all(int fd, const uint8_t *data, size_t size) {
    while (size != 0) {
        ssize_t written = write(fd, data, size);

        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        data += (size_t)written;
        size -= (size_t)written;
    }
    return 0;
}

static void initialize_capture(void) {
    const char *path;
    const char *limit_text;
    char *end = NULL;
    unsigned long requested_limit;

    if (capture_fd != -2)
        return;
    capture_fd = -1;
    path = getenv("TATER_VDEC_CAPTURE_RGBA");
    limit_text = getenv("TATER_VDEC_CAPTURE_FRAMES");
    if (path == NULL || path[0] != '/' || limit_text == NULL)
        return;
    errno = 0;
    requested_limit = strtoul(limit_text, &end, 10);
    if (errno != 0 || end == limit_text || *end != '\0' ||
        requested_limit == 0 || requested_limit > MAX_CAPTURE_FRAMES)
        return;
    capture_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (capture_fd >= 0)
        capture_limit = (uint32_t)requested_limit;
}

static uint32_t capture_frame(struct vdec_frame_buffer *frame,
                              uint32_t previous_hash) {
    size_t tiled_bytes;
    size_t rgba_bytes;
    uint8_t *tiled;
    uint8_t *rgba;
    struct val_memory memory;
    uint32_t rgba_hash;
    uint8_t values[256] = {0};
    uint32_t distinct_values = 0;
    size_t index;

    if (frame == NULL || capture_count >= capture_limit ||
        frame->base.virtual_address == 0 ||
        frame->buffer_width == 0 || frame->buffer_height == 0 ||
        frame->display_width == 0 || frame->display_height == 0 ||
        frame->buffer_width > MAX_CAPTURE_DIMENSION ||
        frame->buffer_height > MAX_CAPTURE_DIMENSION ||
        frame->display_width > frame->buffer_width ||
        frame->display_height > frame->buffer_height ||
        frame->frame_buffer_type !=
            VDEC_FRAME_TYPE_YUV420_BLOCK_16X1 ||
        frame->is_ufo_encoded != 0)
        return 0;
    tiled_bytes = (size_t)frame->buffer_width * frame->buffer_height *
                  3U / 2U;
    rgba_bytes = (size_t)frame->display_width * frame->display_height * 4U;
    if (tiled_bytes == 0 || tiled_bytes > frame->base.size ||
        rgba_bytes == 0 || rgba_bytes >
            (size_t)MAX_CAPTURE_DIMENSION * MAX_CAPTURE_DIMENSION * 4U)
        return 0;
    tiled = (uint8_t *)(uintptr_t)frame->base.virtual_address;
    rgba = malloc(rgba_bytes);
    if (rgba == NULL)
        return 0;

    if (invalidate_cache != NULL) {
        memset(&memory, 0, sizeof(memory));
        memory.memory_type = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
        memory.memory_size.extended = tiled_bytes;
        memory.memory_va.pointer = tiled;
        memory.memory_pa.extended = frame->base.hardware_address;
        memory.alignment = VAL_MEM_ALIGN_64;
        memory.aligned_memory_va.pointer = tiled;
        memory.aligned_memory_pa.extended =
            frame->base.hardware_address;
        memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;
        memory.ion_share_fd = UINT32_MAX;
        (void)invalidate_cache(&memory, sizeof(memory), 1);
    }
    if (tater_mtk_yuv420_block_to_rgba8888(
            tiled, tiled_bytes, frame->buffer_width, frame->buffer_height,
            frame->display_width, frame->display_height, rgba,
            (size_t)frame->display_width * 4U, rgba_bytes) != 0) {
        free(rgba);
        return 0;
    }
    rgba_hash = fnv1a32(rgba, rgba_bytes);
    for (index = 0; index < rgba_bytes; ++index)
        values[rgba[index]] = 1;
    for (index = 0; index < 256U; ++index)
        distinct_values += values[index];
    if (distinct_values > 2U && rgba_hash != previous_hash &&
        write_all(capture_fd, rgba, rgba_bytes) == 0) {
        fprintf(stderr,
                "TATER_VDEC_CAPTURE frame=%u timestamp=%u rgba_hash=%u\n",
                capture_count, frame->timestamp, rgba_hash);
        ++capture_count;
    } else if (rgba_hash == previous_hash || distinct_values <= 2U) {
        rgba_hash = 0;
    }
    free(rgba);
    return rgba_hash;
}

static void scan_supplied_frames(void) {
    uint32_t index;

    initialize_capture();
    if (capture_fd < 0)
        return;
    for (index = 0;
         index < supplied_frame_count && capture_count < capture_limit;
         ++index) {
        uint32_t hash = capture_frame(supplied_frames[index],
                                      supplied_hashes[index]);

        if (hash != 0)
            supplied_hashes[index] = hash;
    }
}

static int32_t wrapped_output_callback_fn(
    unsigned long context, struct vdec_frame_buffer **output,
    uint32_t request_flags, int8_t end_of_stream, void *opaque) {
    int32_t result;
    uint32_t index;

    result = original_output_callback.callback(
        context, output, request_flags, end_of_stream, opaque);
    if (observed_output_callback_calls < 32U) {
        struct vdec_frame_buffer *frame =
            output == NULL ? NULL : *output;

        fprintf(stderr,
                "TATER_VDEC_OUTPUT_CALLBACK call=%u flags=%u eos=%d "
                "result=%d frame=%s",
                observed_output_callback_calls, request_flags,
                (int)end_of_stream, result,
                frame == NULL ? "null" : "present");
        if (frame != NULL) {
            fprintf(stderr,
                    " size=%llu buffer=%ux%u display=%ux%u type=%u ufo=%u",
                    (unsigned long long)frame->base.size,
                    frame->buffer_width, frame->buffer_height,
                    frame->display_width, frame->display_height,
                    frame->frame_buffer_type, frame->is_ufo_encoded);
        }
        fputc('\n', stderr);
        ++observed_output_callback_calls;
    }
    if (result != 0 || output == NULL || *output == NULL)
        return result;
    while (__sync_lock_test_and_set(&capture_lock, 1) != 0)
        usleep(1000);
    for (index = 0; index < supplied_frame_count; ++index) {
        if (supplied_frames[index] == *output)
            break;
    }
    if (index == supplied_frame_count &&
        supplied_frame_count < MAX_CAPTURE_FRAMES)
        supplied_frames[supplied_frame_count++] = *output;
    __sync_lock_release(&capture_lock);
    return result;
}

__attribute__((visibility("default"))) int32_t eVDecDrvSetParam(
    void *decoder, uint32_t type, void *input, void *output) {
    set_decoder_param_fn set_parameter;

    while (__sync_lock_test_and_set(&capture_lock, 1) != 0)
        usleep(1000);
    resolve_symbols();
    set_parameter = real_set_decoder_param;
    if (set_parameter != NULL &&
        type == VDEC_SET_PRIVATE_OUTPUT_CALLBACK && input != NULL) {
        struct vdec_private_output_callback *registration = input;

        if (registration->callback != NULL) {
            original_output_callback = *registration;
            wrapped_output_callback = *registration;
            wrapped_output_callback.callback = wrapped_output_callback_fn;
            input = &wrapped_output_callback;
            fprintf(stderr,
                    "TATER_VDEC_SET_PARAM wrapped_output_callback=1\n");
        }
    }
    __sync_lock_release(&capture_lock);
    if (set_parameter == NULL)
        return -1;
    return set_parameter(decoder, type, input, output);
}

__attribute__((visibility("default"))) int32_t eVDecDrvDecodeEx(
    void *decoder, void *ring, void *input, void *output) {
    decode_ex_fn decode;
    int32_t result;

    while (__sync_lock_test_and_set(&capture_lock, 1) != 0)
        usleep(1000);
    resolve_symbols();
    decode = real_decode_ex;
    __sync_lock_release(&capture_lock);
    if (decode == NULL)
        return -1;

    result = decode(decoder, ring, input, output);
    if (observed_decode_ex_calls < 32U) {
        fprintf(stderr, "TATER_VDEC_DECODE_EX call=%u result=%d\n",
                observed_decode_ex_calls, result);
        ++observed_decode_ex_calls;
    }
    while (__sync_lock_test_and_set(&capture_lock, 1) != 0)
        usleep(1000);
    scan_supplied_frames();
    __sync_lock_release(&capture_lock);
    return result;
}

__attribute__((visibility("default"))) int32_t eVDecDrvGetParam(
    void *decoder, uint32_t type, void *input, void *output) {
    int32_t result;

    while (__sync_lock_test_and_set(&capture_lock, 1) != 0)
        usleep(1000);
    resolve_symbols();
    if (real_get_decoder_param == NULL) {
        __sync_lock_release(&capture_lock);
        return -1;
    }
    __sync_lock_release(&capture_lock);

    result = real_get_decoder_param(decoder, type, input, output);
    if (observed_get_param_calls < 32U) {
        fprintf(stderr, "TATER_VDEC_GET_PARAM call=%u type=%u result=%d\n",
                observed_get_param_calls, type, result);
        ++observed_get_param_calls;
    }
    if (result == VDEC_RESULT_OK &&
        type == VDEC_GET_DISPLAY_FRAME_BUFFER && output != NULL) {
        struct vdec_frame_buffer *frame =
            *(struct vdec_frame_buffer **)output;

        if (frame == NULL) {
            fprintf(stderr, "TATER_VDEC_DISPLAY frame=null\n");
        } else {
            fprintf(stderr,
                    "TATER_VDEC_DISPLAY size=%llu buffer=%ux%u "
                    "display=%ux%u type=%u ufo=%u timestamp=%u\n",
                    (unsigned long long)frame->base.size,
                    frame->buffer_width, frame->buffer_height,
                    frame->display_width, frame->display_height,
                    frame->frame_buffer_type, frame->is_ufo_encoded,
                    frame->timestamp);
        }

        while (__sync_lock_test_and_set(&capture_lock, 1) != 0)
            usleep(1000);
        scan_supplied_frames();
        __sync_lock_release(&capture_lock);
    }
    return result;
}

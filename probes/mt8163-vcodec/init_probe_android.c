#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mtk_yuv420_block.h"

#define TEST_MAPPING_SIZE (1024U * 1024U)
#define TOTAL_MAPPING_SIZE (2U * TEST_MAPPING_SIZE)
#define MAX_FIXTURE_SIZE 4096U
#define MAX_STREAM_FIXTURE_SIZE (256U * 1024U)
#define MAX_STREAM_FRAMES 32U
#define MAX_STREAM_SLOTS (MAX_STREAM_FRAMES + 4U)
#define VDEC_FORMAT_H264 (1U << 8)
#define VDEC_RESULT_OK 0
#define VDEC_SET_BUFFER_MODE 1U
#define VDEC_SET_FRAME_BUFFER_TYPE 2U
#define VDEC_SET_DECODE_MODE 6U
#define VDEC_SET_PRIVATE_OUTPUT_CALLBACK 22U
#define VDEC_GET_DISPLAY_FRAME_BUFFER 2U
#define VDEC_GET_FREE_INPUT_BUFFER 20U
#define VDEC_BUFFER_REF_IS_DISPLAY_EXTERNAL 1U
#define VDEC_FRAME_TYPE_YUV420_BLOCK_16X1 1U
#define VDEC_DECODE_MODE_NORMAL 1U
#define VDEC_DECODE_MODE_LOW_LATENCY 8U
#define VDEC_INPUT_FLAG_CODEC_CONFIG 1U
#define VDEC_INPUT_FLAG_END_OF_STREAM 2U
#define VAL_MEM_CODEC_FOR_VDEC 1U
#define VAL_MEM_TYPE_FOR_HW_CACHEABLE 1U
#define VAL_MEM_ALIGN_64 (1U << 6)

struct m4u_buffer_config {
    uint32_t memory_codec;
    uint32_t cache_coherent;
    uint32_t security;
};

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

struct vdec_ring_buffer {
    struct val_mem_address base;
    uint64_t read;
    uint64_t write;
    uint32_t timestamp;
    uint32_t secure_memory_handle;
    uint32_t input_flag;
};

struct vdec_sequence_info {
    uint32_t buffer_width;
    uint32_t buffer_height;
    uint32_t picture_width;
    uint32_t picture_height;
    int32_t aspect_ratio_width;
    int32_t aspect_ratio_height;
    int8_t is_thumbnail;
    int32_t luma_bit_depth;
    int32_t chroma_bit_depth;
    int8_t horizontal_scan_in_lsb;
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

struct stream_output_pool {
    struct vdec_frame_buffer *frames;
    uint32_t frame_count;
    uint32_t next_frame;
    uint32_t callback_calls;
};

struct vdec_set_decode_mode {
    uint32_t decode_mode;
    uint32_t display_frame_count;
    uint32_t drop_frame_count;
};

typedef int32_t (*private_output_callback_fn)(
    unsigned long, struct vdec_frame_buffer **, uint32_t, int8_t, void *);

struct vdec_private_output_callback {
    void *context;
    void *reserved;
    private_output_callback_fn callback;
};

typedef uint32_t (*init_mva_fn)(void **);
typedef uint32_t (*alloc_mva_fn)(void *, uint32_t, uint32_t *, uint32_t,
                                 struct m4u_buffer_config *);
typedef uint32_t (*free_mva_fn)(void *, uint32_t, uint32_t, uint32_t,
                                struct m4u_buffer_config *);
typedef uint32_t (*deinit_mva_fn)(void *);
typedef int32_t (*invalidate_cache_fn)(struct val_memory *, uint32_t,
                                       uint32_t);
typedef int32_t (*create_decoder_fn)(void **, uint32_t);
typedef int32_t (*init_decoder_fn)(void *, struct vdec_ring_buffer *,
                                   struct vdec_sequence_info *);
typedef int32_t (*deinit_decoder_fn)(void *);
typedef int32_t (*release_decoder_fn)(void *);
typedef int32_t (*set_decoder_param_fn)(void *, uint32_t, void *, void *);
typedef int32_t (*get_decoder_param_fn)(void *, uint32_t, void *, void *);
typedef int32_t (*decode_decoder_fn)(void *, struct vdec_ring_buffer *,
                                     struct vdec_frame_buffer *);
typedef int32_t (*decode_ex_decoder_fn)(void *, struct vdec_ring_buffer *,
                                        void *, void *);

_Static_assert(sizeof(struct m4u_buffer_config) == 12,
               "unexpected public M4U config ABI size");
_Static_assert(sizeof(struct val_mem_address) == 24,
               "unexpected public VAL_MEM_ADDR_T ABI size");
_Static_assert(sizeof(struct val_memory) == 88,
               "unexpected public VAL_MEMORY_T ABI size");
_Static_assert(sizeof(struct vdec_ring_buffer) == 56,
               "unexpected public VDEC ring ABI size");
_Static_assert(sizeof(struct vdec_sequence_info) == 40,
               "unexpected public VDEC sequence ABI size");
_Static_assert(sizeof(struct vdec_frame_buffer) == 120,
               "unexpected public VDEC frame ABI size");
_Static_assert(sizeof(struct vdec_private_output_callback) == 12,
               "unexpected owner-local output callback ABI size");
_Static_assert(sizeof(struct vdec_set_decode_mode) == 12,
               "unexpected public VDEC decode-mode ABI size");
_Static_assert(sizeof(init_decoder_fn) == sizeof(void *),
               "function pointer cannot be loaded safely");

static int load_symbol(void *library, const char *name, void *function,
                       size_t function_size) {
    void *symbol = dlsym(library, name);

    if (symbol == NULL) {
        fprintf(stderr, "dlsym failed for %s: %s\n", name, dlerror());
        return -1;
    }
    if (function_size != sizeof(symbol)) {
        fprintf(stderr, "refusing incompatible function pointer size\n");
        return -1;
    }
    memcpy(function, &symbol, function_size);
    return 0;
}

static int32_t provide_stream_output(
    unsigned long context, struct vdec_frame_buffer **output,
    uint32_t request_flags, int8_t end_of_stream, void *opaque) {
    struct stream_output_pool *pool =
        (struct stream_output_pool *)(uintptr_t)context;

    (void)request_flags;
    (void)end_of_stream;
    (void)opaque;
    if (output == NULL)
        return -1;
    *output = NULL;
    if (pool != NULL)
        ++pool->callback_calls;
    if (pool == NULL || pool->frames == NULL ||
        pool->next_frame >= pool->frame_count)
        return -1;
    *output = &pool->frames[pool->next_frame++];
    return 0;
}

static int has_start_code_and_nal(const uint8_t *data, size_t size,
                                  uint8_t nal_type) {
    size_t index;

    for (index = 0; index + 4 < size; ++index) {
        size_t header;

        if (data[index] != 0 || data[index + 1] != 0)
            continue;
        if (data[index + 2] == 1)
            header = index + 3;
        else if (data[index + 2] == 0 && data[index + 3] == 1)
            header = index + 4;
        else
            continue;
        if (header < size && (data[header] & 0x1fU) == nal_type)
            return 1;
    }
    return 0;
}

static int find_nal_start(const uint8_t *data, size_t size, uint8_t nal_type,
                          size_t *start_offset) {
    size_t index;

    for (index = 0; index + 4 < size; ++index) {
        size_t header;

        if (data[index] != 0 || data[index + 1] != 0)
            continue;
        if (data[index + 2] == 1)
            header = index + 3;
        else if (data[index + 2] == 0 && data[index + 3] == 1)
            header = index + 4;
        else
            continue;
        if (header < size && (data[header] & 0x1fU) == nal_type) {
            *start_offset = index;
            return 0;
        }
    }
    return -1;
}

static int collect_synthetic_frame_ends(const uint8_t *data, size_t size,
                                        size_t idr_offset, size_t *starts,
                                        size_t *ends, size_t capacity,
                                        size_t *count) {
    size_t found = 0;
    size_t index;
    int use_access_unit_delimiters = 0;

    for (index = idr_offset + 1; index + 4 < size; ++index) {
        size_t header;

        if (data[index] != 0 || data[index + 1] != 0)
            continue;
        if (data[index + 2] == 1)
            header = index + 3;
        else if (data[index + 2] == 0 && data[index + 3] == 1)
            header = index + 4;
        else
            continue;
        if (header < size && (data[header] & 0x1fU) == 9) {
            use_access_unit_delimiters = 1;
            break;
        }
        index = header;
    }

    starts[0] = idr_offset;
    for (index = idr_offset + 1; index + 4 < size; ++index) {
        size_t header;

        if (data[index] != 0 || data[index + 1] != 0)
            continue;
        if (data[index + 2] == 1)
            header = index + 3;
        else if (data[index + 2] == 0 && data[index + 3] == 1)
            header = index + 4;
        else
            continue;
        if (header < size &&
            (data[header] & 0x1fU) ==
                (use_access_unit_delimiters ? 9U : 7U)) {
            if (found >= capacity)
                return -1;
            ends[found++] = index;
            if (found >= capacity)
                return -1;
            starts[found] = index;
        }
        index = header;
    }
    if (found >= capacity)
        return -1;
    ends[found++] = size;
    *count = found;
    return 0;
}

static size_t skip_leading_access_unit_delimiter(const uint8_t *data,
                                                 size_t size,
                                                 size_t offset) {
    size_t header;
    size_t index;

    if (offset + 4 >= size || data[offset] != 0 || data[offset + 1] != 0)
        return offset;
    if (data[offset + 2] == 1)
        header = offset + 3;
    else if (data[offset + 2] == 0 && data[offset + 3] == 1)
        header = offset + 4;
    else
        return offset;
    if (header >= size || (data[header] & 0x1fU) != 9)
        return offset;
    for (index = header + 1; index + 4 < size; ++index) {
        if (data[index] != 0 || data[index + 1] != 0)
            continue;
        if (data[index + 2] == 1 ||
            (data[index + 2] == 0 && data[index + 3] == 1))
            return index;
    }
    return offset;
}

static int read_fixture(const char *path, void *mapping, size_t maximum_size,
                        size_t *fixture_size, size_t *idr_offset) {
    struct stat status;
    FILE *file;
    size_t count;

    if (stat(path, &status) != 0 || status.st_size <= 0 ||
        status.st_size > (off_t)maximum_size) {
        fprintf(stderr, "fixture must be between 1 and %zu bytes\n",
                maximum_size);
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "could not open fixture: %s\n", strerror(errno));
        return -1;
    }
    count = fread(mapping, 1, (size_t)status.st_size, file);
    fclose(file);
    if (count != (size_t)status.st_size ||
        !has_start_code_and_nal(mapping, count, 7) ||
        !has_start_code_and_nal(mapping, count, 8) ||
        find_nal_start(mapping, count, 5, idr_offset) != 0 ||
        *idr_offset == 0) {
        fprintf(stderr, "fixture is not bounded Annex-B H.264 SPS/PPS/IDR\n");
        return -1;
    }
    *fixture_size = count;
    return 0;
}

static uint32_t fnv1a32(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261U;
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

static int read_exact_file(const char *path, uint8_t **data, size_t size) {
    struct stat status;
    FILE *file;
    uint8_t *buffer;
    size_t count;

    if (stat(path, &status) != 0 || status.st_size != (off_t)size)
        return -1;
    buffer = malloc(size);
    if (buffer == NULL)
        return -1;
    file = fopen(path, "rb");
    if (file == NULL) {
        free(buffer);
        return -1;
    }
    count = fread(buffer, 1, size, file);
    fclose(file);
    if (count != size) {
        free(buffer);
        return -1;
    }
    *data = buffer;
    return 0;
}

static int write_exact_file(const char *path, const uint8_t *data,
                            size_t size) {
    FILE *file;
    size_t count;

    file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    count = fwrite(data, 1, size, file);
    if (fclose(file) != 0 || count != size)
        return -1;
    return 0;
}

static size_t tiled_offset(size_t x, size_t y, size_t width,
                           size_t tile_width, size_t tile_height) {
    size_t tiles_per_row = width / tile_width;
    size_t tile_index = (y / tile_height) * tiles_per_row +
                        x / tile_width;

    return tile_index * tile_width * tile_height +
           (y % tile_height) * tile_width + x % tile_width;
}

static size_t score_luma(const uint8_t *output, const uint8_t *expected,
                         size_t width, size_t height, size_t tile_height) {
    size_t matches = 0;
    size_t x;
    size_t y;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            size_t offset = tiled_offset(x, y, width, 16, tile_height);
            if (output[offset] == expected[y * width + x])
                ++matches;
        }
    }
    return matches;
}

static size_t score_chroma_planar(const uint8_t *output,
                                  const uint8_t *expected,
                                  size_t width, size_t height,
                                  size_t tile_width, size_t tile_height,
                                  int swap_uv) {
    size_t y_size = width * height;
    size_t chroma_width = width / 2;
    size_t chroma_height = height / 2;
    size_t chroma_size = chroma_width * chroma_height;
    size_t matches = 0;
    size_t x;
    size_t y;

    for (y = 0; y < chroma_height; ++y) {
        for (x = 0; x < chroma_width; ++x) {
            size_t offset = tiled_offset(x, y, chroma_width, tile_width,
                                         tile_height);
            size_t reference = y * chroma_width + x;
            const uint8_t *first = expected + y_size;
            const uint8_t *second = first + chroma_size;

            if (swap_uv) {
                const uint8_t *temporary = first;
                first = second;
                second = temporary;
            }
            if (output[y_size + offset] == first[reference])
                ++matches;
            if (output[y_size + chroma_size + offset] == second[reference])
                ++matches;
        }
    }
    return matches;
}

static size_t score_chroma_interleaved(const uint8_t *output,
                                       const uint8_t *expected,
                                       size_t width, size_t height,
                                       size_t tile_height, int swap_uv) {
    size_t y_size = width * height;
    size_t chroma_width = width / 2;
    size_t chroma_height = height / 2;
    size_t chroma_size = chroma_width * chroma_height;
    size_t matches = 0;
    size_t x;
    size_t y;

    for (y = 0; y < chroma_height; ++y) {
        for (x = 0; x < chroma_width; ++x) {
            size_t offset = tiled_offset(x * 2, y, width, 16, tile_height);
            size_t reference = y * chroma_width + x;
            uint8_t u = expected[y_size + reference];
            uint8_t v = expected[y_size + chroma_size + reference];

            if (swap_uv) {
                uint8_t temporary = u;
                u = v;
                v = temporary;
            }
            if (output[y_size + offset] == u)
                ++matches;
            if (output[y_size + offset + 1] == v)
                ++matches;
        }
    }
    return matches;
}

static void report_layout_scores(const uint8_t *output,
                                 const uint8_t *expected,
                                 size_t width, size_t height) {
    static const size_t heights[] = {1, 2, 4, 8, 16, 32, 64};
    static const size_t planar_widths[] = {8, 16, 32};
    size_t best_luma = 0;
    size_t best_luma_height = 0;
    size_t best_chroma = 0;
    size_t best_chroma_height = 0;
    size_t best_chroma_width = 0;
    const char *best_chroma_layout = "none";
    int best_chroma_swapped = 0;
    size_t index;
    size_t width_index;
    int swapped;

    for (index = 0; index < sizeof(heights) / sizeof(heights[0]); ++index) {
        size_t tile_height = heights[index];
        size_t score;

        if (height % tile_height != 0)
            continue;
        score = score_luma(output, expected, width, height, tile_height);
        if (score > best_luma) {
            best_luma = score;
            best_luma_height = tile_height;
        }
        if ((height / 2) % tile_height != 0)
            continue;
        for (swapped = 0; swapped <= 1; ++swapped) {
            score = score_chroma_interleaved(output, expected, width, height,
                                             tile_height, swapped);
            if (score > best_chroma) {
                best_chroma = score;
                best_chroma_height = tile_height;
                best_chroma_width = 16;
                best_chroma_layout = "interleaved";
                best_chroma_swapped = swapped;
            }
        }
        for (width_index = 0;
             width_index < sizeof(planar_widths) / sizeof(planar_widths[0]);
             ++width_index) {
            size_t tile_width = planar_widths[width_index];

            if ((width / 2) % tile_width != 0)
                continue;
            for (swapped = 0; swapped <= 1; ++swapped) {
                score = score_chroma_planar(output, expected, width, height,
                                            tile_width, tile_height, swapped);
                if (score > best_chroma) {
                    best_chroma = score;
                    best_chroma_height = tile_height;
                    best_chroma_width = tile_width;
                    best_chroma_layout = "planar";
                    best_chroma_swapped = swapped;
                }
            }
        }
    }
    printf(",\"layout_probe\":{\"luma_matches\":%zu,"
           "\"luma_pixels\":%zu,\"luma_tile\":\"16x%zu\","
           "\"chroma_matches\":%zu,\"chroma_samples\":%zu,"
           "\"chroma_layout\":\"%s\",\"chroma_tile\":\"%zux%zu\","
           "\"chroma_order\":\"%s\"}",
           best_luma, width * height, best_luma_height, best_chroma,
           width * height / 2, best_chroma_layout, best_chroma_width,
           best_chroma_height, best_chroma_swapped ? "vu" : "uv");
}

int main(int argc, char **argv) {
    init_mva_fn init_mva;
    alloc_mva_fn alloc_mva;
    free_mva_fn free_mva;
    deinit_mva_fn deinit_mva;
    invalidate_cache_fn invalidate_cache;
    create_decoder_fn create_decoder;
    init_decoder_fn init_decoder;
    deinit_decoder_fn deinit_decoder;
    release_decoder_fn release_decoder;
    set_decoder_param_fn set_decoder_param;
    get_decoder_param_fn get_decoder_param;
    decode_decoder_fn decode_decoder;
    decode_ex_decoder_fn decode_ex_decoder = NULL;
    struct m4u_buffer_config config = {
        .memory_codec = VAL_MEM_CODEC_FOR_VDEC,
        .cache_coherent = 0,
        .security = 0,
    };
    struct vdec_ring_buffer ring;
    struct vdec_sequence_info sequence;
    struct vdec_frame_buffer frame;
    struct val_memory cache_memory;
    void *utility = NULL;
    void *decoder_library = NULL;
    void *mva_handle = NULL;
    void *decoder = NULL;
    void *mapping = MAP_FAILED;
    void *output_mapping = MAP_FAILED;
    uint32_t mva = 0;
    uint32_t output_mva = 0;
    size_t fixture_size = 0;
    size_t idr_offset = 0;
    int mva_ready = 0;
    int decoder_created = 0;
    int decoder_initialized = 0;
    int result = 1;
    int32_t decoder_result;
    int32_t buffer_mode_result;
    int32_t frame_type_result;
    int32_t get_param_result;
    uint32_t buffer_mode = VDEC_BUFFER_REF_IS_DISPLAY_EXTERNAL;
    uint32_t frame_type = VDEC_FRAME_TYPE_YUV420_BLOCK_16X1;
    int do_decode;
    uint32_t output_hash_before = 0;
    uint32_t output_hash_after = 0;
    uint32_t decode_calls = 0;
    uint32_t output_changed_bytes = 0;
    int32_t invalidate_result = -1;
    size_t output_sample_size = 0;
    size_t index;
    uint32_t value_counts[256] = {0};
    uint32_t distinct_output_values = 0;
    uint8_t *expected_yuv = NULL;
    uint8_t *converted_rgba = NULL;
    size_t converted_rgba_size = 0;
    uint32_t converted_rgba_hash = 0;
    int layout_probe = 0;
    int stream_probe = 0;
    int normal_stream_probe = 0;
    int dump_rgba = 0;
    int dump_stream_rgba = 0;
    unsigned int expected_stream_frames = 0;
    unsigned int fed_stream_frames = 0;
    unsigned int freed_stream_inputs = 0;
    unsigned int decoded_stream_frames = 0;
    unsigned int unique_stream_frames = 0;
    unsigned int stream_callback_calls = 0;
    unsigned int stream_supplied_outputs = 0;
    uint32_t stream_rgba_fold_hash = 2166136261U;
    uint32_t stream_rgba_hashes[MAX_STREAM_FRAMES];
    uint32_t stream_display_timestamps[MAX_STREAM_FRAMES];
    uint32_t stream_display_slots[MAX_STREAM_FRAMES];
    uint8_t *stream_rgba_sequence = NULL;
    size_t stream_rgba_sequence_size = 0;
    size_t stream_frame_starts[MAX_STREAM_FRAMES];
    size_t stream_frame_ends[MAX_STREAM_FRAMES];
    size_t stream_frame_end_count = 0;
    char *frame_count_end = NULL;
    unsigned long requested_frame_count;

    if ((argc != 3 && argc != 4 && argc != 5) ||
        (strcmp(argv[1], "--initialize-h264") != 0 &&
         strcmp(argv[1], "--decode-one-h264-frame") != 0 &&
         strcmp(argv[1], "--probe-yuv-layout") != 0 &&
         strcmp(argv[1], "--probe-synthetic-h264-stream") != 0 &&
         strcmp(argv[1], "--dump-synthetic-h264-stream-rgba") != 0 &&
         strcmp(argv[1], "--dump-one-h264-rgba") != 0) ||
        (strcmp(argv[1], "--dump-synthetic-h264-stream-rgba") == 0 &&
         argc != 5) ||
        ((strcmp(argv[1], "--probe-yuv-layout") == 0 ||
          strcmp(argv[1], "--probe-synthetic-h264-stream") == 0 ||
          strcmp(argv[1], "--dump-one-h264-rgba") == 0) &&
         argc != 4) ||
        ((strcmp(argv[1], "--probe-yuv-layout") != 0 &&
          strcmp(argv[1], "--probe-synthetic-h264-stream") != 0 &&
          strcmp(argv[1], "--dump-synthetic-h264-stream-rgba") != 0 &&
          strcmp(argv[1], "--dump-one-h264-rgba") != 0) &&
         argc != 3)) {
        fprintf(stderr,
                "refusing: use --initialize-h264 FIXTURE or "
                "--decode-one-h264-frame FIXTURE or "
                "--probe-yuv-layout SYNTHETIC_FIXTURE EXPECTED_YUV420P or "
                "--probe-synthetic-h264-stream FIXTURE FRAME_COUNT or "
                "--dump-synthetic-h264-stream-rgba FIXTURE FRAME_COUNT "
                "OUTPUT_RGBA or "
                "--dump-one-h264-rgba FIXTURE OUTPUT_RGBA\n");
        return 2;
    }
    layout_probe = strcmp(argv[1], "--probe-yuv-layout") == 0;
    dump_stream_rgba =
        strcmp(argv[1], "--dump-synthetic-h264-stream-rgba") == 0;
    stream_probe =
        strcmp(argv[1], "--probe-synthetic-h264-stream") == 0 ||
        dump_stream_rgba;
    dump_rgba = strcmp(argv[1], "--dump-one-h264-rgba") == 0;
    if (stream_probe || normal_stream_probe) {
        errno = 0;
        requested_frame_count = strtoul(argv[3], &frame_count_end, 10);
        if (errno != 0 || frame_count_end == argv[3] ||
            *frame_count_end != '\0' || requested_frame_count < 2 ||
            requested_frame_count > MAX_STREAM_FRAMES) {
            fprintf(stderr, "stream frame count must be between 2 and %u\n",
                    MAX_STREAM_FRAMES);
            return 2;
        }
        expected_stream_frames = (unsigned int)requested_frame_count;
    }
    do_decode = strcmp(argv[1], "--decode-one-h264-frame") == 0 ||
                layout_probe || stream_probe || normal_stream_probe ||
                dump_rgba;

    utility = dlopen("libvcodec_utility.so", RTLD_NOW | RTLD_LOCAL);
    decoder_library = dlopen("libvcodecdrv.so", RTLD_NOW | RTLD_LOCAL);
    if (utility == NULL || decoder_library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        goto cleanup;
    }
    if (load_symbol(utility, "eVideoInitMVA", &init_mva,
                    sizeof(init_mva)) != 0 ||
        load_symbol(utility, "eVideoAllocMVA", &alloc_mva,
                    sizeof(alloc_mva)) != 0 ||
        load_symbol(utility, "eVideoFreeMVA", &free_mva,
                    sizeof(free_mva)) != 0 ||
        load_symbol(utility, "eVideoDeInitMVA", &deinit_mva,
                    sizeof(deinit_mva)) != 0 ||
        load_symbol(utility, "eVideoInvalidateCache", &invalidate_cache,
                    sizeof(invalidate_cache)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvCreate", &create_decoder,
                    sizeof(create_decoder)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvInit", &init_decoder,
                    sizeof(init_decoder)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvDeInit", &deinit_decoder,
                    sizeof(deinit_decoder)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvRelease", &release_decoder,
                    sizeof(release_decoder)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvSetParam", &set_decoder_param,
                    sizeof(set_decoder_param)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvGetParam", &get_decoder_param,
                    sizeof(get_decoder_param)) != 0 ||
        load_symbol(decoder_library, "eVDecDrvDecode", &decode_decoder,
                    sizeof(decode_decoder)) != 0) {
        goto cleanup;
    }
    if (stream_probe &&
        load_symbol(decoder_library, "eVDecDrvDecodeEx", &decode_ex_decoder,
                    sizeof(decode_ex_decoder)) != 0)
        goto cleanup;

    mapping = mmap(NULL, TOTAL_MAPPING_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }
    memset(mapping, 0, TOTAL_MAPPING_SIZE);
    if (read_fixture(argv[2], mapping,
                     (stream_probe || normal_stream_probe)
                         ? MAX_STREAM_FIXTURE_SIZE
                         : MAX_FIXTURE_SIZE,
                     &fixture_size, &idr_offset) != 0)
        goto cleanup;
    if ((stream_probe || normal_stream_probe) &&
        (collect_synthetic_frame_ends(
             mapping, fixture_size, idr_offset, stream_frame_starts,
             stream_frame_ends,
             MAX_STREAM_FRAMES, &stream_frame_end_count) != 0 ||
         stream_frame_end_count != expected_stream_frames)) {
        fprintf(stderr,
                "bounded stream must contain exactly %u access units\n",
                expected_stream_frames);
        goto cleanup;
    }

    (void)init_mva(&mva_handle);
    if (mva_handle == NULL ||
        alloc_mva(mva_handle, (uint32_t)(uintptr_t)mapping, &mva,
                  TOTAL_MAPPING_SIZE, &config) != 0 ||
        mva == 0) {
        fprintf(stderr, "could not map fixture into VDEC address space\n");
        goto cleanup;
    }
    mva_ready = 1;

    decoder_result = create_decoder(&decoder, VDEC_FORMAT_H264);
    if (decoder_result != VDEC_RESULT_OK || decoder == NULL) {
        fprintf(stderr, "decoder create failed: result=%d\n", decoder_result);
        goto cleanup;
    }
    decoder_created = 1;

    buffer_mode_result = set_decoder_param(
        decoder, VDEC_SET_BUFFER_MODE, &buffer_mode, NULL);
    frame_type_result = set_decoder_param(
        decoder, VDEC_SET_FRAME_BUFFER_TYPE, &frame_type, NULL);
    if (buffer_mode_result != VDEC_RESULT_OK ||
        frame_type_result != VDEC_RESULT_OK) {
        fprintf(stderr, "decoder buffer configuration failed: mode=%d type=%d\n",
                buffer_mode_result, frame_type_result);
        goto cleanup;
    }

    memset(&ring, 0, sizeof(ring));
    memset(&sequence, 0, sizeof(sequence));
    ring.base.virtual_address = (uint32_t)(uintptr_t)mapping;
    ring.base.hardware_address = mva;
    ring.base.size = TEST_MAPPING_SIZE;
    ring.read = ring.base.virtual_address;
    ring.write = ring.base.virtual_address +
                 (do_decode ? idr_offset : fixture_size);
    ring.input_flag = do_decode ? VDEC_INPUT_FLAG_CODEC_CONFIG : 0;

    alarm(15);
    decoder_result = init_decoder(decoder, &ring, &sequence);
    alarm(0);
    if (decoder_result != VDEC_RESULT_OK) {
        fprintf(stderr, "decoder init failed: result=%d\n", decoder_result);
        goto cleanup;
    }
    decoder_initialized = 1;

    if (sequence.picture_width == 0 || sequence.picture_height == 0 ||
        sequence.picture_width > 256 || sequence.picture_height > 256) {
        fprintf(stderr, "decoder returned implausible bounded dimensions\n");
        goto cleanup;
    }

    if (do_decode) {
        ring.read = ring.base.virtual_address + idr_offset;
        ring.write = ring.base.virtual_address + fixture_size;
        ring.input_flag = (stream_probe || normal_stream_probe)
                              ? 0
                              : VDEC_INPUT_FLAG_END_OF_STREAM;
        output_mapping = (uint8_t *)mapping + TEST_MAPPING_SIZE;
        memset(output_mapping, 0x5a, TEST_MAPPING_SIZE);
        output_mva = mva + TEST_MAPPING_SIZE;

        memset(&frame, 0, sizeof(frame));
        frame.base.virtual_address = (uint32_t)(uintptr_t)output_mapping;
        frame.base.hardware_address = output_mva;
        frame.base.size = TEST_MAPPING_SIZE;
        frame.buffer_width = sequence.buffer_width;
        frame.buffer_height = sequence.buffer_height;
        frame.frame_buffer_type = frame_type;

        output_sample_size = (size_t)sequence.buffer_width *
                             sequence.buffer_height * 3U / 2U;
        if (output_sample_size == 0 ||
            output_sample_size > TEST_MAPPING_SIZE) {
            fprintf(stderr, "refusing implausible output sample size\n");
            goto cleanup;
        }
        if (stream_probe) {
            size_t stream_slot_size =
                (output_sample_size + 4095U) & ~(size_t)4095U;
            unsigned int stream_slot_count;
            uint8_t stream_slot_reported[MAX_STREAM_SLOTS] = {0};
            uint8_t stream_input_freed[MAX_STREAM_FRAMES] = {0};
            uint32_t stream_sentinel_hash =
                fnv1a32(output_mapping, output_sample_size);
            struct vdec_ring_buffer stream_rings[MAX_STREAM_FRAMES];
            struct vdec_ring_buffer stream_lookahead_ring;
            struct vdec_frame_buffer stream_frames[MAX_STREAM_SLOTS];
            struct stream_output_pool stream_pool;
            struct vdec_private_output_callback output_callback;
            struct vdec_set_decode_mode decode_mode = {
                .decode_mode = VDEC_DECODE_MODE_LOW_LATENCY,
                .display_frame_count = 0,
                .drop_frame_count = 0,
            };
            unsigned int stream_index;
            size_t input_pool_offset =
                (fixture_size + 63U) & ~(size_t)63U;
            int lookahead_access_unit_submitted = 0;
            int lookahead_access_unit_freed = 0;

            stream_slot_count = expected_stream_frames + 1U;
            if (stream_slot_size == 0 ||
                stream_slot_count > MAX_STREAM_SLOTS ||
                stream_slot_count >
                    TEST_MAPPING_SIZE / stream_slot_size) {
                fprintf(stderr, "synthetic stream exceeds output pool\n");
                goto cleanup;
            }
            if ((size_t)sequence.picture_width >
                SIZE_MAX / sequence.picture_height / 4U) {
                fprintf(stderr, "converted frame size overflow\n");
                goto cleanup;
            }
            converted_rgba_size = (size_t)sequence.picture_width *
                                  sequence.picture_height * 4U;
            converted_rgba = malloc(converted_rgba_size);
            if (converted_rgba == NULL) {
                fprintf(stderr, "could not allocate converted frame\n");
                goto cleanup;
            }
            if (dump_stream_rgba) {
                if (converted_rgba_size >
                        SIZE_MAX / expected_stream_frames ||
                    converted_rgba_size * expected_stream_frames >
                        64U * 1024U * 1024U) {
                    fprintf(stderr,
                            "converted RGBA stream exceeds 64 MiB bound\n");
                    goto cleanup;
                }
                stream_rgba_sequence_size =
                    converted_rgba_size * expected_stream_frames;
                stream_rgba_sequence = malloc(stream_rgba_sequence_size);
                if (stream_rgba_sequence == NULL) {
                    fprintf(stderr,
                            "could not allocate converted RGBA stream\n");
                    goto cleanup;
                }
            }
            memset(&cache_memory, 0, sizeof(cache_memory));
            cache_memory.memory_type = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
            cache_memory.memory_size.extended = output_sample_size;
            cache_memory.memory_va.pointer = output_mapping;
            cache_memory.memory_pa.extended = output_mva;
            cache_memory.alignment = VAL_MEM_ALIGN_64;
            cache_memory.aligned_memory_va.pointer = output_mapping;
            cache_memory.aligned_memory_pa.extended = output_mva;
            cache_memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;
            cache_memory.ion_share_fd = UINT32_MAX;
            for (stream_index = 0; stream_index < stream_slot_count;
                 ++stream_index) {
                memset(&stream_frames[stream_index], 0,
                       sizeof(stream_frames[stream_index]));
                stream_frames[stream_index].base.virtual_address =
                    (uint32_t)(uintptr_t)(
                        (uint8_t *)output_mapping +
                        (size_t)stream_index * stream_slot_size);
                stream_frames[stream_index].base.hardware_address =
                    output_mva +
                    (uint32_t)((size_t)stream_index * stream_slot_size);
                stream_frames[stream_index].base.size = stream_slot_size;
                stream_frames[stream_index].buffer_width =
                    sequence.buffer_width;
                stream_frames[stream_index].buffer_height =
                    sequence.buffer_height;
                stream_frames[stream_index].frame_buffer_type = frame_type;
            }
            for (stream_index = 0;
                 stream_index < expected_stream_frames; ++stream_index) {
                size_t access_unit_start =
                    skip_leading_access_unit_delimiter(
                        mapping, fixture_size,
                        stream_frame_starts[stream_index]);
                size_t access_unit_size =
                    access_unit_start < stream_frame_ends[stream_index]
                        ? stream_frame_ends[stream_index] - access_unit_start
                        : 0;
                static const uint8_t access_unit_delimiter[] = {
                    0x00, 0x00, 0x00, 0x01, 0x09, 0xf0,
                };
                size_t submitted_size =
                    access_unit_size + sizeof(access_unit_delimiter);
                size_t input_slot_size =
                    (submitted_size + 127U) & ~(size_t)63U;

                if (access_unit_size == 0 || submitted_size < access_unit_size ||
                    input_slot_size < submitted_size ||
                    input_pool_offset > TEST_MAPPING_SIZE - input_slot_size) {
                    fprintf(stderr,
                            "synthetic stream exceeds aligned input pool\n");
                    goto cleanup;
                }
                memcpy((uint8_t *)mapping + input_pool_offset,
                       (const uint8_t *)mapping + access_unit_start,
                       access_unit_size);
                memcpy((uint8_t *)mapping + input_pool_offset +
                           access_unit_size,
                       access_unit_delimiter,
                       sizeof(access_unit_delimiter));
                memset((uint8_t *)mapping + input_pool_offset +
                           submitted_size,
                       0, input_slot_size - submitted_size);
                memset(&stream_rings[stream_index], 0,
                       sizeof(stream_rings[stream_index]));
                stream_rings[stream_index].base.virtual_address =
                    (uint32_t)(uintptr_t)((uint8_t *)mapping +
                                          input_pool_offset);
                stream_rings[stream_index].base.hardware_address =
                    mva + (uint32_t)input_pool_offset;
                stream_rings[stream_index].base.size = input_slot_size;
                stream_rings[stream_index].read =
                    stream_rings[stream_index].base.virtual_address;
                stream_rings[stream_index].write =
                    stream_rings[stream_index].base.virtual_address +
                    submitted_size;
                stream_rings[stream_index].timestamp = stream_index;
                stream_rings[stream_index].input_flag = 0;
                input_pool_offset += input_slot_size;
            }
            {
                static const uint8_t access_unit_delimiter[] = {
                    0x00, 0x00, 0x00, 0x01, 0x09, 0xf0,
                };
                /*
                 * This all-IDR synthetic probe uses one already-owned,
                 * distinct access unit as bounded lookahead. The legacy
                 * decoder retains the final requested picture until another
                 * complete picture arrives; the lookahead picture itself is
                 * intentionally outside the requested result count.
                 */
                size_t tail_index = 0;
                size_t tail_access_unit_size =
                    stream_frame_ends[tail_index] -
                    stream_frame_starts[tail_index];
                size_t tail_submitted_size =
                    tail_access_unit_size + sizeof(access_unit_delimiter);
                size_t lookahead_slot_size =
                    (tail_submitted_size + 63U) & ~(size_t)63U;

                if (tail_access_unit_size == 0 ||
                    tail_submitted_size < tail_access_unit_size ||
                    lookahead_slot_size < tail_submitted_size ||
                    input_pool_offset >
                        TEST_MAPPING_SIZE - lookahead_slot_size) {
                    fprintf(stderr,
                            "synthetic stream has no aligned lookahead slot\n");
                    goto cleanup;
                }
                memcpy((uint8_t *)mapping + input_pool_offset,
                       (const uint8_t *)mapping +
                           stream_frame_starts[tail_index],
                       tail_access_unit_size);
                memset((uint8_t *)mapping + input_pool_offset +
                           tail_access_unit_size,
                       0, lookahead_slot_size - tail_access_unit_size);
                memcpy((uint8_t *)mapping + input_pool_offset +
                           tail_access_unit_size,
                       access_unit_delimiter,
                       sizeof(access_unit_delimiter));
                memset(&stream_lookahead_ring, 0,
                       sizeof(stream_lookahead_ring));
                stream_lookahead_ring.base.virtual_address =
                    (uint32_t)(uintptr_t)((uint8_t *)mapping +
                                          input_pool_offset);
                stream_lookahead_ring.base.hardware_address =
                    mva + (uint32_t)input_pool_offset;
                stream_lookahead_ring.base.size = lookahead_slot_size;
                stream_lookahead_ring.read =
                    stream_lookahead_ring.base.virtual_address;
                stream_lookahead_ring.write =
                    stream_lookahead_ring.base.virtual_address +
                    tail_submitted_size;
                stream_lookahead_ring.timestamp = expected_stream_frames;
                stream_lookahead_ring.input_flag = 0;
            }
            memset(&stream_pool, 0, sizeof(stream_pool));
            stream_pool.frames = stream_frames;
            stream_pool.frame_count = stream_slot_count;
            decoder_result = set_decoder_param(
                decoder, VDEC_SET_DECODE_MODE, &decode_mode, NULL);
            if (decoder_result != VDEC_RESULT_OK) {
                fprintf(stderr,
                        "low-latency stream mode failed: result=%d\n",
                        decoder_result);
                goto cleanup;
            }
            memset(&output_callback, 0, sizeof(output_callback));
            output_callback.context = &stream_pool;
            output_callback.callback = provide_stream_output;
            decoder_result = set_decoder_param(
                decoder, VDEC_SET_PRIVATE_OUTPUT_CALLBACK,
                &output_callback, NULL);
            if (decoder_result != VDEC_RESULT_OK) {
                fprintf(
                    stderr,
                    "owner-local output callback registration failed: "
                    "result=%d\n",
                    decoder_result);
                goto cleanup;
            }

            while (decoded_stream_frames < expected_stream_frames &&
                   decode_calls < expected_stream_frames * 2U + 4U) {
                struct vdec_ring_buffer *input = NULL;
                uint8_t *returned_output;
                struct vdec_frame_buffer *display_frame = NULL;
                struct vdec_ring_buffer *free_input = NULL;
                uint32_t returned_mva;
                uintptr_t returned_address;
                size_t returned_offset;
                unsigned int returned_slot;

                if (fed_stream_frames < expected_stream_frames) {
                    input = &stream_rings[fed_stream_frames];
                } else if (!lookahead_access_unit_submitted) {
                    input = &stream_lookahead_ring;
                }
                output_hash_before = stream_sentinel_hash;
                alarm(15);
                decoder_result = decode_ex_decoder(decoder, input, NULL,
                                                   NULL);
                ++decode_calls;
                alarm(0);
                if (decoder_result != VDEC_RESULT_OK) {
                    fprintf(stderr,
                            "synthetic stream decode failed: result=%d call=%u\n",
                            decoder_result, decode_calls);
                    goto cleanup;
                }
                if (input == &stream_lookahead_ring) {
                    lookahead_access_unit_submitted = 1;
                } else if (input != NULL) {
                    ++fed_stream_frames;
                }
                get_param_result = get_decoder_param(
                    decoder, VDEC_GET_FREE_INPUT_BUFFER, NULL, &free_input);
                if (get_param_result != VDEC_RESULT_OK) {
                    fprintf(stderr,
                            "could not query synthetic free input: %d\n",
                            get_param_result);
                    goto cleanup;
                }
                if (free_input == &stream_lookahead_ring) {
                    lookahead_access_unit_freed = 1;
                } else if (free_input != NULL) {
                    for (stream_index = 0;
                         stream_index < expected_stream_frames;
                         ++stream_index) {
                        if (free_input == &stream_rings[stream_index])
                            break;
                    }
                    if (stream_index == expected_stream_frames) {
                        fprintf(
                            stderr,
                            "synthetic decoder returned an unknown input\n");
                        goto cleanup;
                    }
                    if (!stream_input_freed[stream_index]) {
                        stream_input_freed[stream_index] = 1;
                        ++freed_stream_inputs;
                    }
                }
                get_param_result = get_decoder_param(
                    decoder, VDEC_GET_DISPLAY_FRAME_BUFFER, NULL,
                    &display_frame);
                if (get_param_result != VDEC_RESULT_OK) {
                    fprintf(stderr,
                            "could not query synthetic display frame: %d\n",
                            get_param_result);
                    goto cleanup;
                }
                if (display_frame == NULL) {
                    output_changed_bytes = 0;
                    if (input == NULL) {
                        fprintf(stderr,
                                "synthetic stream drain made no progress: "
                                "fed=%u decoded=%u unique=%u calls=%u "
                                "callbacks=%u supplied=%u\n",
                                fed_stream_frames, decoded_stream_frames,
                                unique_stream_frames, decode_calls,
                                stream_pool.callback_calls,
                                stream_pool.next_frame);
                        goto cleanup;
                    }
                    continue;
                }
                returned_address =
                    (uintptr_t)display_frame->base.virtual_address;
                if (returned_address < (uintptr_t)output_mapping ||
                    returned_address >
                        (uintptr_t)output_mapping + TEST_MAPPING_SIZE -
                            output_sample_size) {
                    fprintf(stderr,
                            "synthetic stream returned an unowned frame\n");
                    goto cleanup;
                }
                returned_offset =
                    returned_address - (uintptr_t)output_mapping;
                if (returned_offset % stream_slot_size != 0) {
                    fprintf(stderr,
                            "synthetic stream returned an unaligned frame\n");
                    goto cleanup;
                }
                returned_slot =
                    (unsigned int)(returned_offset / stream_slot_size);
                if (returned_slot >= stream_slot_count) {
                    fprintf(stderr,
                            "synthetic stream returned an unknown frame slot\n");
                    goto cleanup;
                }
                returned_output = (uint8_t *)returned_address;
                returned_mva = output_mva + (uint32_t)returned_offset;
                cache_memory.memory_va.pointer = returned_output;
                cache_memory.memory_pa.extended = returned_mva;
                cache_memory.aligned_memory_va.pointer = returned_output;
                cache_memory.aligned_memory_pa.extended = returned_mva;
                invalidate_result = invalidate_cache(
                    &cache_memory, sizeof(cache_memory), 1);
                (void)msync(returned_output, stream_slot_size,
                            MS_SYNC | MS_INVALIDATE);
                output_hash_after =
                    fnv1a32(returned_output, output_sample_size);
                output_changed_bytes = 0;
                memset(value_counts, 0, sizeof(value_counts));
                distinct_output_values = 0;
                for (index = 0; index < output_sample_size; ++index) {
                    uint8_t value =
                        ((const uint8_t *)returned_output)[index];

                    if (value != 0x5a)
                        ++output_changed_bytes;
                    ++value_counts[value];
                }
                if (!stream_slot_reported[returned_slot] &&
                    output_changed_bytes != 0 &&
                    output_hash_before != output_hash_after) {
                    for (index = 0; index < 256; ++index) {
                        if (value_counts[index] != 0)
                            ++distinct_output_values;
                    }
                    if (distinct_output_values < 3 ||
                        tater_mtk_yuv420_block_to_rgba8888(
                            returned_output, output_sample_size,
                            sequence.buffer_width, sequence.buffer_height,
                            sequence.picture_width, sequence.picture_height,
                            converted_rgba,
                            (size_t)sequence.picture_width * 4U,
                            converted_rgba_size) != 0) {
                        fprintf(stderr,
                                "synthetic stream returned an invalid frame\n");
                        goto cleanup;
                    }
                    for (index = 0; index < converted_rgba_size; ++index) {
                        stream_rgba_fold_hash ^= converted_rgba[index];
                        stream_rgba_fold_hash *= 16777619U;
                    }
                    converted_rgba_hash =
                        fnv1a32(converted_rgba, converted_rgba_size);
                    if (dump_stream_rgba)
                        memcpy(stream_rgba_sequence +
                                   (size_t)decoded_stream_frames *
                                       converted_rgba_size,
                               converted_rgba, converted_rgba_size);
                    for (index = 0; index < decoded_stream_frames; ++index) {
                        if (stream_rgba_hashes[index] ==
                            converted_rgba_hash)
                            break;
                    }
                    if (index == decoded_stream_frames)
                        ++unique_stream_frames;
                    stream_rgba_hashes[decoded_stream_frames] =
                        converted_rgba_hash;
                    stream_display_timestamps[decoded_stream_frames] =
                        display_frame->timestamp;
                    stream_display_slots[decoded_stream_frames] =
                        returned_slot;
                    stream_slot_reported[returned_slot] = 1;
                    ++decoded_stream_frames;
                } else if (stream_slot_reported[returned_slot]) {
                    output_changed_bytes = 0;
                }
                if (input == NULL && output_changed_bytes == 0) {
                    fprintf(stderr,
                            "synthetic stream drain made no progress: "
                            "fed=%u decoded=%u unique=%u calls=%u "
                            "callbacks=%u supplied=%u\n",
                            fed_stream_frames, decoded_stream_frames,
                            unique_stream_frames, decode_calls,
                            stream_pool.callback_calls,
                            stream_pool.next_frame);
                    goto cleanup;
                }
            }
            if (fed_stream_frames != expected_stream_frames ||
                decoded_stream_frames != expected_stream_frames) {
                fprintf(stderr,
                        "synthetic stream incomplete: fed=%u/%u frames=%u/%u "
                        "remaining=%llu\n",
                        fed_stream_frames, expected_stream_frames,
                        decoded_stream_frames, expected_stream_frames,
                        (unsigned long long)(
                            fixture_size -
                            (fed_stream_frames == 0
                                 ? idr_offset
                                 : stream_frame_ends[
                                       fed_stream_frames - 1U])));
                goto cleanup;
            }
            if (freed_stream_inputs != expected_stream_frames) {
                fprintf(stderr,
                        "synthetic stream retained input descriptors: %u/%u "
                        "released\n",
                        freed_stream_inputs, expected_stream_frames);
                goto cleanup;
            }
            if (!lookahead_access_unit_submitted ||
                !lookahead_access_unit_freed) {
                fprintf(stderr,
                        "synthetic stream did not retire its bounded "
                        "lookahead "
                        "unit\n");
                goto cleanup;
            }
            if (unique_stream_frames != expected_stream_frames ||
                stream_pool.callback_calls != expected_stream_frames ||
                stream_pool.next_frame != expected_stream_frames) {
                fprintf(stderr, "synthetic stream frame hashes:");
                for (stream_index = 0;
                     stream_index < decoded_stream_frames; ++stream_index)
                    fprintf(stderr, " %u(slot=%u,ts=%u)",
                            stream_rgba_hashes[stream_index],
                            stream_display_slots[stream_index],
                            stream_display_timestamps[stream_index]);
                fprintf(stderr,
                        "\nsynthetic stream did not produce one distinct "
                        "frame per requested access unit "
                        "(callbacks=%u supplied=%u)\n",
                        stream_pool.callback_calls,
                        stream_pool.next_frame);
                goto cleanup;
            }
            stream_callback_calls = stream_pool.callback_calls;
            stream_supplied_outputs = stream_pool.next_frame;
            if (dump_stream_rgba &&
                write_exact_file(argv[4], stream_rgba_sequence,
                                 stream_rgba_sequence_size) != 0) {
                fprintf(stderr,
                        "could not write converted RGBA stream\n");
                goto cleanup;
            }
            ring.read = ring.base.virtual_address + fixture_size;
            ring.write = ring.read;
        } else if (normal_stream_probe) {
            size_t stream_slot_size =
                (output_sample_size + 4095U) & ~(size_t)4095U;
            unsigned int stream_slot_count = expected_stream_frames + 4U;
            uint8_t stream_slot_reported[MAX_STREAM_SLOTS] = {0};
            uint8_t normal_input_freed[MAX_STREAM_FRAMES] = {0};
            struct vdec_ring_buffer normal_rings[MAX_STREAM_FRAMES];
            struct vdec_frame_buffer stream_frames[MAX_STREAM_SLOTS];
            struct vdec_set_decode_mode decode_mode = {
                .decode_mode = VDEC_DECODE_MODE_NORMAL,
                .display_frame_count = 0,
                .drop_frame_count = 0,
            };
            unsigned int supplied_frames = 0;
            unsigned int fed_normal_frames = 0;
            unsigned int normal_freed_inputs = 0;
            unsigned int normal_direct_outputs = 0;
            unsigned int stream_index;
            size_t input_pool_offset =
                (fixture_size + 63U) & ~(size_t)63U;
            uint32_t stream_sentinel_hash =
                fnv1a32(output_mapping, output_sample_size);

            if (stream_slot_size == 0 ||
                stream_slot_count > MAX_STREAM_SLOTS ||
                stream_slot_count >
                    TEST_MAPPING_SIZE / stream_slot_size) {
                fprintf(stderr, "normal stream exceeds output pool\n");
                goto cleanup;
            }
            if ((size_t)sequence.picture_width >
                SIZE_MAX / sequence.picture_height / 4U) {
                fprintf(stderr, "converted frame size overflow\n");
                goto cleanup;
            }
            converted_rgba_size = (size_t)sequence.picture_width *
                                  sequence.picture_height * 4U;
            converted_rgba = malloc(converted_rgba_size);
            if (converted_rgba == NULL ||
                converted_rgba_size >
                    SIZE_MAX / expected_stream_frames ||
                converted_rgba_size * expected_stream_frames >
                    64U * 1024U * 1024U) {
                fprintf(stderr,
                        "normal RGBA stream exceeds bounded memory\n");
                goto cleanup;
            }
            stream_rgba_sequence_size =
                converted_rgba_size * expected_stream_frames;
            stream_rgba_sequence = malloc(stream_rgba_sequence_size);
            if (stream_rgba_sequence == NULL) {
                fprintf(stderr,
                        "could not allocate normal converted stream\n");
                goto cleanup;
            }
            memset(&cache_memory, 0, sizeof(cache_memory));
            cache_memory.memory_type = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
            cache_memory.memory_size.extended = output_sample_size;
            cache_memory.alignment = VAL_MEM_ALIGN_64;
            cache_memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;
            cache_memory.ion_share_fd = UINT32_MAX;
            for (stream_index = 0;
                 stream_index < stream_slot_count; ++stream_index) {
                memset(&stream_frames[stream_index], 0,
                       sizeof(stream_frames[stream_index]));
                stream_frames[stream_index].base.virtual_address =
                    (uint32_t)(uintptr_t)(
                        (uint8_t *)output_mapping +
                        (size_t)stream_index * stream_slot_size);
                stream_frames[stream_index].base.hardware_address =
                    output_mva +
                    (uint32_t)((size_t)stream_index * stream_slot_size);
                stream_frames[stream_index].base.size =
                    TEST_MAPPING_SIZE -
                    (size_t)stream_index * stream_slot_size;
                stream_frames[stream_index].buffer_width =
                    sequence.buffer_width;
                stream_frames[stream_index].buffer_height =
                    sequence.buffer_height;
                stream_frames[stream_index].frame_buffer_type = frame_type;
            }
            for (stream_index = 0;
                 stream_index < expected_stream_frames; ++stream_index) {
                size_t access_unit_start =
                    skip_leading_access_unit_delimiter(
                        mapping, fixture_size,
                        stream_frame_starts[stream_index]);
                size_t access_unit_size =
                    access_unit_start < stream_frame_ends[stream_index]
                        ? stream_frame_ends[stream_index] -
                              access_unit_start
                        : 0;
                size_t submitted_size = access_unit_size;
                size_t input_slot_size =
                    (submitted_size + 127U) & ~(size_t)63U;

                if (access_unit_size == 0 ||
                    submitted_size < access_unit_size ||
                    input_slot_size < submitted_size ||
                    input_pool_offset >
                        TEST_MAPPING_SIZE - input_slot_size) {
                    fprintf(stderr,
                            "normal stream exceeds aligned input pool\n");
                    goto cleanup;
                }
                memcpy((uint8_t *)mapping + input_pool_offset,
                       (const uint8_t *)mapping + access_unit_start,
                       access_unit_size);
                memset((uint8_t *)mapping + input_pool_offset +
                           submitted_size,
                       0, input_slot_size - submitted_size);
                memset(&normal_rings[stream_index], 0,
                       sizeof(normal_rings[stream_index]));
                normal_rings[stream_index].base.virtual_address =
                    (uint32_t)(uintptr_t)(
                        (uint8_t *)mapping + input_pool_offset);
                normal_rings[stream_index].base.hardware_address =
                    mva + (uint32_t)input_pool_offset;
                normal_rings[stream_index].base.size =
                    TEST_MAPPING_SIZE - input_pool_offset;
                normal_rings[stream_index].read =
                    normal_rings[stream_index].base.virtual_address;
                normal_rings[stream_index].write =
                    normal_rings[stream_index].base.virtual_address +
                    submitted_size;
                normal_rings[stream_index].timestamp = stream_index;
                normal_rings[stream_index].input_flag = 0;
                input_pool_offset += input_slot_size;
            }
            (void)decode_mode;

            while (decoded_stream_frames < expected_stream_frames &&
                   decode_calls < stream_slot_count) {
                struct vdec_ring_buffer *input = NULL;
                struct vdec_ring_buffer *free_input = NULL;
                struct vdec_frame_buffer *display_frame = NULL;
                unsigned int outputs_before = decoded_stream_frames;

                if (fed_normal_frames < expected_stream_frames)
                    input = &normal_rings[fed_normal_frames];
                if (supplied_frames >= stream_slot_count) {
                    fprintf(stderr,
                            "normal stream exhausted bounded frame pool\n");
                    goto cleanup;
                }
                alarm(15);
                decoder_result = decode_decoder(
                    decoder, input, &stream_frames[supplied_frames]);
                alarm(0);
                ++decode_calls;
                ++supplied_frames;
                if (decoder_result != VDEC_RESULT_OK) {
                    fprintf(stderr,
                            "normal stream decode failed: result=%d "
                            "call=%u\n",
                            decoder_result, decode_calls);
                    goto cleanup;
                }
                if (input != NULL)
                    ++fed_normal_frames;
                if (input != NULL) {
                    get_param_result = get_decoder_param(
                        decoder, VDEC_GET_FREE_INPUT_BUFFER, NULL,
                        &free_input);
                    if (get_param_result != VDEC_RESULT_OK) {
                        fprintf(stderr,
                                "could not query normal free input: %d\n",
                                get_param_result);
                        goto cleanup;
                    }
                    if (free_input != NULL) {
                        for (stream_index = 0;
                             stream_index < expected_stream_frames;
                             ++stream_index) {
                            if (free_input ==
                                &normal_rings[stream_index])
                                break;
                        }
                        if (stream_index == expected_stream_frames) {
                            fprintf(stderr,
                                    "normal decoder returned unknown "
                                    "input\n");
                            goto cleanup;
                        }
                        if (!normal_input_freed[stream_index]) {
                            normal_input_freed[stream_index] = 1;
                            ++normal_freed_inputs;
                        }
                    }
                }

                do {
                    uint8_t *returned_output;
                    uintptr_t returned_address;
                    size_t returned_offset;
                    unsigned int returned_slot;
                    uint32_t returned_mva;
                    uint32_t slot_changed_bytes = 0;
                    uint32_t slot_value_counts[256] = {0};
                    uint32_t slot_distinct_values = 0;

                    display_frame = NULL;
                    get_param_result = get_decoder_param(
                        decoder, VDEC_GET_DISPLAY_FRAME_BUFFER, NULL,
                        &display_frame);
                    if (get_param_result != VDEC_RESULT_OK) {
                        fprintf(stderr,
                                "could not query normal display frame: %d\n",
                                get_param_result);
                        goto cleanup;
                    }
                    if (display_frame == NULL) {
                        for (stream_index = 0;
                             stream_index < supplied_frames;
                             ++stream_index) {
                            uint8_t *candidate;
                            uint32_t candidate_mva;

                            if (stream_slot_reported[stream_index])
                                continue;
                            candidate =
                                (uint8_t *)(uintptr_t)
                                    stream_frames[stream_index]
                                        .base.virtual_address;
                            candidate_mva =
                                stream_frames[stream_index]
                                    .base.hardware_address;
                            cache_memory.memory_va.pointer = candidate;
                            cache_memory.memory_pa.extended = candidate_mva;
                            cache_memory.aligned_memory_va.pointer =
                                candidate;
                            cache_memory.aligned_memory_pa.extended =
                                candidate_mva;
                            invalidate_result = invalidate_cache(
                                &cache_memory, sizeof(cache_memory), 1);
                            (void)msync(candidate, stream_slot_size,
                                        MS_SYNC | MS_INVALIDATE);
                            if (fnv1a32(candidate, output_sample_size) !=
                                stream_sentinel_hash) {
                                display_frame =
                                    &stream_frames[stream_index];
                                ++normal_direct_outputs;
                                break;
                            }
                        }
                        if (display_frame == NULL)
                            break;
                    }
                    returned_address =
                        (uintptr_t)display_frame->base.virtual_address;
                    if (returned_address < (uintptr_t)output_mapping ||
                        returned_address >
                            (uintptr_t)output_mapping + TEST_MAPPING_SIZE -
                                output_sample_size) {
                        fprintf(stderr,
                                "normal stream returned unowned frame\n");
                        goto cleanup;
                    }
                    returned_offset =
                        returned_address - (uintptr_t)output_mapping;
                    if (returned_offset % stream_slot_size != 0) {
                        fprintf(stderr,
                                "normal stream returned unaligned frame\n");
                        goto cleanup;
                    }
                    returned_slot =
                        (unsigned int)(returned_offset / stream_slot_size);
                    if (returned_slot >= stream_slot_count ||
                        stream_slot_reported[returned_slot]) {
                        fprintf(stderr,
                                "normal stream returned duplicate or "
                                "unknown slot\n");
                        goto cleanup;
                    }
                    returned_output = (uint8_t *)returned_address;
                    returned_mva =
                        output_mva + (uint32_t)returned_offset;
                    cache_memory.memory_va.pointer = returned_output;
                    cache_memory.memory_pa.extended = returned_mva;
                    cache_memory.aligned_memory_va.pointer =
                        returned_output;
                    cache_memory.aligned_memory_pa.extended = returned_mva;
                    invalidate_result = invalidate_cache(
                        &cache_memory, sizeof(cache_memory), 1);
                    (void)msync(returned_output, stream_slot_size,
                                MS_SYNC | MS_INVALIDATE);
                    for (index = 0; index < output_sample_size; ++index) {
                        uint8_t value = returned_output[index];

                        if (value != 0x5a)
                            ++slot_changed_bytes;
                        ++slot_value_counts[value];
                    }
                    for (index = 0; index < 256; ++index) {
                        if (slot_value_counts[index] != 0)
                            ++slot_distinct_values;
                    }
                    if (slot_changed_bytes == 0 ||
                        slot_distinct_values < 3 ||
                        tater_mtk_yuv420_block_to_rgba8888(
                            returned_output, output_sample_size,
                            sequence.buffer_width, sequence.buffer_height,
                            sequence.picture_width, sequence.picture_height,
                            converted_rgba,
                            (size_t)sequence.picture_width * 4U,
                            converted_rgba_size) != 0) {
                        fprintf(stderr,
                                "normal stream returned invalid frame\n");
                        goto cleanup;
                    }
                    output_changed_bytes = slot_changed_bytes;
                    distinct_output_values = slot_distinct_values;
                    output_hash_after =
                        fnv1a32(returned_output, output_sample_size);
                    converted_rgba_hash =
                        fnv1a32(converted_rgba, converted_rgba_size);
                    memcpy(stream_rgba_sequence +
                               (size_t)decoded_stream_frames *
                                   converted_rgba_size,
                           converted_rgba, converted_rgba_size);
                    for (index = 0;
                         index < decoded_stream_frames; ++index) {
                        if (stream_rgba_hashes[index] ==
                            converted_rgba_hash)
                            break;
                    }
                    if (index == decoded_stream_frames)
                        ++unique_stream_frames;
                    for (index = 0; index < converted_rgba_size; ++index) {
                        stream_rgba_fold_hash ^= converted_rgba[index];
                        stream_rgba_fold_hash *= 16777619U;
                    }
                    stream_rgba_hashes[decoded_stream_frames] =
                        converted_rgba_hash;
                    stream_display_timestamps[decoded_stream_frames] =
                        display_frame->timestamp;
                    stream_display_slots[decoded_stream_frames] =
                        returned_slot;
                    stream_slot_reported[returned_slot] = 1;
                    ++decoded_stream_frames;
                } while (decoded_stream_frames < expected_stream_frames);

                if (decoded_stream_frames == outputs_before &&
                    input == NULL) {
                    fprintf(stderr,
                            "normal stream made no progress: call=%u "
                            "frames=%u\n",
                            decode_calls, decoded_stream_frames);
                    goto cleanup;
                }
            }
            if (decoded_stream_frames != expected_stream_frames ||
                fed_normal_frames != expected_stream_frames) {
                fprintf(stderr,
                        "normal stream incomplete: frames=%u/%u "
                        "fed=%u/%u calls=%u\n",
                        decoded_stream_frames, expected_stream_frames,
                        fed_normal_frames, expected_stream_frames,
                        decode_calls);
                goto cleanup;
            }
            if (normal_freed_inputs != expected_stream_frames) {
                fprintf(stderr,
                        "normal stream retained input descriptors: %u/%u "
                        "released\n",
                        normal_freed_inputs, expected_stream_frames);
                goto cleanup;
            }
            freed_stream_inputs = normal_freed_inputs;
            stream_supplied_outputs = supplied_frames;
            stream_callback_calls = normal_direct_outputs;
            if (write_exact_file(argv[4], stream_rgba_sequence,
                                 stream_rgba_sequence_size) != 0) {
                fprintf(stderr,
                        "could not write normal converted stream\n");
                goto cleanup;
            }
            ring.read = ring.base.virtual_address + fixture_size;
            ring.write = ring.read;
        } else {
        output_hash_before = fnv1a32(output_mapping, output_sample_size);
        alarm(15);
        decoder_result = decode_decoder(decoder, &ring, &frame);
        decode_calls = 1;
        alarm(0);
        if (decoder_result != VDEC_RESULT_OK) {
            fprintf(stderr, "decoder frame failed: result=%d\n",
                    decoder_result);
            goto cleanup;
        }
        memset(&cache_memory, 0, sizeof(cache_memory));
        cache_memory.memory_type = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
        cache_memory.memory_size.extended = output_sample_size;
        cache_memory.memory_va.pointer = output_mapping;
        cache_memory.memory_pa.extended = output_mva;
        cache_memory.alignment = VAL_MEM_ALIGN_64;
        cache_memory.aligned_memory_va.pointer = output_mapping;
        cache_memory.aligned_memory_pa.extended = output_mva;
        cache_memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;
        cache_memory.ion_share_fd = UINT32_MAX;
        invalidate_result = invalidate_cache(&cache_memory,
                                             sizeof(cache_memory), 1);
        (void)msync(output_mapping, TEST_MAPPING_SIZE,
                    MS_SYNC | MS_INVALIDATE);
        output_hash_after = fnv1a32(output_mapping, output_sample_size);
        for (index = 0; index < output_sample_size; ++index) {
            if (((const uint8_t *)output_mapping)[index] != 0x5a)
                ++output_changed_bytes;
        }
        if (output_changed_bytes == 0 ||
            output_hash_before == output_hash_after) {
            alarm(15);
            decoder_result = decode_decoder(decoder, NULL, &frame);
            decode_calls = 2;
            alarm(0);
            if (decoder_result != VDEC_RESULT_OK) {
                fprintf(stderr, "decoder flush failed: result=%d\n",
                        decoder_result);
                goto cleanup;
            }
            invalidate_result = invalidate_cache(&cache_memory,
                                                 sizeof(cache_memory), 1);
            (void)msync(output_mapping, TEST_MAPPING_SIZE,
                        MS_SYNC | MS_INVALIDATE);
            output_hash_after = fnv1a32(output_mapping,
                                        output_sample_size);
            output_changed_bytes = 0;
            for (index = 0; index < output_sample_size; ++index) {
                if (((const uint8_t *)output_mapping)[index] != 0x5a)
                    ++output_changed_bytes;
            }
        }
        if (output_changed_bytes == 0 ||
            output_hash_before == output_hash_after) {
            fprintf(stderr,
                    "decoder returned without observable frame output: "
                    "read_offset=%llu display=%ux%u pitch=%u status=%u "
                    "hash_before=%u hash_after=%u\n",
                    (unsigned long long)(ring.read -
                                         ring.base.virtual_address),
                    frame.display_width, frame.display_height,
                    frame.display_pitch, frame.frame_buffer_status,
                    output_hash_before, output_hash_after);
            fprintf(stderr, "cache invalidation result=%d\n",
                    invalidate_result);
            goto cleanup;
        }
        for (index = 0; index < output_sample_size; ++index)
            ++value_counts[((const uint8_t *)output_mapping)[index]];
        for (index = 0; index < 256; ++index) {
            if (value_counts[index] != 0)
                ++distinct_output_values;
        }
        if (distinct_output_values < 3) {
            fprintf(stderr, "decoded frame has implausibly few output values\n");
            goto cleanup;
        }
        if (layout_probe &&
            read_exact_file(argv[3], &expected_yuv, output_sample_size) != 0) {
            fprintf(stderr,
                    "expected YUV420P must exactly match the decoded buffer size\n");
            goto cleanup;
        }
        if (layout_probe || dump_rgba) {
            if ((size_t)sequence.picture_width >
                SIZE_MAX / sequence.picture_height / 4U) {
                fprintf(stderr, "converted frame size overflow\n");
                goto cleanup;
            }
            converted_rgba_size = (size_t)sequence.picture_width *
                                  sequence.picture_height * 4U;
            converted_rgba = malloc(converted_rgba_size);
            if (converted_rgba == NULL ||
                tater_mtk_yuv420_block_to_rgba8888(
                    output_mapping, output_sample_size,
                    sequence.buffer_width, sequence.buffer_height,
                    sequence.picture_width, sequence.picture_height,
                    converted_rgba, (size_t)sequence.picture_width * 4U,
                    converted_rgba_size) != 0) {
                fprintf(stderr, "block-YUV conversion failed\n");
                goto cleanup;
            }
            converted_rgba_hash =
                fnv1a32(converted_rgba, converted_rgba_size);
            if (dump_rgba &&
                write_exact_file(argv[3], converted_rgba,
                                 converted_rgba_size) != 0) {
                fprintf(stderr, "could not write converted RGBA frame\n");
                goto cleanup;
            }
        }
        }
    }

    printf("{\"format\":\"h264\",\"fixture_bytes\":%zu,"
           "\"sequence_buffer\":\"%ux%u\",\"picture\":\"%ux%u\","
           "\"bit_depth\":%d,\"read_offset\":%llu,"
           "\"buffer_mode\":\"external-shared\","
           "\"frame_type\":\"yuv420-block-16x1\",\"init\":\"ok\","
           "\"decode\":\"%s\",\"output_changed_bytes\":%u,"
           "\"output_hash\":%u,\"cache_invalidate\":%d,"
           "\"decode_calls\":%u,\"distinct_output_values\":%u",
           fixture_size, sequence.buffer_width, sequence.buffer_height,
           sequence.picture_width, sequence.picture_height,
           sequence.luma_bit_depth,
           (unsigned long long)(ring.read - ring.base.virtual_address),
           do_decode ? "ok" : "not-requested", output_changed_bytes,
           output_hash_after, invalidate_result, decode_calls,
           distinct_output_values);
    if (layout_probe) {
        printf(",\"expected_hash\":%u,\"converted_rgba_bytes\":%zu,"
               "\"converted_rgba_hash\":%u",
               fnv1a32(expected_yuv, output_sample_size),
               converted_rgba_size, converted_rgba_hash);
        report_layout_scores(output_mapping, expected_yuv,
                             sequence.buffer_width, sequence.buffer_height);
    }
    if (dump_rgba) {
        printf(",\"rgba_dump_bytes\":%zu,\"converted_rgba_hash\":%u",
               converted_rgba_size, converted_rgba_hash);
    }
    if (stream_probe) {
        printf(",\"synthetic_stream_frames\":%u,"
               "\"synthetic_stream_expected\":%u,"
               "\"synthetic_stream_unique_frames\":%u,"
               "\"synthetic_stream_freed_inputs\":%u,"
               "\"synthetic_stream_callback_calls\":%u,"
               "\"synthetic_stream_supplied_outputs\":%u,"
               "\"stream_rgba_fold_hash\":%u,"
               "\"input_fully_consumed\":true",
               decoded_stream_frames, expected_stream_frames,
               unique_stream_frames, freed_stream_inputs,
               stream_callback_calls, stream_supplied_outputs,
               stream_rgba_fold_hash);
        printf(",\"stream_rgba_hashes\":[");
        for (index = 0; index < decoded_stream_frames; ++index) {
            if (index != 0)
                putchar(',');
            printf("%u", stream_rgba_hashes[index]);
        }
        printf("],\"stream_display_slots\":[");
        for (index = 0; index < decoded_stream_frames; ++index) {
            if (index != 0)
                putchar(',');
            printf("%u", stream_display_slots[index]);
        }
        printf("],\"stream_display_timestamps\":[");
        for (index = 0; index < decoded_stream_frames; ++index) {
            if (index != 0)
                putchar(',');
            printf("%u", stream_display_timestamps[index]);
        }
        putchar(']');
        if (dump_stream_rgba)
            printf(",\"rgba_stream_dump_bytes\":%zu",
                   stream_rgba_sequence_size);
    }
    if (normal_stream_probe) {
        printf(",\"normal_stream_frames\":%u,"
               "\"normal_stream_expected\":%u,"
               "\"normal_stream_unique_frames\":%u,"
               "\"normal_stream_freed_inputs\":%u,"
               "\"normal_stream_supplied_outputs\":%u,"
               "\"normal_stream_direct_outputs\":%u,"
               "\"stream_rgba_fold_hash\":%u,"
               "\"rgba_stream_dump_bytes\":%zu,"
               "\"input_fully_consumed\":true",
               decoded_stream_frames, expected_stream_frames,
               unique_stream_frames, freed_stream_inputs,
               stream_supplied_outputs, stream_callback_calls,
               stream_rgba_fold_hash,
               stream_rgba_sequence_size);
        printf(",\"stream_rgba_hashes\":[");
        for (index = 0; index < decoded_stream_frames; ++index) {
            if (index != 0)
                putchar(',');
            printf("%u", stream_rgba_hashes[index]);
        }
        printf("],\"stream_display_slots\":[");
        for (index = 0; index < decoded_stream_frames; ++index) {
            if (index != 0)
                putchar(',');
            printf("%u", stream_display_slots[index]);
        }
        printf("],\"stream_display_timestamps\":[");
        for (index = 0; index < decoded_stream_frames; ++index) {
            if (index != 0)
                putchar(',');
            printf("%u", stream_display_timestamps[index]);
        }
        putchar(']');
    }
    puts("}");
    result = 0;

cleanup:
    alarm(0);
    if (decoder_initialized)
        (void)deinit_decoder(decoder);
    if (decoder_created)
        (void)release_decoder(decoder);
    if (mva_ready)
        (void)free_mva(mva_handle, (uint32_t)(uintptr_t)mapping, mva,
                       TOTAL_MAPPING_SIZE, &config);
    if (mva_handle != NULL)
        (void)deinit_mva(mva_handle);
    if (mapping != MAP_FAILED)
        (void)munmap(mapping, TOTAL_MAPPING_SIZE);
    free(expected_yuv);
    free(converted_rgba);
    free(stream_rgba_sequence);
    if (decoder_library != NULL)
        dlclose(decoder_library);
    if (utility != NULL)
        dlclose(utility);
    return result;
}

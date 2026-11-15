#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VDEC_QUERY_VIDEO_FORMAT 1U
#define VDEC_QUERY_FRAME_BUFFER_TYPE 0U
#define VDEC_QUERY_BUFFER_CONTROL 4U
#define VDEC_QUERY_FEATURE_SUPPORTED 5U
#define VDEC_FEATURE_QUEUE_INPUT_BUFFER 1U
#define VDEC_FORMAT_MPEG2 (1U << 5)
#define VDEC_FORMAT_MPEG4 (1U << 6)
#define VDEC_FORMAT_H263 (1U << 7)
#define VDEC_FORMAT_H264 (1U << 8)
#define VDEC_FORMAT_H265 (1U << 9)

struct vdec_query_video_format {
    uint32_t video_format;
    uint32_t profile;
    uint32_t level;
    uint32_t resolution;
    uint32_t width;
    uint32_t height;
    uint32_t stride_align;
    uint32_t slice_height_align;
    uint32_t pixel_format;
    uint32_t decoder_type;
    uint32_t compatible_flag;
};

struct format_request {
    const char *name;
    uint32_t format;
};

typedef int32_t (*query_capability_fn)(uint32_t, void *, void *);

_Static_assert(sizeof(struct vdec_query_video_format) == 44,
               "unexpected public VDEC query ABI size");
_Static_assert(sizeof(query_capability_fn) == sizeof(void *),
               "function pointer cannot be loaded safely");

static const struct format_request formats[] = {
    {"mpeg2", VDEC_FORMAT_MPEG2},
    {"mpeg4", VDEC_FORMAT_MPEG4},
    {"h263", VDEC_FORMAT_H263},
    {"h264", VDEC_FORMAT_H264},
    {"h265", VDEC_FORMAT_H265},
};

int main(int argc, char **argv) {
    query_capability_fn query_capability;
    void *library;
    void *symbol;
    size_t index;

    if (argc != 2 || strcmp(argv[1], "--query-public-capabilities") != 0) {
        fprintf(stderr, "refusing: use --query-public-capabilities\n");
        return 2;
    }

    library = dlopen("libvcodecdrv.so", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    symbol = dlsym(library, "eVDecDrvQueryCapability");
    if (symbol == NULL) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(library);
        return 2;
    }
    memcpy(&query_capability, &symbol, sizeof(query_capability));

    printf("{\"abi_size\":%zu,\"formats\":[",
           sizeof(struct vdec_query_video_format));
    for (index = 0; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        struct vdec_query_video_format input;
        struct vdec_query_video_format output;

        memset(&input, 0, sizeof(input));
        memset(&output, 0, sizeof(output));
        input.video_format = formats[index].format;
        (void)query_capability(VDEC_QUERY_VIDEO_FORMAT, &input, &output);
        if (index)
            putchar(',');
        printf("{\"name\":\"%s\",\"requested\":%u,\"reported\":%u,"
               "\"profiles\":%u,\"max_level\":%u,\"resolution\":%u,"
               "\"max_width\":%u,\"max_height\":%u,\"stride_align\":%u,"
               "\"slice_height_align\":%u,\"pixel_format\":%u,"
               "\"decoder_type\":%u,\"compatible\":%u}",
               formats[index].name, input.video_format, output.video_format,
               output.profile, output.level, output.resolution, output.width,
               output.height, output.stride_align, output.slice_height_align,
               output.pixel_format, output.decoder_type,
               output.compatible_flag);
    }
    printf("],\"frame_types\":[");
    for (index = 0; index < 5; ++index) {
        uint32_t input = 1U << index;
        uint32_t output = 0;
        int32_t query_result = query_capability(
            VDEC_QUERY_FRAME_BUFFER_TYPE, &input, &output);

        if (index)
            putchar(',');
        printf("{\"requested\":%u,\"reported\":%u,\"result\":%d}",
               input, output, query_result);
    }
    printf("],\"buffer_modes\":[");
    for (index = 0; index < 20; ++index) {
        uint32_t input = 1U << index;
        uint32_t output = 0;
        int32_t query_result = query_capability(
            VDEC_QUERY_BUFFER_CONTROL, &input, &output);

        if (index)
            putchar(',');
        printf("{\"requested\":%u,\"reported\":%u,\"result\":%d}",
               input, output, query_result);
    }
    {
        uint32_t input = VDEC_FEATURE_QUEUE_INPUT_BUFFER;
        uint32_t output = 0;
        int32_t query_result = query_capability(
            VDEC_QUERY_FEATURE_SUPPORTED, &input, &output);

        printf("],\"queue_input_feature\":{"
               "\"requested\":%u,\"reported\":%u,\"result\":%d}}\n",
               input, output, query_result);
    }
    dlclose(library);
    return 0;
}

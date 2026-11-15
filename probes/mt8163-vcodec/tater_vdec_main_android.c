// SPDX-License-Identifier: MIT

#include "tater_vdec_android.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef TATER_CRONOS_ABI
#ifdef TATER_GPU_ROOK
#include "../mt8163-gpu/rook_target.h"
#define cronos_gpu_target_allowed rook_gpu_target_allowed
#elif defined(TATER_GPU_CROWN)
#include "../mt8163-gpu/crown_target.h"
#define cronos_gpu_target_allowed crown_gpu_target_allowed
#else
#include "../mt8163-gpu/cronos_target.h"
#endif
static int finish(int result) {
    /* OMX buffers, states and the component have been torn down by the
     * client. The owner's DSOs stay resident until this isolated process exits. */
    fflush(NULL);
    _exit(result);
}
#endif

static int parse_uint32(const char *value, uint32_t *result) {
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0' || result == NULL)
        return -1;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed > UINT32_MAX)
        return -1;
    *result = (uint32_t)parsed;
    return 0;
}

static void usage(void) {
    fputs(
        "usage:\n"
        "  tater-vdec --capabilities\n"
        "  tater-vdec --decode-h264-batch COMPONENT_LIBRARY INPUT.h264 "
        "WIDTH HEIGHT FRAME_COUNT OUTPUT.yuv\n"
        "  tater-vdec --decode-h264-batch-rgba COMPONENT_LIBRARY INPUT.h264 "
        "WIDTH HEIGHT FRAME_COUNT OUTPUT.rgba\n"
        "  tater-vdec --stream-h264-rgba COMPONENT_LIBRARY WIDTH HEIGHT FPS\n"
        "  tater-vdec --stream-h264-rgba-v2 COMPONENT_LIBRARY WIDTH HEIGHT FPS\n",
        stderr);
}

int main(int argc, char **argv) {
    struct tater_vdec_batch request;

#ifdef TATER_CRONOS_ABI
    if (!cronos_gpu_target_allowed()) {
        fputs("refusing: Cronos decoder kernel/product/panel preflight failed\n", stderr);
        return 2;
    }
#endif

    if (argc == 2 && strcmp(argv[1], "--capabilities") == 0) {
        printf("{\"adapter\":\"tater-vdec\",\"version\":2,"
               "\"codec\":\"h264\",\"input\":\"annex-b-aud-batch\","
               "\"outputs\":[\"yuv420p\",\"rgba8888\"],"
               "\"batch_version\":1,\"stream_versions\":[1,2],"
               "\"stream_input\":\"le32-length-prefixed-access-units\","
               "\"stream_v2_input\":\"le32-size-i64-pts-us-access-unit\","
               "\"stream_v2_output\":\"TVO2-header-and-timestamped-rgba\","
               "\"presentation_order\":true,"
               "\"max_width\":%u,\"max_height\":%u,"
               "\"max_batch_frames\":%u}\n",
               TATER_VDEC_MAX_WIDTH, TATER_VDEC_MAX_HEIGHT,
               TATER_VDEC_MAX_BATCH_FRAMES);
        return 0;
    }
    if (argc == 6 &&
        (strcmp(argv[1], "--stream-h264-rgba") == 0 ||
         strcmp(argv[1], "--stream-h264-rgba-v2") == 0)) {
        uint32_t width;
        uint32_t height;
        uint32_t fps;
        int binary_output;
        int result;

        if (parse_uint32(argv[3], &width) != 0 ||
            parse_uint32(argv[4], &height) != 0 ||
            parse_uint32(argv[5], &fps) != 0) {
            usage();
            return 2;
        }
        (void)fflush(stdout);
        binary_output = dup(STDOUT_FILENO);
        if (binary_output < 0 ||
            dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
            if (binary_output >= 0)
                close(binary_output);
            return 1;
        }
        if (strcmp(argv[1], "--stream-h264-rgba-v2") == 0)
            result = tater_vdec_stream_rgba8888_v2(
                argv[2], width, height, fps, STDIN_FILENO, binary_output);
        else
            result = tater_vdec_stream_rgba8888(
                argv[2], width, height, fps, STDIN_FILENO, binary_output);
        close(binary_output);
#ifdef TATER_CRONOS_ABI
        return finish(result);
#else
        return result;
#endif
    }
    if (argc != 8 ||
        (strcmp(argv[1], "--decode-h264-batch") != 0 &&
         strcmp(argv[1], "--decode-h264-batch-rgba") != 0)) {
        usage();
        return 2;
    }
    memset(&request, 0, sizeof(request));
    request.component_library = argv[2];
    request.input_annex_b = argv[3];
    request.output_path = argv[7];
    request.output_rgba8888 =
        strcmp(argv[1], "--decode-h264-batch-rgba") == 0;
    if (parse_uint32(argv[4], &request.width) != 0 ||
        parse_uint32(argv[5], &request.height) != 0 ||
        parse_uint32(argv[6], &request.frame_count) != 0) {
        usage();
        return 2;
    }
#ifdef TATER_CRONOS_ABI
    return finish(tater_vdec_decode_batch(&request));
#else
    return tater_vdec_decode_batch(&request);
#endif
}

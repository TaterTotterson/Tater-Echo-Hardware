// SPDX-License-Identifier: MIT

#include "tater_vdec_android.h"

#include "omx_probe_android.h"

#include <stdio.h>
#include <string.h>

static int valid_path(const char *path) {
    return path != NULL && path[0] != '\0' && strlen(path) <= 4096U;
}

int tater_vdec_decode_batch(const struct tater_vdec_batch *request) {
    char width[16];
    char height[16];
    char frames[16];
    char *arguments[9];

    if (request == NULL || !valid_path(request->component_library) ||
        !valid_path(request->input_annex_b) ||
        !valid_path(request->output_path) ||
        request->width < TATER_VDEC_MIN_DIMENSION ||
        request->width > TATER_VDEC_MAX_WIDTH ||
        request->height < TATER_VDEC_MIN_DIMENSION ||
        request->height > TATER_VDEC_MAX_HEIGHT ||
        (request->width & 1U) != 0 || (request->height & 1U) != 0 ||
        request->frame_count < TATER_VDEC_MIN_BATCH_FRAMES ||
        request->frame_count > TATER_VDEC_MAX_BATCH_FRAMES)
        return 2;
    if (strcmp(request->input_annex_b, request->output_path) == 0)
        return 2;

    (void)snprintf(width, sizeof(width), "%u", request->width);
    (void)snprintf(height, sizeof(height), "%u", request->height);
    (void)snprintf(frames, sizeof(frames), "%u", request->frame_count);
    arguments[0] = (char *)"tater-vdec";
#ifdef TATER_CRONOS_ABI
    arguments[1] = request->output_rgba8888 ?
        (char *)"--decode-avc-linear-rgba-stream" :
        (char *)"--decode-avc-stream";
#else
    arguments[1] = request->output_rgba8888 ?
        (char *)"--decode-avc-native-rgba-stream" :
        (char *)"--decode-avc-native-stream";
#endif
    arguments[2] = (char *)request->component_library;
    arguments[3] = (char *)request->input_annex_b;
    arguments[4] = width;
    arguments[5] = height;
    arguments[6] = frames;
    arguments[7] = (char *)request->output_path;
    arguments[8] = NULL;
    return tater_omx_probe_main(8, arguments);
}

static int stream_rgba8888(const char *component_library,
                           uint32_t width_value,
                           uint32_t height_value,
                           uint32_t fps_value, int input_fd,
                           int output_fd, uint32_t stream_version) {
    char width[16];
    char height[16];
    char fps[16];
    char input[16];
    char output[16];
    char *arguments[9];

    if (!valid_path(component_library) ||
        width_value < TATER_VDEC_MIN_DIMENSION ||
        width_value > TATER_VDEC_MAX_WIDTH ||
        height_value < TATER_VDEC_MIN_DIMENSION ||
        height_value > TATER_VDEC_MAX_HEIGHT ||
        (width_value & 1U) != 0 || (height_value & 1U) != 0 ||
        fps_value == 0 || fps_value > 120U || input_fd < 0 ||
        output_fd < 0 || input_fd == output_fd)
        return 2;
    if (stream_version != 1U && stream_version != 2U)
        return 2;

    (void)snprintf(width, sizeof(width), "%u", width_value);
    (void)snprintf(height, sizeof(height), "%u", height_value);
    (void)snprintf(fps, sizeof(fps), "%u", fps_value);
    (void)snprintf(input, sizeof(input), "%d", input_fd);
    (void)snprintf(output, sizeof(output), "%d", output_fd);
    arguments[0] = (char *)"tater-vdec";
#ifdef TATER_CRONOS_ABI
    arguments[1] = stream_version == 2U ?
        (char *)"--serve-avc-linear-rgba-stream-v2" :
        (char *)"--serve-avc-linear-rgba-stream";
#else
    arguments[1] = stream_version == 2U ?
        (char *)"--serve-avc-native-rgba-stream-v2" :
        (char *)"--serve-avc-native-rgba-stream";
#endif
    arguments[2] = (char *)component_library;
    arguments[3] = width;
    arguments[4] = height;
    arguments[5] = fps;
    arguments[6] = input;
    arguments[7] = output;
    arguments[8] = NULL;
    return tater_omx_probe_main(8, arguments);
}

int tater_vdec_stream_rgba8888(const char *component_library,
                               uint32_t width_value,
                               uint32_t height_value,
                               uint32_t fps_value, int input_fd,
                               int output_fd) {
    return stream_rgba8888(component_library, width_value, height_value,
                           fps_value, input_fd, output_fd, 1U);
}

int tater_vdec_stream_rgba8888_v2(const char *component_library,
                                  uint32_t width_value,
                                  uint32_t height_value,
                                  uint32_t fps_value, int input_fd,
                                  int output_fd) {
    return stream_rgba8888(component_library, width_value, height_value,
                           fps_value, input_fd, output_fd, 2U);
}

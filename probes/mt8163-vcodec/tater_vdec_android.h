// SPDX-License-Identifier: MIT

#ifndef TATER_VDEC_ANDROID_H
#define TATER_VDEC_ANDROID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TATER_VDEC_MIN_DIMENSION = 16,
    TATER_VDEC_MAX_WIDTH = 1920,
    TATER_VDEC_MAX_HEIGHT = 1088,
    TATER_VDEC_MIN_BATCH_FRAMES = 2,
    TATER_VDEC_MAX_BATCH_FRAMES = 120
};

struct tater_vdec_batch {
    const char *component_library;
    const char *input_annex_b;
    const char *output_path;
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    int output_rgba8888;
};

/*
 * Decodes one finite, AUD-framed Annex-B H.264 batch through the stock MT8163
 * OMX native-buffer path. Output pictures are tightly packed YUV420P in OMX
 * presentation order. Set output_rgba8888 for tightly packed RGBA8888
 * instead. The caller owns both ordinary file paths.
 *
 * This is the first reusable process boundary, intentionally retaining the
 * probe's small limits while streaming and dynamic-size handling are built.
 */
int tater_vdec_decode_batch(const struct tater_vdec_batch *request);

/* Reads little-endian length-prefixed Annex-B access units from input_fd and
 * writes fixed-size RGBA8888 pictures to output_fd until clean EOS. */
int tater_vdec_stream_rgba8888(const char *component_library,
                               uint32_t width, uint32_t height,
                               uint32_t fps, int input_fd, int output_fd);

/* Protocol v2 adds a timestamp to every input access unit and writes a
 * negotiated stream header plus a timestamped header before every RGBA frame.
 * See README.md for the exact little-endian wire format. */
int tater_vdec_stream_rgba8888_v2(const char *component_library,
                                  uint32_t width, uint32_t height,
                                  uint32_t fps, int input_fd, int output_fd);

#ifdef __cplusplus
}
#endif

#endif

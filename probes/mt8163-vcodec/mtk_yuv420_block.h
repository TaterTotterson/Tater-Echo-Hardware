// SPDX-License-Identifier: MIT
#ifndef TATER_MTK_YUV420_BLOCK_H
#define TATER_MTK_YUV420_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert the MT8163 decoder's YUV420 block layout to RGBA8888.
 *
 * Luma is stored as raster-ordered 16x32 tiles. Chroma follows the complete
 * luma plane and is interleaved UV in raster-ordered 16x16 tiles. Buffer width
 * must be a multiple of 16 and buffer height a multiple of 32.
 */
int tater_mtk_yuv420_block_to_rgba8888(
    const uint8_t *source,
    size_t source_size,
    unsigned int buffer_width,
    unsigned int buffer_height,
    unsigned int visible_width,
    unsigned int visible_height,
    uint8_t *destination,
    size_t destination_stride,
    size_t destination_size);

/* Detile the same hardware layout into tightly packed planar YUV420P. */
int tater_mtk_yuv420_block_to_yuv420p(
    const uint8_t *source,
    size_t source_size,
    unsigned int buffer_width,
    unsigned int buffer_height,
    unsigned int visible_width,
    unsigned int visible_height,
    uint8_t *destination,
    size_t destination_size);

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: MIT
#include "mtk_yuv420_block.h"

#include <errno.h>
#include <stdint.h>

#if defined(__ARM_NEON) && !defined(TATER_MTK_YUV_FORCE_SCALAR)
#include <arm_neon.h>

static uint8x8_t rgb_channel(int16x8_t c, int16x8_t d, int16x8_t e,
                           int u_coefficient, int v_coefficient)
{
    int32x4_t lo = vmull_n_s16(vget_low_s16(c), 298);
    int32x4_t hi = vmull_n_s16(vget_high_s16(c), 298);
    lo = vmlal_n_s16(lo, vget_low_s16(d), (int16_t)u_coefficient);
    hi = vmlal_n_s16(hi, vget_high_s16(d), (int16_t)u_coefficient);
    lo = vmlal_n_s16(lo, vget_low_s16(e), (int16_t)v_coefficient);
    hi = vmlal_n_s16(hi, vget_high_s16(e), (int16_t)v_coefficient);
    // Rounding shift implements the scalar +128 then arithmetic >>8 exactly.
    return vqmovn_u16(vcombine_u16(vqrshrun_n_s32(lo, 8), vqrshrun_n_s32(hi, 8)));
}

static void rgba_eight(uint8x8_t luma, uint8x8_t u, uint8x8_t v, uint8_t *out)
{
    int16x8_t c = vmaxq_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(luma)), vdupq_n_s16(16)), vdupq_n_s16(0));
    int16x8_t d = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u)), vdupq_n_s16(128));
    int16x8_t e = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v)), vdupq_n_s16(128));
    uint8x8x4_t rgba;
    rgba.val[0] = rgb_channel(c, d, e, 0, 409);
    rgba.val[1] = rgb_channel(c, d, e, -100, -208);
    rgba.val[2] = rgb_channel(c, d, e, 516, 0);
    rgba.val[3] = vdup_n_u8(255);
    vst4_u8(out, rgba);
}
#endif

static size_t tile_offset(unsigned int x, unsigned int y,
                          unsigned int width, unsigned int tile_width,
                          unsigned int tile_height)
{
    size_t tiles_per_row = width / tile_width;
    size_t tile_index = (size_t)(y / tile_height) * tiles_per_row +
                        x / tile_width;

    return tile_index * tile_width * tile_height +
           (size_t)(y % tile_height) * tile_width + x % tile_width;
}

static uint8_t clamp_byte(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (uint8_t)value;
}

int tater_mtk_yuv420_block_to_rgba8888(
    const uint8_t *source,
    size_t source_size,
    unsigned int buffer_width,
    unsigned int buffer_height,
    unsigned int visible_width,
    unsigned int visible_height,
    uint8_t *destination,
    size_t destination_stride,
    size_t destination_size)
{
    size_t luma_size;
    size_t required_source;
    size_t required_destination;
    unsigned int x;
    unsigned int y;

    if (source == NULL || destination == NULL || buffer_width == 0 ||
        buffer_height == 0 || buffer_width % 16 != 0 ||
        buffer_height % 32 != 0 || visible_width == 0 ||
        visible_height == 0 || visible_width > buffer_width ||
        visible_height > buffer_height ||
        destination_stride < (size_t)visible_width * 4)
        return -EINVAL;
    if ((size_t)buffer_width > SIZE_MAX / buffer_height)
        return -EOVERFLOW;
    luma_size = (size_t)buffer_width * buffer_height;
    if (luma_size > SIZE_MAX - luma_size / 2)
        return -EOVERFLOW;
    required_source = luma_size + luma_size / 2;
    if (source_size < required_source)
        return -ENOSPC;
    if ((size_t)visible_height > SIZE_MAX / destination_stride)
        return -EOVERFLOW;
    required_destination = (size_t)visible_height * destination_stride;
    if (destination_size < required_destination)
        return -ENOSPC;

    for (y = 0; y < visible_height; ++y) {
        uint8_t *row = destination + (size_t)y * destination_stride;

        x = 0;
#if defined(__ARM_NEON) && !defined(TATER_MTK_YUV_FORCE_SCALAR)
        // One 16-pixel row segment lies wholly inside each public MTK tile.
        // Share chroma for pixel pairs and avoid scalar detile/convert work
        // per pixel; clipped tails retain the exact scalar reference path.
        for (; x + 16 <= visible_width; x += 16) {
            const uint8_t *yp = source + tile_offset(x, y, buffer_width, 16, 32);
            const uint8_t *uvp = source + luma_size + tile_offset(x, y / 2, buffer_width, 16, 16);
            uint8x16_t ys = vld1q_u8(yp);
            uint8x8x2_t uv = vld2_u8(uvp);
            uint8x8x2_t us = vzip_u8(uv.val[0], uv.val[0]);
            uint8x8x2_t vs = vzip_u8(uv.val[1], uv.val[1]);
            rgba_eight(vget_low_u8(ys), us.val[0], vs.val[0], row + x * 4);
            rgba_eight(vget_high_u8(ys), us.val[1], vs.val[1], row + x * 4 + 32);
        }
#endif

        for (; x < visible_width; ++x) {
            size_t y_offset = tile_offset(x, y, buffer_width, 16, 32);
            size_t uv_offset = tile_offset(x & ~1U, y / 2,
                                           buffer_width, 16, 16);
            int luma = source[y_offset];
            int u = source[luma_size + uv_offset];
            int v = source[luma_size + uv_offset + 1];
            int c = luma - 16;
            int d = u - 128;
            int e = v - 128;

            if (c < 0)
                c = 0;
            row[x * 4] = clamp_byte((298 * c + 409 * e + 128) >> 8);
            row[x * 4 + 1] =
                clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8);
            row[x * 4 + 2] = clamp_byte((298 * c + 516 * d + 128) >> 8);
            row[x * 4 + 3] = 255;
        }
    }
    return 0;
}

int tater_mtk_yuv420_block_to_yuv420p(
    const uint8_t *source,
    size_t source_size,
    unsigned int buffer_width,
    unsigned int buffer_height,
    unsigned int visible_width,
    unsigned int visible_height,
    uint8_t *destination,
    size_t destination_size)
{
    size_t buffer_luma_size;
    size_t required_source;
    size_t visible_luma_size;
    size_t visible_chroma_size;
    size_t required_destination;
    unsigned int x;
    unsigned int y;

    if (source == NULL || destination == NULL || buffer_width == 0 ||
        buffer_height == 0 || buffer_width % 16 != 0 ||
        buffer_height % 32 != 0 || visible_width == 0 ||
        visible_height == 0 || visible_width > buffer_width ||
        visible_height > buffer_height || visible_width % 2 != 0 ||
        visible_height % 2 != 0)
        return -EINVAL;
    if ((size_t)buffer_width > SIZE_MAX / buffer_height ||
        (size_t)visible_width > SIZE_MAX / visible_height)
        return -EOVERFLOW;
    buffer_luma_size = (size_t)buffer_width * buffer_height;
    visible_luma_size = (size_t)visible_width * visible_height;
    if (buffer_luma_size > SIZE_MAX - buffer_luma_size / 2 ||
        visible_luma_size > SIZE_MAX - visible_luma_size / 2)
        return -EOVERFLOW;
    required_source = buffer_luma_size + buffer_luma_size / 2;
    visible_chroma_size = visible_luma_size / 4;
    required_destination = visible_luma_size + visible_luma_size / 2;
    if (source_size < required_source ||
        destination_size < required_destination)
        return -ENOSPC;

    for (y = 0; y < visible_height; ++y) {
        for (x = 0; x < visible_width; ++x) {
            destination[(size_t)y * visible_width + x] =
                source[tile_offset(x, y, buffer_width, 16, 32)];
        }
    }
    for (y = 0; y < visible_height / 2; ++y) {
        for (x = 0; x < visible_width; x += 2) {
            size_t source_offset = buffer_luma_size +
                tile_offset(x, y, buffer_width, 16, 16);
            size_t destination_offset =
                (size_t)y * (visible_width / 2) + x / 2;

            destination[visible_luma_size + destination_offset] =
                source[source_offset];
            destination[visible_luma_size + visible_chroma_size +
                        destination_offset] = source[source_offset + 1];
        }
    }
    return 0;
}

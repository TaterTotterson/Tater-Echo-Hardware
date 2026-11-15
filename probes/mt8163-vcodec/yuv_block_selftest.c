// SPDX-License-Identifier: MIT
#include "mtk_yuv420_block.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 32U
#define HEIGHT 32U
#define PADDED_HEIGHT 64U
#define CROPPED_HEIGHT 34U

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

int main(void)
{
    uint8_t source[WIDTH * HEIGHT * 3 / 2];
    uint8_t rgba[WIDTH * HEIGHT * 4];
    uint8_t yuv420p[WIDTH * HEIGHT * 3 / 2];
    uint8_t padded[WIDTH * PADDED_HEIGHT * 3 / 2];
    uint8_t cropped[WIDTH * CROPPED_HEIGHT * 3 / 2];
    size_t luma_size = WIDTH * HEIGHT;
    unsigned int x;
    unsigned int y;

    memset(source, 0, sizeof(source));
    memset(rgba, 0, sizeof(rgba));
    memset(yuv420p, 0, sizeof(yuv420p));
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            size_t offset = tile_offset(x, y, WIDTH, 16, 32);
            if (y < 16)
                source[offset] = x < 16 ? 16 : 235;
            else
                source[offset] = x < 16 ? 81 : 41;
        }
    }
    for (y = 0; y < HEIGHT / 2; ++y) {
        for (x = 0; x < WIDTH; x += 2) {
            size_t offset = tile_offset(x, y, WIDTH, 16, 16);
            if (y < 8) {
                source[luma_size + offset] = 128;
                source[luma_size + offset + 1] = 128;
            } else if (x < 16) {
                source[luma_size + offset] = 90;
                source[luma_size + offset + 1] = 240;
            } else {
                source[luma_size + offset] = 240;
                source[luma_size + offset + 1] = 110;
            }
        }
    }
    if (tater_mtk_yuv420_block_to_rgba8888(
            source, sizeof(source), WIDTH, HEIGHT, WIDTH, HEIGHT, rgba,
            WIDTH * 4, sizeof(rgba)) != 0)
        return 1;
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            const uint8_t *pixel = rgba + ((size_t)y * WIDTH + x) * 4;
            uint8_t red;
            uint8_t green;
            uint8_t blue;

            if (y < 16) {
                red = green = blue = x < 16 ? 0 : 255;
            } else if (x < 16) {
                red = 255;
                green = 0;
                blue = 0;
            } else {
                red = 0;
                green = 0;
                blue = 255;
            }
            if (pixel[0] != red || pixel[1] != green ||
                pixel[2] != blue || pixel[3] != 255)
                return 2;
        }
    }
    if (tater_mtk_yuv420_block_to_rgba8888(
            source, sizeof(source) - 1, WIDTH, HEIGHT, WIDTH, HEIGHT, rgba,
            WIDTH * 4, sizeof(rgba)) == 0)
        return 3;
    if (tater_mtk_yuv420_block_to_yuv420p(
            source, sizeof(source), WIDTH, HEIGHT, WIDTH, HEIGHT,
            yuv420p, sizeof(yuv420p)) != 0)
        return 4;
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            uint8_t expected;

            if (y < 16)
                expected = x < 16 ? 16 : 235;
            else
                expected = x < 16 ? 81 : 41;
            if (yuv420p[(size_t)y * WIDTH + x] != expected)
                return 5;
        }
    }
    for (y = 0; y < HEIGHT / 2; ++y) {
        for (x = 0; x < WIDTH / 2; ++x) {
            uint8_t expected_u;
            uint8_t expected_v;
            size_t offset = (size_t)y * (WIDTH / 2) + x;

            if (y < 8) {
                expected_u = 128;
                expected_v = 128;
            } else if (x < 8) {
                expected_u = 90;
                expected_v = 240;
            } else {
                expected_u = 240;
                expected_v = 110;
            }
            if (yuv420p[luma_size + offset] != expected_u ||
                yuv420p[luma_size + luma_size / 4 + offset] != expected_v)
                return 6;
        }
    }
    if (tater_mtk_yuv420_block_to_yuv420p(
            source, sizeof(source) - 1, WIDTH, HEIGHT, WIDTH, HEIGHT,
            yuv420p, sizeof(yuv420p)) == 0)
        return 7;

    /* A 720- or 1080-line picture occupies a 736- or 1088-line hardware
     * surface. Verify that the converter accepts that padding and returns
     * exactly the visible rows rather than leaking the padded tail. */
    memset(padded, 0xee, sizeof(padded));
    memset(cropped, 0, sizeof(cropped));
    for (y = 0; y < PADDED_HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            padded[tile_offset(x, y, WIDTH, 16, 32)] =
                y < CROPPED_HEIGHT ? (uint8_t)(16U + y) : 0xdd;
        }
    }
    for (y = 0; y < PADDED_HEIGHT / 2; ++y) {
        for (x = 0; x < WIDTH; x += 2) {
            size_t offset = WIDTH * PADDED_HEIGHT +
                tile_offset(x, y, WIDTH, 16, 16);

            padded[offset] = y < CROPPED_HEIGHT / 2 ? 100 : 0xdd;
            padded[offset + 1] =
                y < CROPPED_HEIGHT / 2 ? 150 : 0xdd;
        }
    }
    if (tater_mtk_yuv420_block_to_yuv420p(
            padded, sizeof(padded), WIDTH, PADDED_HEIGHT, WIDTH,
            CROPPED_HEIGHT, cropped, sizeof(cropped)) != 0)
        return 8;
    for (y = 0; y < CROPPED_HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            if (cropped[(size_t)y * WIDTH + x] != (uint8_t)(16U + y))
                return 9;
        }
    }
    luma_size = WIDTH * CROPPED_HEIGHT;
    for (x = 0; x < luma_size / 4; ++x) {
        if (cropped[luma_size + x] != 100 ||
            cropped[luma_size + luma_size / 4 + x] != 150)
            return 10;
    }
    puts("MTK YUV420 block conversion self-test: ok");
    return 0;
}

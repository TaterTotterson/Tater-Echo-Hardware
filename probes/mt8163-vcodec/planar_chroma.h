/* SPDX-License-Identifier: MIT
 * Original adapter for the qualified OMX encoder's observed Y,V,U input.
 * Keep Bayer conversion and synthetic fixtures in standard I420 (Y,U,V).
 */
#ifndef TATER_PLANAR_CHROMA_H
#define TATER_PLANAR_CHROMA_H
#include <stddef.h>
#include <stdint.h>

static inline int tater_i420_to_yv12(uint8_t *buffer, unsigned stride,
                                    unsigned slice, size_t capacity) {
    if (!buffer || !stride || !slice || stride % 2 || slice % 2 ||
        (size_t)stride > SIZE_MAX / slice) return -1;
    size_t pixels = (size_t)stride * slice;
    if (pixels > capacity || pixels / 2 > capacity - pixels) return -1;
    uint8_t *u = buffer + pixels, *v = u + pixels / 4;
    for (size_t index = 0; index < pixels / 4; ++index) {
        uint8_t value = u[index];
        u[index] = v[index];
        v[index] = value;
    }
    return 0;
}
#endif

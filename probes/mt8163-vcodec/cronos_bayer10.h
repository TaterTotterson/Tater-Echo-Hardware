// SPDX-License-Identifier: MIT
#ifndef TATER_CRONOS_BAYER10_H
#define TATER_CRONOS_BAYER10_H
#include <stddef.h>
#include <stdint.h>
/* OV02B10: full 1600x1200 red-first packed10 sensor input -> 1280x960 I420.
 * Preserve the complete 4:3 field of view. Three 2048-byte tone LUTs are R/G/B. */
int cronos_bayer10_yuv420(const uint8_t *raw, size_t raw_bytes,
                         const uint8_t *luts, uint16_t *scratch,
                         uint8_t *output, size_t output_bytes);
int cronos_bayer10_balanced_yuv420(const uint8_t *raw, size_t raw_bytes,
                                  const uint8_t *luts, uint16_t *scratch,
                                  uint8_t *output, size_t output_bytes);
#endif

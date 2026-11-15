#ifndef TATER_BAYER10_H
#define TATER_BAYER10_H
#include <stddef.h>
#include <stdint.h>
/* Checkers BGGR, public packed little-endian 10-bit samples, three 2048-byte
 * tone tables in R/G/B order. Produces limited-range planar YUV420. */
int tater_bayer10_yuv420(const uint8_t *raw, size_t raw_bytes,
                        const uint8_t *luts, unsigned width, unsigned height,
                        uint16_t *scratch, uint8_t *output, size_t output_bytes);
int tater_bayer10_balanced_yuv420(const uint8_t *raw, size_t raw_bytes,
                                 const uint8_t *luts, uint16_t *scratch,
                                 uint8_t *output, size_t output_bytes);
#endif

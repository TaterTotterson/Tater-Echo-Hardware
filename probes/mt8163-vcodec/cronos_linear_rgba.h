// SPDX-License-Identifier: MIT
#ifndef TATER_CRONOS_LINEAR_RGBA_H
#define TATER_CRONOS_LINEAR_RGBA_H

#include <stddef.h>
#include <stdint.h>

#if defined(__ARM_NEON) && !defined(TATER_CRONOS_FORCE_SCALAR)
#include <arm_neon.h>
static uint8x8_t cronos_rgb_channel(int16x8_t c, int16x8_t d, int16x8_t e,
                                   int16_t u, int16_t v) {
    int32x4_t lo=vmull_n_s16(vget_low_s16(c),298);
    int32x4_t hi=vmull_n_s16(vget_high_s16(c),298);
    lo=vmlal_n_s16(lo,vget_low_s16(d),u);hi=vmlal_n_s16(hi,vget_high_s16(d),u);
    lo=vmlal_n_s16(lo,vget_low_s16(e),v);hi=vmlal_n_s16(hi,vget_high_s16(e),v);
    return vqmovn_u16(vcombine_u16(vqrshrun_n_s32(lo,8),vqrshrun_n_s32(hi,8)));
}

static void cronos_rgba_eight(uint8x8_t y,uint8x8_t u,uint8x8_t v,uint8_t *out) {
    int16x8_t c=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(y)),vdupq_n_s16(16));
    int16x8_t d=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u)),vdupq_n_s16(128));
    int16x8_t e=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v)),vdupq_n_s16(128));
    uint8x8x4_t rgba;
    rgba.val[0]=cronos_rgb_channel(c,d,e,0,409);
    rgba.val[1]=cronos_rgb_channel(c,d,e,-100,-208);
    rgba.val[2]=cronos_rgb_channel(c,d,e,516,0);
    rgba.val[3]=vdup_n_u8(255);
    vst4_u8(out,rgba);
}
#endif

static uint8_t cronos_clip_rgb(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value;
}

/* The qualified OMX contract is tightly packed, limited-range BT.601 I420.
 * This function touches only owned userspace memory, never a device. */
static int cronos_i420_rgba(const uint8_t *input, size_t input_bytes,
                            uint32_t width, uint32_t height,
                            uint8_t *output, size_t output_bytes) {
    size_t pixels = (size_t)width * height;
    uint32_t y, x;
    if (!input || !output || width < 2 || height < 2 ||
        width > 1920 || height > 1088 || (width & 1) || (height & 1) ||
        input_bytes < pixels * 3 / 2 || output_bytes < pixels * 4)
        return -1;
    for (y = 0; y < height; ++y) {
        x=0;
#if defined(__ARM_NEON) && !defined(TATER_CRONOS_FORCE_SCALAR)
        for (; x+16<=width; x+=16) {
            size_t p=(size_t)y*width+x;
            size_t k=(size_t)(y/2)*(width/2)+x/2;
            uint8x16_t ys=vld1q_u8(input+p);
            uint8x8_t u=vld1_u8(input+pixels+k);
            uint8x8_t v=vld1_u8(input+pixels+pixels/4+k);
            uint8x8x2_t us=vzip_u8(u,u),vs=vzip_u8(v,v);
            cronos_rgba_eight(vget_low_u8(ys),us.val[0],vs.val[0],output+p*4);
            cronos_rgba_eight(vget_high_u8(ys),us.val[1],vs.val[1],output+p*4+32);
        }
#endif
        for (; x < width; ++x) {
            size_t p = (size_t)y * width + x;
            size_t chroma = (size_t)(y / 2) * (width / 2) + x / 2;
            int c = (int)input[p] - 16;
            int d = (int)input[pixels + chroma] - 128;
            int e = (int)input[pixels + pixels / 4 + chroma] - 128;
            output[p * 4] = cronos_clip_rgb((298*c + 409*e + 128) >> 8);
            output[p * 4 + 1] = cronos_clip_rgb((298*c - 100*d - 208*e + 128) >> 8);
            output[p * 4 + 2] = cronos_clip_rgb((298*c + 516*d + 128) >> 8);
            output[p * 4 + 3] = 255;
        }
    }
    return 0;
}
#endif

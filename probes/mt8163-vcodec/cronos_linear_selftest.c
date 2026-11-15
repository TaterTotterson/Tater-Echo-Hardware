// SPDX-License-Identifier: MIT
// Owned-memory differential test. Never opens a hardware node.
#define TATER_CRONOS_FORCE_SCALAR
#define cronos_i420_rgba scalar_convert
#define cronos_clip_rgb scalar_clip
#include "cronos_linear_rgba.h"
#undef TATER_CRONOS_LINEAR_RGBA_H
#undef TATER_CRONOS_FORCE_SCALAR
#undef cronos_i420_rgba
#undef cronos_clip_rgb
#include "cronos_linear_rgba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const unsigned sizes[][2]={{2,2},{14,6},{18,18},{128,128},{1280,720},{1280,960},{1920,1088}};
    uint32_t random=5123;size_t checked=0;unsigned tests=0;
    for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);++n) {
        unsigned w=sizes[n][0],h=sizes[n][1];size_t pixels=(size_t)w*h;
        uint8_t *input=malloc(pixels*3/2),*reference=malloc(pixels*4),*result=malloc(pixels*4);
        if(!input||!reference||!result)return 1;
        for(unsigned pattern=0;pattern<3;++pattern) {
            for(size_t k=0;k<pixels*3/2;++k) {
                random=random*1664525+1013904223;
                input[k]=pattern==0?0:pattern==1?255:(uint8_t)(random>>24);
            }
            if(scalar_convert(input,pixels*3/2,w,h,reference,pixels*4)||
               cronos_i420_rgba(input,pixels*3/2,w,h,result,pixels*4)||
               memcmp(reference,result,pixels*4))return 1;
            checked+=pixels*4;++tests;
        }
        free(input);free(reference);free(result);
    }
    printf("{\"converter_cases\":%u,\"matching_bytes\":%zu,\"neon\":%s,\"hardware_opened\":false}\n",tests,checked,
#ifdef __ARM_NEON
           "true"
#else
           "false"
#endif
    );
    return 0;
}

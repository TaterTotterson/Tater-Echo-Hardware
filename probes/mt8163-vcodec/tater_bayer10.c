#include "tater_bayer10.h"
#include <pthread.h>
#include "bayer10_scaled.h"

int tater_bayer10_balanced_yuv420(const uint8_t *raw,size_t bytes,const uint8_t *luts,
                                 uint16_t *scratch,uint8_t *out,size_t out_bytes) {
    return scaled_bayer10_yuv420(raw,bytes,luts,1280,720,768,432,0,scratch,out,out_bytes);
}

struct band {
    const uint16_t *scratch;
    const uint8_t *luts;
    uint8_t *out;
    unsigned width, height, y0, y1;
};

static inline void pixel(uint8_t *out, const uint8_t *luts,
                          unsigned r, unsigned g, unsigned b, int sums[3]) {
    int red = luts[r*2], green = luts[2048+g*2], blue = luts[4096+b*2];
    *out = 16 + ((66*red+129*green+25*blue+128)>>8);
    sums[0] += red; sums[1] += green; sums[2] += blue;
}

static void *develop_band(void *context) {
    const struct band *band = context;
    unsigned width = band->width, height = band->height;
    const uint16_t *scratch = band->scratch;
    const uint8_t *luts = band->luts;
    uint8_t *out = band->out;
    size_t pixels = (size_t)width * height;
    for (unsigned y = band->y0; y < band->y1; y += 2) {
        unsigned ym = y ? y-1 : 1;
        unsigned yp = y+2 < height ? y+2 : height-2;
        const uint16_t *r0 = scratch + (size_t)ym*width;
        const uint16_t *r1 = scratch + (size_t)y*width;
        const uint16_t *r2 = scratch + (size_t)(y+1)*width;
        const uint16_t *r3 = scratch + (size_t)yp*width;
        uint8_t *top = out + (size_t)y*width;
        uint8_t *bottom = top + width;
        for (unsigned x = 0; x < width; x += 2) {
            unsigned xm = x ? x-1 : 1;
            unsigned xp = x+2 < width ? x+2 : width-2;
            int sum[3] = {0};
            pixel(top+x, luts, (r0[xm]+r0[x+1]+r2[xm]+r2[x+1])/4,
                  (r1[xm]+r1[x+1]+r0[x]+r2[x])/4, r1[x], sum);
            pixel(top+x+1, luts, (r0[x+1]+r2[x+1])/2, r1[x+1],
                  (r1[x]+r1[xp])/2, sum);
            pixel(bottom+x, luts, (r2[xm]+r2[x+1])/2, r2[x],
                  (r1[x]+r3[x])/2, sum);
            pixel(bottom+x+1, luts, r2[x+1],
                  (r2[x]+r2[xp]+r1[x+1]+r3[x+1])/4,
                  (r1[x]+r1[xp]+r3[x]+r3[xp])/4, sum);
            size_t chroma = (size_t)(y/2)*(width/2)+x/2;
            out[pixels+chroma] = 128 + ((-38*sum[0]-74*sum[1]+112*sum[2]+512)>>10);
            out[pixels+pixels/4+chroma] = 128 + ((112*sum[0]-94*sum[1]-18*sum[2]+512)>>10);
        }
    }
    return NULL;
}

int tater_bayer10_yuv420(const uint8_t *raw, size_t raw_bytes,
                        const uint8_t *luts, unsigned width, unsigned height,
                        uint16_t *scratch, uint8_t *out, size_t out_bytes) {
    if (!raw || !luts || !scratch || !out || width < 2 || height < 2 ||
        width > 1280 || height > 720 || width % 4 || height % 2 ||
        raw_bytes != (size_t)width * height * 5 / 4 ||
        out_bytes < (size_t)width * height * 3 / 2) return -1;
    for (size_t j = 0, k = 0; j < raw_bytes; j += 5, k += 4) {
        scratch[k] = raw[j] | (raw[j+1] & 3U) << 8;
        scratch[k+1] = raw[j+1] >> 2 | (raw[j+2] & 15U) << 6;
        scratch[k+2] = raw[j+2] >> 4 | (raw[j+3] & 63U) << 4;
        scratch[k+3] = raw[j+3] >> 6 | (unsigned)raw[j+4] << 2;
    }
    struct band bands[4];
    pthread_t threads[3];
    unsigned launched = 0;
    for (unsigned n = 0; n < 4; ++n) {
        bands[n] = (struct band){scratch, luts, out, width, height,
            2*((height/2)*n/4), 2*((height/2)*(n+1)/4)};
        if (n < 3) {
            if (pthread_create(&threads[n], NULL, develop_band, &bands[n])) {
                for (unsigned j = 0; j < launched; ++j) pthread_join(threads[j], NULL);
                return -1;
            }
            ++launched;
        } else develop_band(&bands[n]);
    }
    for (unsigned n = 0; n < launched; ++n) pthread_join(threads[n], NULL);
    return 0;
}

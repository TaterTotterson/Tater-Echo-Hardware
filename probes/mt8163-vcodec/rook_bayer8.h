/* SPDX-License-Identifier: MIT
 * Original bounded RGGB8 developer for Rook's existing immutable camera frame.
 * Color/levels LUTs are supplied by the existing owner-side camera developer.
 * This does not open or configure the sensor or ISP.
 */
#ifndef TATER_ROOK_BAYER8_H
#define TATER_ROOK_BAYER8_H
#include <stddef.h>
#include <stdint.h>

static int rook_byte(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }
static void rook_rgb(const uint8_t *raw, const uint8_t *tables, unsigned x, unsigned y, int rgb[3]) {
    unsigned l = x ? x-1 : 1, r = x < 639 ? x+1 : 638;
    unsigned u = y ? y-1 : 1, d = y < 479 ? y+1 : 478;
    unsigned center = raw[y*640+x]*4, cross = raw[u*640+x]+raw[d*640+x]+raw[y*640+l]+raw[y*640+r];
    unsigned diagonal = raw[u*640+l]+raw[u*640+r]+raw[d*640+l]+raw[d*640+r];
    unsigned red, green, blue;
    if (!(x&1) && !(y&1)) { red=center; green=cross; blue=diagonal; }
    else if ((x&1) && (y&1)) { red=diagonal; green=cross; blue=center; }
    else {
        green=center;
        red = !(y&1) ? 2*(raw[y*640+l]+raw[y*640+r]) : 2*(raw[u*640+x]+raw[d*640+x]);
        blue = !(y&1) ? 2*(raw[u*640+x]+raw[d*640+x]) : 2*(raw[y*640+l]+raw[y*640+r]);
    }
    rgb[0]=tables[red]; rgb[1]=tables[1024+green]; rgb[2]=tables[2048+blue];
}

static int rook_bayer8_yuv420(const uint8_t *record, size_t record_size,
                             unsigned width, unsigned height, uint8_t *out, size_t capacity) {
    if (!record || !out || record_size != 3072+640*480 ||
        !((width==640 && height==480) || (width==320 && height==240)) || capacity < (size_t)width*height*3/2) return -1;
    const uint8_t *raw = record+3072;
    uint8_t *u = out+(size_t)width*height, *v=u+(size_t)width*height/4;
    unsigned step=640/width;
    for (unsigned y=0; y<height; y+=2) {
        for (unsigned x=0; x<width; x+=2) {
            int sums[3]={0};
            for (unsigned dy=0; dy<2; ++dy) {
                for (unsigned dx=0; dx<2; ++dx) {
                    int rgb[3];
                    rook_rgb(raw, record, (x+dx)*step, (y+dy)*step, rgb);
                    out[(size_t)(y+dy)*width+x+dx]=(uint8_t)rook_byte(16+((66*rgb[0]+129*rgb[1]+25*rgb[2]+128)>>8));
                    for (unsigned c=0;c<3;++c) sums[c]+=rgb[c];
                }
            }
            int red=(sums[0]+2)/4, green=(sums[1]+2)/4, blue=(sums[2]+2)/4;
            size_t index=(size_t)(y/2)*(width/2)+x/2;
            u[index]=(uint8_t)rook_byte(128+((-38*red-74*green+112*blue+128)>>8));
            v[index]=(uint8_t)rook_byte(128+((112*red-94*green-18*blue+128)>>8));
        }
    }
    return 0;
}
#endif

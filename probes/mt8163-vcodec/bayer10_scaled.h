/* SPDX-License-Identifier: MIT
 * Original bounded Bayer sampling/conversion for screen-sized live video.
 * Sample the full field before YUV conversion, not a full-resolution RGB image.
 */
#ifndef TATER_BAYER10_SCALED_H
#define TATER_BAYER10_SCALED_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

struct scaled_bayer_band {
    const uint16_t *raw;
    const uint8_t *luts;
    uint8_t *out;
    unsigned sw, sh, dw, dh, red_first, y0, y1;
};

static void scaled_bayer_rgb(const struct scaled_bayer_band *b,
                             unsigned dx, unsigned dy, unsigned v[3]) {
    unsigned x=dx*b->sw/b->dw, y=dy*b->sh/b->dh;
    unsigned xm=x?x-1:1, xp=x+1<b->sw?x+1:b->sw-2;
    unsigned ym=y?y-1:1, yp=y+1<b->sh?y+1:b->sh-2;
    const uint16_t *top=b->raw+(size_t)ym*b->sw;
    const uint16_t *row=b->raw+(size_t)y*b->sw;
    const uint16_t *bottom=b->raw+(size_t)yp*b->sw;
    unsigned r,g,blue;
    if (((x^y)&1)==0) {
        unsigned diagonal=(top[xm]+top[xp]+bottom[xm]+bottom[xp])/4;
        g=(row[xm]+row[xp]+top[x]+bottom[x])/4;
        if (!(y&1)) {r=row[x];blue=diagonal;}
        else {r=diagonal;blue=row[x];}
    } else {
        unsigned horizontal=(row[xm]+row[xp])/2;
        unsigned vertical=(top[x]+bottom[x])/2;
        g=row[x];
        if (!(y&1)) {r=horizontal;blue=vertical;}
        else {r=vertical;blue=horizontal;}
    }
    if (!b->red_first) {unsigned swap=r;r=blue;blue=swap;}
    v[0]=b->luts[2*r];v[1]=b->luts[2048+2*g];v[2]=b->luts[4096+2*blue];
}

static void *scaled_bayer_develop(void *context) {
    const struct scaled_bayer_band *b=context;
    size_t pixels=(size_t)b->dw*b->dh;
    for (unsigned y=b->y0;y<b->y1;y+=2) {
        for (unsigned x=0;x<b->dw;x+=2) {
            int sums[3]={0};
            for (unsigned j=0;j<2;++j) for (unsigned i=0;i<2;++i) {
                unsigned v[3];scaled_bayer_rgb(b,x+i,y+j,v);
                b->out[(size_t)(y+j)*b->dw+x+i]=16+((66*v[0]+129*v[1]+25*v[2]+128)>>8);
                for(unsigned k=0;k<3;++k)sums[k]+=(int)v[k];
            }
            size_t k=(size_t)(y/2)*(b->dw/2)+x/2;
            b->out[pixels+k]=128+((-38*sums[0]-74*sums[1]+112*sums[2]+512)>>10);
            b->out[pixels*5/4+k]=128+((112*sums[0]-94*sums[1]-18*sums[2]+512)>>10);
        }
    }
    return NULL;
}

static int scaled_bayer10_yuv420(const uint8_t *raw,size_t bytes,const uint8_t *luts,
                                 unsigned sw,unsigned sh,unsigned dw,unsigned dh,
                                 unsigned red_first,uint16_t *scratch,
                                 uint8_t *out,size_t out_bytes) {
    if(!raw||!luts||!scratch||!out||sw<4||sh<2||sw>1600||sh>1200||sw%4||sh%2||
       dw<2||dh<2||dw>sw||dh>sh||dw%2||dh%2||red_first>1||
       bytes!=(size_t)sw*sh*5/4||out_bytes<(size_t)dw*dh*3/2)return -1;
    for(size_t j=0,k=0;j<bytes;j+=5,k+=4) {
        scratch[k]=raw[j]|(raw[j+1]&3U)<<8;
        scratch[k+1]=raw[j+1]>>2|(raw[j+2]&15U)<<6;
        scratch[k+2]=raw[j+2]>>4|(raw[j+3]&63U)<<4;
        scratch[k+3]=raw[j+3]>>6|(unsigned)raw[j+4]<<2;
    }
    struct scaled_bayer_band bands[4];pthread_t threads[3];unsigned launched=0;
    for(unsigned n=0;n<4;++n) {
        bands[n]=(struct scaled_bayer_band){scratch,luts,out,sw,sh,dw,dh,red_first,
            2*((dh/2)*n/4),2*((dh/2)*(n+1)/4)};
        if(n<3) {
            if(pthread_create(&threads[n],NULL,scaled_bayer_develop,&bands[n])) {
                for(unsigned j=0;j<launched;++j)pthread_join(threads[j],NULL);
                return -1;
            }
            ++launched;
        } else scaled_bayer_develop(&bands[n]);
    }
    for(unsigned j=0;j<launched;++j)pthread_join(threads[j],NULL);
    return 0;
}
#endif

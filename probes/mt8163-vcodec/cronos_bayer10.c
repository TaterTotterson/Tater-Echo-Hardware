// SPDX-License-Identifier: MIT
#include "cronos_bayer10.h"
#include <pthread.h>
#include "bayer10_scaled.h"

int cronos_bayer10_balanced_yuv420(const uint8_t *raw,size_t bytes,const uint8_t *luts,
                                  uint16_t *scratch,uint8_t *out,size_t out_bytes) {
    return scaled_bayer10_yuv420(raw,bytes,luts,1600,1200,640,480,1,scratch,out,out_bytes);
}

#define SW 1600U
#define SH 1200U
#define DW 1280U
#define DH 960U
struct cronos_band {
    const uint16_t *raw;
    const uint8_t *luts;
    uint8_t *output;
    unsigned y0, y1;
};

static void rgb(const struct cronos_band *band, unsigned dx, unsigned dy,
                 unsigned values[3]) {
    unsigned x=dx*SW/DW,y=dy*SH/DH;
    unsigned xm=x?x-1:1,xp=x+1<SW?x+1:SW-2;
    unsigned ym=y?y-1:1,yp=y+1<SH?y+1:SH-2;
    const uint16_t *top=band->raw+(size_t)ym*SW;
    const uint16_t *row=band->raw+(size_t)y*SW;
    const uint16_t *bottom=band->raw+(size_t)yp*SW;
    unsigned r,g,b;
    if (((x^y)&1)==0) {
        unsigned diagonal=(top[xm]+top[xp]+bottom[xm]+bottom[xp])/4;
        g=(row[xm]+row[xp]+top[x]+bottom[x])/4;
        if(!(y&1)){r=row[x];b=diagonal;}else{r=diagonal;b=row[x];}
    } else {
        unsigned horizontal=(row[xm]+row[xp])/2;
        unsigned vertical=(top[x]+bottom[x])/2;
        g=row[x];
        if(!(y&1)){r=horizontal;b=vertical;}else{r=vertical;b=horizontal;}
    }
    values[0]=band->luts[r*2];
    values[1]=band->luts[2048+g*2];
    values[2]=band->luts[4096+b*2];
}

static void *develop(void *context) {
    struct cronos_band *band=context;
    for (unsigned y=band->y0;y<band->y1;y+=2) {
        for (unsigned x=0;x<DW;x+=2) {
            int sums[3]={0};
            for (unsigned j=0;j<2;++j) for(unsigned i=0;i<2;++i) {
                unsigned v[3];rgb(band,x+i,y+j,v);
                band->output[(size_t)(y+j)*DW+x+i]=16+((66*v[0]+129*v[1]+25*v[2]+128)>>8);
                for(unsigned k=0;k<3;++k)sums[k]+=(int)v[k];
            }
            size_t k=(size_t)(y/2)*(DW/2)+x/2;
            band->output[DW*DH+k]=128+((-38*sums[0]-74*sums[1]+112*sums[2]+512)>>10);
            band->output[DW*DH*5/4+k]=128+((112*sums[0]-94*sums[1]-18*sums[2]+512)>>10);
        }
    }
    return NULL;
}

int cronos_bayer10_yuv420(const uint8_t *raw,size_t bytes,const uint8_t *luts,
                         uint16_t *scratch,uint8_t *out,size_t out_bytes) {
    if(!raw||!luts||!scratch||!out||bytes!=SW*SH*5/4||out_bytes<DW*DH*3/2)return -1;
    for(size_t j=0,k=0;j<bytes;j+=5,k+=4) {
        scratch[k]=raw[j]|(raw[j+1]&3U)<<8;
        scratch[k+1]=raw[j+1]>>2|(raw[j+2]&15U)<<6;
        scratch[k+2]=raw[j+2]>>4|(raw[j+3]&63U)<<4;
        scratch[k+3]=raw[j+3]>>6|(unsigned)raw[j+4]<<2;
    }
    struct cronos_band bands[4];pthread_t threads[3];unsigned launched=0;
    for(unsigned n=0;n<4;++n) {
        bands[n]=(struct cronos_band){scratch,luts,out,n*DH/4,(n+1)*DH/4};
        if(n<3) {
            if(pthread_create(&threads[n],NULL,develop,&bands[n])) {
                for(unsigned j=0;j<launched;++j)pthread_join(threads[j],NULL);
                return -1;
            }
            ++launched;
        } else develop(&bands[n]);
    }
    for(unsigned j=0;j<launched;++j)pthread_join(threads[j],NULL);
    return 0;
}

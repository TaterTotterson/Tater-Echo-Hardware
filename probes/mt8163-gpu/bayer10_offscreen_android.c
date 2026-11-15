/* SPDX-License-Identifier: MIT
 * Original generated-pattern RAW10 -> RGBA -> I420 GPU experiment.
 * Cronos only. Private pbuffer, no camera, framebuffer, ISP, or live pixels.
 * Dynamic EGL/GLES entry points use the owner's existing isolated runtime.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include "../mt8163-vcodec/bayer10_scaled.h"

#define SW 1600U
#define SH 1200U
#define DW 640U
#define DH 480U
#define RAW_BYTES ((size_t)SW*SH*5/4)
#define RGBA_BYTES ((size_t)DW*DH*4)
#define YUV_BYTES ((size_t)DW*DH*3/2)
#ifndef ITERATIONS
#define ITERATIONS 16
#endif
#if ITERATIONS < 1 || ITERATIONS > 128
#error "GPU prototype benchmark must remain bounded to 1..128 frames"
#endif
#if defined(TATER_GPU_BAYER_UNPACKED) && !defined(TATER_GPU_BAYER_ES3)
#error "Hybrid integer texture measurement requires ES3"
#endif

static double milliseconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}

/* Include CFA edges, each packed bit position, non-linear/channel-specific LUTs. */
static void generate(unsigned pattern, uint8_t *raw, uint8_t *luts,
                     uint16_t *samples) {
    for (unsigned c=0;c<3;++c) for(unsigned n=0;n<2048;++n) {
        unsigned value=n/8;
        if (pattern==7) value=(unsigned)(sqrt(n/2047.0)*255.0+0.5);
        if (pattern==7 && c==0) value=value*4/5;
        if (pattern==7 && c==2) value=value*3/4;
        luts[c*2048+n]=(uint8_t)value;
    }
    for (unsigned y=0;y<SH;++y) for(unsigned x=0;x<SW;++x) {
        unsigned channel=!(x&1)&&!(y&1)?0:(x&1)&&(y&1)?2:1;
        unsigned value=0;
        if (pattern==0) value=512;
        else if (pattern<=3) value=channel==pattern-1?1023:0;
        else if (pattern==4) value=((x/200)>>(2-channel))&1?1023:0;
        else if (pattern==5) value=(x*1023/(SW-1)+y*1023/(SH-1))/2;
        else value=(x*73+y*193+(x*y*7))%1024;
        samples[(size_t)y*SW+x]=(uint16_t)value;
    }
    for(size_t p=0,k=0;p<RAW_BYTES;p+=5,k+=4) {
        uint64_t bits=(uint64_t)samples[k]|(uint64_t)samples[k+1]<<10|
            (uint64_t)samples[k+2]<<20|(uint64_t)samples[k+3]<<30;
        for(unsigned n=0;n<5;++n) raw[p+n]=(uint8_t)(bits>>(8*n));
    }
}

static void reference_rgba(const uint16_t *samples,const uint8_t *luts,
                            uint8_t *rgba) {
    const struct scaled_bayer_band b={samples,luts,NULL,SW,SH,DW,DH,1,0,DH};
    for(unsigned y=0;y<DH;++y) for(unsigned x=0;x<DW;++x) {
        unsigned v[3];scaled_bayer_rgb(&b,x,y,v);
        size_t k=((size_t)y*DW+x)*4;
        for(unsigned c=0;c<3;++c)rgba[k+c]=(uint8_t)v[c];
        rgba[k+3]=255;
    }
}

struct difference {unsigned maximum; size_t differing, over_one; double mean;};
static struct difference compare(const uint8_t *a,const uint8_t *b,size_t bytes) {
    struct difference d={0};uint64_t total=0;
    for(size_t n=0;n<bytes;++n) {
        unsigned error=(unsigned)abs((int)a[n]-(int)b[n]);
        if(error>d.maximum)d.maximum=error;
        d.differing+=error!=0;d.over_one+=error>1;total+=error;
    }
    d.mean=(double)total/bytes;
    return d;
}

#ifndef TATER_GPU_BAYER_REFERENCE_ONLY
#include <dlfcn.h>
#include <sys/stat.h>
#include <EGL/egl.h>
#ifdef TATER_GPU_BAYER_ES3
#include <GLES3/gl3.h>
#ifdef TATER_GPU_BAYER_UNPACKED
#define RAW_INTERNAL GL_R16UI
#define RAW_WIDTH SW
#define RAW_TYPE GL_UNSIGNED_SHORT
#else
#define RAW_INTERNAL GL_R8UI
#define RAW_WIDTH (SW*5/4)
#define RAW_TYPE GL_UNSIGNED_BYTE
#endif
#define RAW_EXTERNAL GL_RED_INTEGER
#define CONTEXT_VERSION 3
#define CONTEXT_BIT 0x0040 /* EGL_OPENGL_ES3_BIT_KHR */
#else
#include <GLES2/gl2.h>
#define RAW_INTERNAL GL_LUMINANCE
#define RAW_EXTERNAL GL_LUMINANCE
#define RAW_WIDTH (SW*5/4)
#define RAW_TYPE GL_UNSIGNED_BYTE
#define CONTEXT_VERSION 2
#define CONTEXT_BIT EGL_OPENGL_ES2_BIT
#endif
#include "cronos_target.h"

#define GPU_APIS(X) \
 X(eglGetDisplay) X(eglInitialize) X(eglChooseConfig) X(eglBindAPI) \
 X(eglCreatePbufferSurface) X(eglCreateContext) X(eglMakeCurrent) \
 X(eglDestroyContext) X(eglDestroySurface) X(eglTerminate) X(eglGetError) \
 X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) \
 X(glGetShaderInfoLog) X(glDeleteShader) X(glCreateProgram) X(glAttachShader) \
 X(glBindAttribLocation) X(glLinkProgram) X(glGetProgramiv) \
 X(glGetProgramInfoLog) X(glDeleteProgram) X(glUseProgram) \
 X(glGenTextures) X(glDeleteTextures) X(glActiveTexture) X(glBindTexture) \
 X(glTexParameteri) X(glPixelStorei) X(glTexImage2D) X(glTexSubImage2D) \
 X(glGetUniformLocation) X(glUniform1i) X(glViewport) X(glVertexAttribPointer) \
 X(glEnableVertexAttribArray) X(glDrawArrays) X(glFinish) X(glReadPixels) \
 X(glGetError) X(glDisable) X(glGetString) X(glGenFramebuffers) \
 X(glBindFramebuffer) X(glFramebufferTexture2D) X(glCheckFramebufferStatus) \
 X(glDeleteFramebuffers) X(glGetShaderPrecisionFormat) X(glGenBuffers) \
 X(glBindBuffer) X(glBufferData) X(glDeleteBuffers)
struct gpu {
    void *library;
    FILE *diagnostic;
#define DECLARE(name) __typeof__(name) *name;
    GPU_APIS(DECLARE)
#undef DECLARE
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint program[2],texture[3],fbo,vbo;
#ifdef TATER_GPU_BAYER_UNPACKED
    uint16_t *unpacked;
#endif
};

static GLuint shader(struct gpu *g,GLenum kind,const char *source) {
    GLuint id=g->glCreateShader(kind);GLint ok=0;
    g->glShaderSource(id,1,&source,NULL);g->glCompileShader(id);
    g->glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[2048]={0};g->glGetShaderInfoLog(id,sizeof(log),NULL,log);
        fprintf(stderr,"shader_error=%s\n",log);g->glDeleteShader(id);return 0;
    }
    return id;
}

static GLuint program(struct gpu *g,const char *fragment) {
#ifdef TATER_GPU_BAYER_ES3
    const char *vertex="#version 300 es\nin vec2 position;void main(){gl_Position=vec4(position,0.,1.);}";
#else
    const char *vertex="attribute vec2 position;void main(){gl_Position=vec4(position,0.,1.);}";
#endif
    GLuint v=shader(g,GL_VERTEX_SHADER,vertex),f=shader(g,GL_FRAGMENT_SHADER,fragment);
    GLuint p=0;GLint ok=0;
    if(v&&f) {
        p=g->glCreateProgram();g->glAttachShader(p,v);g->glAttachShader(p,f);
        g->glBindAttribLocation(p,0,"position");g->glLinkProgram(p);
        g->glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if(!ok) {
            char log[2048]={0};g->glGetProgramInfoLog(p,sizeof(log),NULL,log);
            fprintf(stderr,"program_error=%s\n",log);g->glDeleteProgram(p);p=0;
        }
    }
    if(v)g->glDeleteShader(v);
    if(f)g->glDeleteShader(f);
    return p;
}

/* Integer math is expressed with highp floats (all intermediates < 2^24).
 * Match the production CPU's bilinear demosaic and integer truncation exactly.
 * Raw upload row zero and readback row zero both use GL's lower row: no flip.
 */
#ifdef TATER_GPU_BAYER_ES3
static const char bayer_fragment[]=
    "#version 300 es\nprecision highp float;precision highp int;"
    "uniform highp usampler2D raw;uniform highp usampler2D lut;out vec4 color;"
#ifdef TATER_GPU_BAYER_UNPACKED
    "uint sampleAt(int x,int y){return texelFetch(raw,ivec2(x,y),0).r;}"
#else
    "uint sampleAt(int x,int y){int bit=x*10;int b=bit>>3;"
    "uint v=texelFetch(raw,ivec2(b,y),0).r|(texelFetch(raw,ivec2(b+1,y),0).r<<8);"
    "return (v>>uint(bit&7))&1023u;}"
#endif
    "float tone(uint n,int c){return float(texelFetch(lut,ivec2(int(n)*2,c),0).r);}"
    "void main(){ivec2 p=ivec2(gl_FragCoord.xy)*5/2;int x=p.x,y=p.y;"
    "int xm=x>0?x-1:1,xp=x+1<1600?x+1:1598;"
    "int ym=y>0?y-1:1,yp=y+1<1200?y+1:1198;uint r,g,b,c=sampleAt(x,y);"
    "if((x&1)==(y&1)){"
    "uint diag=(sampleAt(xm,ym)+sampleAt(xp,ym)+sampleAt(xm,yp)+sampleAt(xp,yp))/4u;"
    "g=(sampleAt(xm,y)+sampleAt(xp,y)+sampleAt(x,ym)+sampleAt(x,yp))/4u;"
    "if((y&1)==0){r=c;b=diag;}else{r=diag;b=c;}"
    "}else{uint h=(sampleAt(xm,y)+sampleAt(xp,y))/2u;uint v=(sampleAt(x,ym)+sampleAt(x,yp))/2u;g=c;"
    "if((y&1)==0){r=h;b=v;}else{r=v;b=h;}}"
    "color=vec4(tone(r,0),tone(g,1),tone(b,2),255.)/255.;}";

static const char i420_fragment[]=
    "#version 300 es\nprecision highp float;precision highp int;uniform highp sampler2D rgb;out vec4 color;"
    "ivec3 at(ivec2 p){return ivec3(floor(texelFetch(rgb,p,0).rgb*255.+.5));}"
    "float outByte(int k){if(k<307200){ivec3 c=at(ivec2(k%640,k/640));"
    "return float(16+((66*c.r+129*c.g+25*c.b+128)>>8));}"
    "int q=k-307200;bool v=q>=76800;if(v)q-=76800;ivec2 p=ivec2(q%320,q/320)*2;"
    "ivec3 s=at(p)+at(p+ivec2(1,0))+at(p+ivec2(0,1))+at(p+ivec2(1,1));"
    "int t=v?112*s.r-94*s.g-18*s.b:-38*s.r-74*s.g+112*s.b;"
    "return float(128+((t+512)>>10));}"
    "void main(){ivec2 p=ivec2(gl_FragCoord.xy);int k=(p.y*160+p.x)*4;"
    "color=vec4(outByte(k),outByte(k+1),outByte(k+2),outByte(k+3))/255.;}";
#else
static const char bayer_fragment[]=
    "precision highp float;uniform highp sampler2D raw;uniform highp sampler2D lut;"
    "float byteAt(float x,float y){return floor(texture2D(raw,(vec2(x,y)+.5)/vec2(2000.,1200.)).r*255.+.5);}"
    "float sampleAt(float x,float y){float bit=x*10.;float b=floor(bit*.125);float shift=mod(bit,8.);"
    "float scale=shift==0.?1.:shift==2.?.25:shift==4.?.0625:.015625;"
    "return mod(floor((byteAt(b,y)+256.*byteAt(b+1.,y))*scale),1024.);}"
    "float tone(float n,float c){return floor(texture2D(lut,vec2((2.*n+.5)/2048.,(c+.5)/3.)).r*255.+.5);}"
    "void main(){vec2 p=floor(gl_FragCoord.xy);float x=floor(p.x*2.5);float y=floor(p.y*2.5);"
    "float xm=x>0.?x-1.:1.;float xp=x+1.<1600.?x+1.:1598.;"
    "float ym=y>0.?y-1.:1.;float yp=y+1.<1200.?y+1.:1198.;"
    "float r,g,b;float c=sampleAt(x,y);"
    "if(mod(x,2.)==mod(y,2.)){"
    "float diag=floor((sampleAt(xm,ym)+sampleAt(xp,ym)+sampleAt(xm,yp)+sampleAt(xp,yp))/4.);"
    "g=floor((sampleAt(xm,y)+sampleAt(xp,y)+sampleAt(x,ym)+sampleAt(x,yp))/4.);"
    "if(mod(y,2.)==0.){r=c;b=diag;}else{r=diag;b=c;}"
    "}else{float h=floor((sampleAt(xm,y)+sampleAt(xp,y))/2.);"
    "float v=floor((sampleAt(x,ym)+sampleAt(x,yp))/2.);g=c;"
    "if(mod(y,2.)==0.){r=h;b=v;}else{r=v;b=h;}}"
    "gl_FragColor=vec4(tone(r,0.),tone(g,1.),tone(b,2.),255.)/255.;}";

/* Pack four I420 bytes in RGBA. 160 x 720 readback is the exact contiguous
 * 640 x 480 I420 buffer expected by the existing encoder, including U then V.
 */
static const char i420_fragment[]=
    "precision highp float;uniform highp sampler2D rgb;"
    "vec3 at(vec2 p){return floor(texture2D(rgb,(p+.5)/vec2(640.,480.)).rgb*255.+.5);}"
    "float outByte(float k){if(k<307200.){vec2 p=vec2(mod(k,640.),floor(k/640.));"
    "return 16.+floor((dot(at(p),vec3(66.,129.,25.))+128.)/256.);}"
    "float q=k-307200.;bool v=q>=76800.;if(v)q-=76800.;"
    "vec2 p=vec2(mod(q,320.),floor(q/320.))*2.;"
    "vec3 s=at(p)+at(p+vec2(1.,0.))+at(p+vec2(0.,1.))+at(p+vec2(1.,1.));"
    "return 128.+floor((dot(s,v?vec3(112.,-94.,-18.):vec3(-38.,-74.,112.))+512.)/1024.);}"
    "void main(){vec2 p=floor(gl_FragCoord.xy);float k=(p.y*160.+p.x)*4.;"
    "gl_FragColor=vec4(outByte(k),outByte(k+1.),outByte(k+2.),outByte(k+3.))/255.;}";
#endif

static void texture(struct gpu *g,unsigned unit,GLenum format,GLenum external,GLenum type,
                    unsigned w,unsigned h,const void *data) {
    g->glActiveTexture(GL_TEXTURE0+unit);g->glBindTexture(GL_TEXTURE_2D,g->texture[unit]);
    g->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    g->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    g->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    g->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    g->glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,external,type,data);
}

static int initialize(struct gpu *g) {
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,CONTEXT_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint sa[]={EGL_WIDTH,DW/4,EGL_HEIGHT,DH*3/2,EGL_NONE};
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,CONTEXT_VERSION,EGL_NONE};
    EGLConfig config;EGLint count=0,major=0,minor=0;GLint range[2],precision=0;
    static const GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};
    g->library=dlopen("libGLES_mali.so",RTLD_NOW|RTLD_LOCAL);
    if(!g->library){fprintf(stderr,"dlopen_error=%s\n",dlerror());return -1;}
#define LOAD(name) do {void *p=dlsym(g->library,#name);if(!p){fprintf(stderr,"missing_api=%s\n",#name);return -1;}memcpy(&g->name,&p,sizeof(p));}while(0);
    GPU_APIS(LOAD)
#undef LOAD
    g->display=g->eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(g->display==EGL_NO_DISPLAY||!g->eglInitialize(g->display,&major,&minor)||
       !g->eglChooseConfig(g->display,ca,&config,1,&count)||count!=1||!g->eglBindAPI(EGL_OPENGL_ES_API))return -1;
    g->surface=g->eglCreatePbufferSurface(g->display,config,sa);
    g->context=g->eglCreateContext(g->display,config,EGL_NO_CONTEXT,ctx);
    if(g->surface==EGL_NO_SURFACE||g->context==EGL_NO_CONTEXT||
       !g->eglMakeCurrent(g->display,g->surface,g->surface,g->context))return -1;
    FILE *report=g->diagnostic?g->diagnostic:stdout;
    fprintf(report,"gpu_vendor=%s gpu_renderer=%s gpu_version=%s\n",g->glGetString(GL_VENDOR),g->glGetString(GL_RENDERER),g->glGetString(GL_VERSION));
    g->glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
    fprintf(report,"fragment_highp_bits=%d range=%d,%d\n",precision,range[0],range[1]);
    if(precision<23)return -1;
    g->program[0]=program(g,bayer_fragment);g->program[1]=program(g,i420_fragment);
    if(!g->program[0]||!g->program[1])return -1;
    g->glDisable(GL_DITHER);g->glDisable(GL_BLEND);g->glDisable(GL_DEPTH_TEST);
    g->glPixelStorei(GL_UNPACK_ALIGNMENT,1);g->glPixelStorei(GL_PACK_ALIGNMENT,1);
    g->glGenTextures(3,g->texture);
    texture(g,0,RAW_INTERNAL,RAW_EXTERNAL,RAW_TYPE,RAW_WIDTH,SH,NULL);
#ifdef TATER_GPU_BAYER_ES3
    texture(g,1,GL_R8UI,GL_RED_INTEGER,GL_UNSIGNED_BYTE,2048,3,NULL);
#else
    texture(g,1,GL_LUMINANCE,GL_LUMINANCE,GL_UNSIGNED_BYTE,2048,3,NULL);
#endif
    texture(g,2,GL_RGBA,GL_RGBA,GL_UNSIGNED_BYTE,DW,DH,NULL);
#ifdef TATER_GPU_BAYER_UNPACKED
    g->unpacked=malloc((size_t)SW*SH*2);
    if(!g->unpacked)return -1;
#endif
    g->glGenFramebuffers(1,&g->fbo);g->glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g->texture[2],0);
    if(g->glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return -1;
    g->glGenBuffers(1,&g->vbo);g->glBindBuffer(GL_ARRAY_BUFFER,g->vbo);
    g->glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    g->glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);g->glEnableVertexAttribArray(0);
    g->glUseProgram(g->program[0]);g->glUniform1i(g->glGetUniformLocation(g->program[0],"raw"),0);
    g->glUniform1i(g->glGetUniformLocation(g->program[0],"lut"),1);
    g->glUseProgram(g->program[1]);g->glUniform1i(g->glGetUniformLocation(g->program[1],"rgb"),2);
    return g->glGetError()==GL_NO_ERROR?0:-1;
}

static void destroy(struct gpu *g) {
#ifdef TATER_GPU_BAYER_UNPACKED
    free(g->unpacked);
#endif
    if(g->context&&g->glDeleteTextures) {
        g->glDeleteTextures(3,g->texture);
        if(g->vbo)g->glDeleteBuffers(1,&g->vbo);
        if(g->fbo)g->glDeleteFramebuffers(1,&g->fbo);
        for(unsigned n=0;n<2;++n)if(g->program[n])g->glDeleteProgram(g->program[n]);
    }
    if(g->display&&g->eglMakeCurrent)g->eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(g->context&&g->eglDestroyContext)g->eglDestroyContext(g->display,g->context);
    if(g->surface&&g->eglDestroySurface)g->eglDestroySurface(g->display,g->surface);
    if(g->display&&g->eglTerminate)g->eglTerminate(g->display);
    if(g->library)dlclose(g->library);
}

static int convert(struct gpu *g,const uint8_t *raw,const uint8_t *luts,uint8_t *rgba,uint8_t *yuv) {
    const void *upload=raw;
#ifdef TATER_GPU_BAYER_UNPACKED
    /* Hybrid measurement: retain only byte unpacking on CPU, not demosaic,
     * white balance, gamma, scaling or RGB->I420 conversion. Included in time.
     */
    for(size_t j=0,k=0;j<RAW_BYTES;j+=5,k+=4) {
        g->unpacked[k]=raw[j]|(raw[j+1]&3U)<<8;
        g->unpacked[k+1]=raw[j+1]>>2|(raw[j+2]&15U)<<6;
        g->unpacked[k+2]=raw[j+2]>>4|(raw[j+3]&63U)<<4;
        g->unpacked[k+3]=raw[j+3]>>6|(unsigned)raw[j+4]<<2;
    }
    upload=g->unpacked;
#endif
    g->glActiveTexture(GL_TEXTURE0);g->glBindTexture(GL_TEXTURE_2D,g->texture[0]);
    g->glTexSubImage2D(GL_TEXTURE_2D,0,0,0,RAW_WIDTH,SH,RAW_EXTERNAL,RAW_TYPE,upload);
    g->glActiveTexture(GL_TEXTURE1);g->glBindTexture(GL_TEXTURE_2D,g->texture[1]);
    g->glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2048,3,RAW_EXTERNAL,GL_UNSIGNED_BYTE,luts);
    /* Avoid a render-to-texture feedback loop, even though program 0 only uses units 0 and 1. */
    g->glActiveTexture(GL_TEXTURE2);g->glBindTexture(GL_TEXTURE_2D,0);
    g->glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);g->glViewport(0,0,DW,DH);
    g->glUseProgram(g->program[0]);g->glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    if(rgba)g->glReadPixels(0,0,DW,DH,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    g->glBindFramebuffer(GL_FRAMEBUFFER,0);g->glViewport(0,0,DW/4,DH*3/2);
    g->glBindTexture(GL_TEXTURE_2D,g->texture[2]);g->glUseProgram(g->program[1]);
    g->glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    g->glReadPixels(0,0,DW/4,DH*3/2,GL_RGBA,GL_UNSIGNED_BYTE,yuv);
    return g->glGetError()==GL_NO_ERROR?0:-1;
}

static int order_double(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);
}
static double cpu_milliseconds(void) {
    struct rusage usage;getrusage(RUSAGE_SELF,&usage);
    return (usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)*1000.0+
        (usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1000.0;
}

int main(int argc,char **argv) {
    struct gpu g={0};struct stat node;int status=1;
    uint8_t *raw=NULL,*luts=NULL,*rgba=NULL,*reference=NULL,*yuv=NULL,*cpu=NULL;
    uint16_t *samples=NULL,*scratch=NULL;unsigned failed=0;
    setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
    if(argc!=8||strcmp(argv[1],"--generated-bayer")||strcmp(argv[2],"--target")||
       strcmp(argv[3],"cronos")||strcmp(argv[4],"--acknowledge-gpu-job")||
       strcmp(argv[5],"private-pbuffer-no-camera")||strcmp(argv[6],"--bounded")||strcmp(argv[7],"run")) {
        fprintf(stderr,"explicit generated-only Cronos GPU acknowledgment required\n");return 2;
    }
    if(!cronos_gpu_target_allowed()||stat("/dev/mali0",&node)||!S_ISCHR(node.st_mode)) {
        fprintf(stderr,"target_refused\n");return 2;
    }
    alarm(45);
    raw=malloc(RAW_BYTES);luts=malloc(6144);rgba=malloc(RGBA_BYTES);reference=malloc(RGBA_BYTES);
    yuv=malloc(YUV_BYTES);cpu=malloc(YUV_BYTES);samples=malloc((size_t)SW*SH*2);scratch=malloc((size_t)SW*SH*2);
    if(!raw||!luts||!rgba||!reference||!yuv||!cpu||!samples||!scratch)goto done;
    printf("TATER_GPU_BAYER_BEGIN generated_only=1 source=%ux%u output=%ux%u\n",SW,SH,DW,DH);
#ifdef TATER_GPU_BAYER_UNPACKED
    printf("variant=es3_hybrid_cpu_unpack\n");
#elif defined(TATER_GPU_BAYER_ES3)
    printf("variant=es3_packed_integer\n");
#else
    printf("variant=es2_packed_float\n");
#endif
    if(initialize(&g))goto done;
    for(unsigned n=0;n<8;++n) {
        generate(n,raw,luts,samples);reference_rgba(samples,luts,reference);
        if(scaled_bayer10_yuv420(raw,RAW_BYTES,luts,SW,SH,DW,DH,1,scratch,cpu,YUV_BYTES)||
           convert(&g,raw,luts,rgba,yuv))goto done;
        struct difference a=compare(rgba,reference,RGBA_BYTES),b=compare(yuv,cpu,YUV_BYTES);
        printf("pattern=%u rgba_max=%u rgba_mean=%.8f rgba_differing=%zu i420_max=%u i420_mean=%.8f i420_differing=%zu i420_over_one=%zu\n",
            n,a.maximum,a.mean,a.differing,b.maximum,b.mean,b.differing,b.over_one);
        if(a.differing) {
            unsigned shown=0;
            for(size_t k=0;k<RGBA_BYTES&&shown<4;++k)if(rgba[k]!=reference[k]) {
                printf("rgba_difference pattern=%u x=%zu y=%zu channel=%zu gpu=%u cpu=%u\n",
                    n,k/4%DW,k/4/DW,k%4,rgba[k],reference[k]);++shown;
            }
        }
        if(a.over_one||b.over_one||a.mean>.01||b.mean>.01)++failed;
    }
    if(failed){printf("accuracy_failed=%u\n",failed);goto done;}
    double gpu_ms[ITERATIONS],cpu_ms[ITERATIONS],c0=cpu_milliseconds();
    for(unsigned n=0;n<ITERATIONS;++n) {
        double t=milliseconds();if(convert(&g,raw,luts,NULL,yuv))goto done;gpu_ms[n]=milliseconds()-t;
    }
    double gpu_cpu=(cpu_milliseconds()-c0)/ITERATIONS;c0=cpu_milliseconds();
    for(unsigned n=0;n<ITERATIONS;++n) {
        double t=milliseconds();
        if(scaled_bayer10_yuv420(raw,RAW_BYTES,luts,SW,SH,DW,DH,1,scratch,cpu,YUV_BYTES))goto done;
        cpu_ms[n]=milliseconds()-t;
    }
    double cpu_cpu=(cpu_milliseconds()-c0)/ITERATIONS;
    qsort(gpu_ms,ITERATIONS,sizeof(double),order_double);qsort(cpu_ms,ITERATIONS,sizeof(double),order_double);
    printf("benchmark_frames=%d gpu_min_ms=%.3f gpu_median_ms=%.3f gpu_max_ms=%.3f gpu_cpu_ms=%.3f cpu_min_ms=%.3f cpu_median_ms=%.3f cpu_max_ms=%.3f cpu_cpu_ms=%.3f\n",
        ITERATIONS,gpu_ms[0],gpu_ms[ITERATIONS/2],gpu_ms[ITERATIONS-1],gpu_cpu,
        cpu_ms[0],cpu_ms[ITERATIONS/2],cpu_ms[ITERATIONS-1],cpu_cpu);
    printf("TATER_GPU_BAYER_PASS generated_only=1 production_changed=0\n");status=0;
done:
    if(status)fprintf(stderr,"TATER_GPU_BAYER_FAIL\n");
    destroy(&g);free(raw);free(luts);free(rgba);free(reference);free(yuv);free(cpu);free(samples);free(scratch);
    return status;
}
#endif

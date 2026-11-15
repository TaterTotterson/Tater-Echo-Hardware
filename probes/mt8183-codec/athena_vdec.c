/* Original continuous Baseline H.264 TVI2/TVO2 adapter. Owner runtime external. */
#include "ap_decoder_abi.h"
#include "baseline_packet.h"
#include "ap_stream_io.h"
#ifndef TATER_ATHENA_CODEC_ROOT
#define TATER_ATHENA_CODEC_ROOT "/data/tater-athena-codec"
#endif
#define CAPACITY AP_INPUT_CAPACITY
#define MAX_RGBA (1280U*960U*4U)
struct stream_pool {
    union {VDEC_DRV_FRAMEBUF_T frame;unsigned char backing[4096];} slots[12];
    unsigned active[12],displayed[12],requests,current_token;
    unsigned token[12];uint64_t pts[12],current_pts;
};
static struct stream_pool stream_pool;
static int32_t provide_stream(unsigned long context,VDEC_DRV_FRAMEBUF_T **frame,uint32_t flags,int8_t eos,void *extra) {
    (void)flags;(void)eos;(void)extra;if(!frame)return -1;*frame=NULL;
    if(context!=(uintptr_t)&stream_pool)return -1;
    for(unsigned i=0;i<12;i++)if(!stream_pool.active[i]) {
        *frame=&stream_pool.slots[i].frame;(*frame)->u4Timestamp=stream_pool.current_token;
        stream_pool.active[i]=1;stream_pool.requests++;
        stream_pool.token[i]=stream_pool.current_token;stream_pool.pts[i]=stream_pool.current_pts;return 0;
    }
    return -1;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--capabilities")) {
        puts("{\"adapter\":\"tater-athena-ap-vdec\",\"version\":1,\"codec\":\"h264\",\"stream_versions\":[2],\"stream_v2_output\":\"rgba\",\"max_width\":1280,\"max_height\":960,\"profiles\":[\"baseline\"],\"single_slice_only\":true,\"live_only\":true}");return fflush(stdout)?3:0;
    }
    if(argc!=6 || strcmp(argv[1],"--stream-h264-rgba-v2") ||
       strcmp(argv[2],TATER_ATHENA_CODEC_ROOT "/lib/libvcodecdrv.so") ||
       strcmp(argv[5],"30") || !target_ok())return 2;
    char *end=NULL;unsigned long width=strtoul(argv[3],&end,10);
    if(!end || *end || width<16 || width>1280 || (width&1))return 2;
    unsigned long height=strtoul(argv[4],&end,10);
    if(!end || *end || height<16 || height>960 || (height&1))return 2;
    unsigned bw=(width+15)&~15U,bh=(height+31)&~31U,rgba_bytes=width*height*4;
    trace_enabled=getenv("TATER_CODEC_TRACE") && !strcmp(getenv("TATER_CODEC_TRACE"),"1");
    setvbuf(stderr,NULL,_IONBF,0);
    signal(SIGPIPE,SIG_IGN);
    owned_write_fd=dup(STDOUT_FILENO);
    if(owned_write_fd<0 || dup2(STDERR_FILENO,STDOUT_FILENO)<0)return 3;
    int output_flags=fcntl(owned_write_fd,F_GETFL);
    if(output_flags<0 || fcntl(owned_write_fd,F_SETFL,output_flags|O_NONBLOCK))return 3;
    setvbuf(stdout,NULL,_IONBF,0);
    owned_codec=VAL_MEM_CODEC_FOR_VDEC;owned_driver=VAL_DRIVER_TYPE_H264_DEC;
    register_mode=1;irq_failure_reporter=irq_failure_snapshot;
    adapter=open("/dev/tater-athena-ap-vdec",O_RDWR|O_CLOEXEC);
    struct ap_info info;uint32_t on=1;
    if(adapter<0 || ioctl(adapter,AP_INFO,&info) || info.version!=1 || info.engine!=1 ||
       !info.mmio_enabled || info.powered || ioctl(adapter,AP_POWER,&on))return 5;
    VAL_MEMORY_T input={0},outputs[12];memset(outputs,0,sizeof outputs);
    input.eMemCodec=VAL_MEM_CODEC_FOR_VDEC;input.eMemType=VAL_MEM_TYPE_FOR_HW_NONCACHEABLE;
    input.eAlignment=VAL_MEM_ALIGN_4K;input.u4MemSize=CAPACITY;
    if(eVideoMemAlloc(&input,sizeof input)!=VAL_RESULT_NO_ERROR)return 6;
    const int keep[]={adapter,owned_write_fd,(int)input.i4IonShareFd};
    if(close_unrelated(keep,sizeof keep/sizeof keep[0]))return 7;
    if(fence(adapter))return 7;
    watchdog(5);
    void *library=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);
    void *utility=dlopen(TATER_ATHENA_CODEC_ROOT "/lib/libvcodec_utility.so",RTLD_NOW|RTLD_LOCAL);
    if(!library || !utility)return 8;
    void *symbol=dlsym(utility,"eHalCmdProc");memcpy(&original_hal_command,&symbol,sizeof original_hal_command);
    if(!original_hal_command || original_hal_command==eHalCmdProc)return 9;
    LOAD(eVDecDrvCreate,create_fn);LOAD(eVDecDrvRelease,release_fn);LOAD(eVDecDrvInit,init_fn);
    LOAD(eVDecDrvDeInit,release_fn);LOAD(eVDecDrvSetParam,set_fn);LOAD(eVDecDrvGetParam,get_fn);
    LOAD(eVDecDrvDecodeEx,decode_ex_fn);
    VAL_HANDLE_T handle=0;
    if(eVDecDrvCreate(&handle,VDEC_DRV_VIDEO_FORMAT_H264) || !handle)return 10;
    VDEC_DRV_BUFFER_CONTROL_T mode=VDEC_DRV_BUFFER_CONTROL_REF_IS_DISP_EXT;
    VDEC_DRV_FBTYPE_T type=VDEC_DRV_FBTYPE_YUV420_BK_16x1;
    unsigned external_timestamp=1;
    if(eVDecDrvSetParam(handle,VDEC_DRV_SET_TYPE_USE_EXT_TIMESTAMP,&external_timestamp,NULL) ||
       eVDecDrvSetParam(handle,VDEC_DRV_SET_TYPE_SET_BUFFER_MODE,&mode,NULL) ||
       eVDecDrvSetParam(handle,VDEC_DRV_SET_TYPE_SET_FRAME_BUFFER_TYPE,&type,NULL))return 11;
    static unsigned char packet[CAPACITY],cached[65536],rgba[MAX_RGBA];
    static struct ap_parameter_sets parameter_sets;
    unsigned cache_bytes=0,initialized=0,config_records=0;
    uint64_t decoded=0,submitted=0;
    const unsigned slot_bytes=(bw*bh*3/2+4095)&~4095U;
    VDEC_DRV_RINGBUF_T ring;memset(&ring,0,sizeof ring);
    ring.rBase.u4VA=(uintptr_t)input.pvMemVa;ring.rBase.u4PA=(uintptr_t)input.pvMemPa;
    uint64_t previous_pts=0;
    watchdog(0);
    for(;;) {
        unsigned char header[12];if(transfer(0,header,sizeof header,0))return 12;
        uint32_t bytes=load32(header);uint64_t pts=load64(header+4);
        if(!bytes) {
            if(pts || !initialized || decoded!=submitted)return 13;
            break;
        }
        if(bytes>CAPACITY-65536-64 || pts>INT64_MAX || (submitted && pts<=previous_pts) || transfer(0,packet,bytes,0))return 15;
        unsigned video_bytes=0;
        int picture=ap_baseline_packet(&parameter_sets,packet,bytes,&video_bytes);
        if(picture<0)return 16;
        if(!picture) {
            if(initialized || ++config_records>4)return 22;
            continue;
        }
        cache_bytes=parameter_sets.sps_bytes+parameter_sets.pps_bytes;
        memcpy(cached,parameter_sets.sps,parameter_sets.sps_bytes);
        memcpy(cached+parameter_sets.sps_bytes,parameter_sets.pps,parameter_sets.pps_bytes);
        if(submitted>=UINT32_MAX-1)return 23; /* token wrap: close/reopen, never alias */
        if(!initialized) {
            memcpy(input.pvMemVa,cached,cache_bytes);
            ring.rBase.u4Size=cache_bytes;ring.u4Read=ring.rBase.u4VA;
            ring.u4Write=ring.u4Read+cache_bytes;ring.u4InputFlag=1;
            union {VDEC_DRV_SEQINFO_T fields;unsigned char backing[4096];} sequence;memset(&sequence,0,sizeof sequence);
            watchdog(5);
            if(eVDecDrvInit(handle,&ring,&sequence.fields) || sequence.fields.u4Width<width || sequence.fields.u4Width>bw ||
               sequence.fields.u4Height<height || sequence.fields.u4Height>bh ||
               sequence.fields.u4PicWidth!=width || sequence.fields.u4PicHeight!=height ||
               sequence.fields.u4BitDepthLuma!=8 || sequence.fields.u4BitDepthChroma!=8)return 24;
            unsigned pixel=0;if(eVDecDrvGetParam(handle,VDEC_DRV_GET_TYPE_GET_PIXEL_FORMAT,NULL,&pixel) || pixel!=2)return 25;
            VDEC_DRV_SET_DECODE_MODE_T low_latency={VDEC_DRV_DECODE_MODE_LOW_LATENCY_DECODE,0,0};
            if(eVDecDrvSetParam(handle,VDEC_DRV_SET_TYPE_SET_DECODE_MODE,&low_latency,NULL))return 26;
            for(unsigned i=0;i<12;i++) {
                VAL_MEMORY_T *memory=&outputs[i];
                memory->eMemCodec=VAL_MEM_CODEC_FOR_VDEC;memory->eMemType=VAL_MEM_TYPE_FOR_HW_NONCACHEABLE;
                memory->eAlignment=VAL_MEM_ALIGN_4K;memory->u4MemSize=slot_bytes;
                if(eVideoMemAlloc(memory,sizeof *memory)!=VAL_RESULT_NO_ERROR)return 43;
                VDEC_DRV_FRAMEBUF_T *frame=&stream_pool.slots[i].frame;
                frame->rBaseAddr.u4VA=(uintptr_t)memory->pvMemVa;
                frame->rBaseAddr.u4PA=(uintptr_t)memory->pvMemPa;frame->rBaseAddr.u4Size=slot_bytes;
                frame->u4BufWidth=bw;frame->u4BufHeight=bh;frame->u4FrameBufferType=type;
            }
            struct output_callback callback={&stream_pool,NULL,provide_stream};
            if(eVDecDrvSetParam(handle,(VDEC_DRV_SET_TYPE_T)23,&callback,NULL) || callback.context!=(void *)(uintptr_t)handle ||
               !callback.callback || callback.callback==provide_stream)return 27;
            unsigned char output_header[32]={0};memcpy(output_header,"TVO2",4);store32(output_header+4,32);
            store32(output_header+8,width);store32(output_header+12,height);store32(output_header+16,rgba_bytes);
            memcpy(output_header+20,"RGBA",4);store32(output_header+24,30);store32(output_header+28,1);
            if(transfer(owned_write_fd,output_header,sizeof output_header,1))return 28;
            initialized=1;
        }
        unsigned char *active=input.pvMemVa;
        memcpy(active,cached,cache_bytes);memcpy(active+cache_bytes,packet,video_bytes);
        static const unsigned char delimiter[]={0,0,0,1,9,0xf0};
        memcpy(active+cache_bytes+video_bytes,delimiter,sizeof delimiter);
        unsigned logical_bytes=cache_bytes+video_bytes+sizeof delimiter;
        memset(active+logical_bytes,0,64);ring.rBase.u4Size=logical_bytes;
        ring.u4Read=ring.rBase.u4VA+cache_bytes;ring.u4Write=ring.rBase.u4VA+logical_bytes;
        ring.u4InputFlag=0;ring.u4Timestamp=submitted+1;
        stream_pool.current_token=(unsigned)submitted+1;stream_pool.current_pts=pts;
        __sync_synchronize();
        watchdog(5);
        if(eVDecDrvDecodeEx(handle,&ring,NULL,NULL))return 29;
        submitted++;previous_pts=pts;
        VDEC_DRV_FRAMEBUF_T *display=NULL;
        if(eVDecDrvGetParam(handle,VDEC_DRV_GET_TYPE_DISP_FRAME_BUFFER,NULL,&display) || !display)return 30;
        unsigned slot=12;for(unsigned i=0;i<12;i++)if(display==&stream_pool.slots[i].frame)slot=i;
        if(slot==12 || !stream_pool.active[slot] || stream_pool.displayed[slot])return 31;
        uintptr_t va=(uintptr_t)outputs[slot].pvMemVa,pa=(uintptr_t)outputs[slot].pvMemPa;
        if(display->rBaseAddr.u4VA!=va || display->rBaseAddr.u4PA!=pa || display->u4BufWidth!=bw || display->u4BufHeight!=bh ||
           display->u4IsUFOEncoded || (display->u4FrameBufferType!=0 && display->u4FrameBufferType!=type))return 32;
        if(display->u4Timestamp!=stream_pool.token[slot] || stream_pool.token[slot]!=submitted ||
           stream_pool.pts[slot]!=pts)return 33;
        __sync_synchronize();
        if(tater_mtk_yuv420_block_to_rgba8888((void *)va,slot_bytes,bw,bh,width,height,rgba,width*4,rgba_bytes))return 34;
        unsigned char frame_header[16]={0};store32(frame_header,rgba_bytes);store64(frame_header+8,stream_pool.pts[slot]);
        if(transfer(owned_write_fd,frame_header,sizeof frame_header,1) || transfer(owned_write_fd,rgba,rgba_bytes,1))return 35;
        stream_pool.displayed[slot]=1;decoded++;
        VDEC_DRV_RINGBUF_T *free_input=NULL;
        if(eVDecDrvGetParam(handle,(VDEC_DRV_GET_TYPE_T)21,NULL,&free_input) || free_input!=&ring)return 36;
        for(unsigned n=0;n<13;n++) {
            VDEC_DRV_FRAMEBUF_T *released=NULL;
            if(eVDecDrvGetParam(handle,VDEC_DRV_GET_TYPE_FREE_FRAME_BUFFER,NULL,&released))return 37;
            if(!released)break;if(n==12)return 38;
            unsigned free_slot=12;for(unsigned i=0;i<12;i++)if(released==&stream_pool.slots[i].frame)free_slot=i;
            if(free_slot==12 || !stream_pool.active[free_slot] || !stream_pool.displayed[free_slot])return 39;
            stream_pool.active[free_slot]=0;stream_pool.displayed[free_slot]=0;
        }
        watchdog(0);
    }
    watchdog(5);
    if(!decoded || decoded!=submitted || eVDecDrvDeInit(handle) || eVDecDrvRelease(handle))return 40;
    for(unsigned i=0;i<12;i++)if(eVideoMemFree(&outputs[i],sizeof outputs[i])!=VAL_RESULT_NO_ERROR)return 41;
    if(eVideoMemFree(&input,sizeof input)!=VAL_RESULT_NO_ERROR)return 41;
    unsigned live=0;for(unsigned i=0;i<64;i++)live+=slots[i].cpu!=NULL;
    if(live)return 42;
    unsigned char end_record[16]={0};store32(end_record+4,1);
    if(transfer(owned_write_fd,end_record,sizeof end_record,1))return 14;
    watchdog(0);
    fprintf(stderr,"ATHENA_VDEC_FINISHED frames=%llu live_buffers=%u\n",(unsigned long long)decoded,live);
    _exit(0);
}


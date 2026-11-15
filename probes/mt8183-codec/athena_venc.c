/* Original continuous NV12/OpenMAX encoder; no camera or vendor implementation. */
#include "ap_legacy.c"
#include "ap_stream_io.h"
#include <signal.h>
#ifndef TATER_ATHENA_CODEC_ROOT
#define TATER_ATHENA_CODEC_ROOT "/data/tater-athena-codec"
#endif
#include <media/openmax/OMX_Component.h>
#include <media/openmax/OMX_Core.h>
#include <media/openmax/OMX_Video.h>
struct callback_state { pthread_mutex_t mutex; OMX_STATETYPE completed; OMX_ERRORTYPE error;
    OMX_BUFFERHEADERTYPE *outputs[16];unsigned output_count,filled,emptied,eos;
    uint64_t bytes;
    OMX_BUFFERHEADERTYPE *inputs[16];unsigned input_count,input_available[16];
    OMX_COMPONENTTYPE *component;unsigned recycle;
    unsigned output_available[16]; };
static OMX_ERRORTYPE event_callback(OMX_HANDLETYPE c,OMX_PTR opaque,OMX_EVENTTYPE event,OMX_U32 a,OMX_U32 b,OMX_PTR extra) {
    (void)c;(void)extra;struct callback_state *s=opaque;
    pthread_mutex_lock(&s->mutex);
    if(event==OMX_EventCmdComplete && a==OMX_CommandStateSet)s->completed=b;
    if(event==OMX_EventError)s->error=a;
    pthread_mutex_unlock(&s->mutex);
    AP_TRACE("OMX_AP_EVENT type=%u a=%u b=%u\n",event,a,b);return OMX_ErrorNone;
}
static OMX_ERRORTYPE empty_callback(OMX_HANDLETYPE c,OMX_PTR opaque,OMX_BUFFERHEADERTYPE *b) {
    (void)c;struct callback_state *s=opaque;
    pthread_mutex_lock(&s->mutex);
    unsigned slot=16;for(unsigned i=0;i<s->input_count;i++)if(s->inputs[i]==b)slot=i;
    if(slot==16 || s->input_available[slot])s->error=OMX_ErrorBadParameter;
    else{s->input_available[slot]=1;s->emptied++;}
    pthread_mutex_unlock(&s->mutex);return OMX_ErrorNone;
}
static OMX_ERRORTYPE fill_callback(OMX_HANDLETYPE c,OMX_PTR opaque,OMX_BUFFERHEADERTYPE *buffer) {
    (void)c;struct callback_state *state=opaque;
    pthread_mutex_lock(&state->mutex);
    unsigned slot=16;for(unsigned i=0;i<state->output_count;i++)if(state->outputs[i]==buffer)slot=i;
    if(slot==16 || state->output_available[slot]!=1 || buffer->nOffset>buffer->nAllocLen ||
       buffer->nFilledLen>buffer->nAllocLen-buffer->nOffset || buffer->nFilledLen>4U*1024U*1024U)_exit(30);
    state->output_available[slot]=0;
    if(!buffer->nFilledLen && !(buffer->nFlags&OMX_BUFFERFLAG_EOS) && (!state->recycle || state->eos)) {
        pthread_mutex_unlock(&state->mutex);return OMX_ErrorNone;
    }
    unsigned ignored=0;
    pthread_mutex_lock(&allocation_mutex);
    unsigned invalid=owned_mva((unsigned)(uintptr_t)buffer->pBuffer+buffer->nOffset,buffer->nFilledLen,&ignored);
    pthread_mutex_unlock(&allocation_mutex);
    if(invalid)_exit(31);
    unsigned flags=(buffer->nFlags&OMX_BUFFERFLAG_EOS?1U:0U) |
        (buffer->nFlags&OMX_BUFFERFLAG_CODECCONFIG?2U:0U) |
        (buffer->nFlags&OMX_BUFFERFLAG_SYNCFRAME?4U:0U);
    if(!buffer->nFilledLen && !(flags&1))_exit(32);
    unsigned char record[16]={0};store32(record,buffer->nFilledLen);store32(record+4,flags);
    store64(record+8,(uint64_t)buffer->nTimeStamp);
    if(transfer(owned_write_fd,record,sizeof record,1) ||
       (buffer->nFilledLen && transfer(owned_write_fd,buffer->pBuffer+buffer->nOffset,buffer->nFilledLen,1)))_exit(35);
    state->filled++;state->bytes+=buffer->nFilledLen;state->eos|=!!(flags&1);
    unsigned recycle=!state->error && state->recycle && !state->eos;
    OMX_COMPONENTTYPE *component=state->component;
    if(recycle)state->output_available[slot]=1;
    pthread_mutex_unlock(&state->mutex);
    if(recycle) {
        buffer->nFilledLen=0;buffer->nOffset=0;buffer->nFlags=0;
        OMX_ERRORTYPE result=component->FillThisBuffer(component,buffer);
        if(result) { pthread_mutex_lock(&state->mutex);state->error=result;pthread_mutex_unlock(&state->mutex); }
    }
    return OMX_ErrorNone;
}
static void init_header(void *data,size_t bytes) {
    memset(data,0,bytes);((OMX_U32 *)data)[0]=bytes;((OMX_U32 *)data)[1]=1;
}
static int wait_state(struct callback_state *s,OMX_STATETYPE wanted) {
    for(unsigned i=0;i<200;i++) {
        pthread_mutex_lock(&s->mutex);int ready=s->completed==wanted;unsigned error=s->error;pthread_mutex_unlock(&s->mutex);
        if(error)return -1;if(ready)return 0;usleep(10000);
    }
    return -1;
}
#define REQUIRE(call) do { OMX_ERRORTYPE result=(call);if(result!=OMX_ErrorNone) {fprintf(stderr,"ATHENA_VENC_ERROR line=%u omx=%#x\n",__LINE__,result);_exit(10);} } while(0)
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--capabilities")) {
        puts("{\"adapter\":\"tater-athena-ap-venc\",\"version\":1,\"codec\":\"h264\",\"input\":\"nv12\",\"stream_versions\":[1],\"max_width\":640,\"max_height\":480,\"max_fps\":30}");return fflush(stdout)?3:0;
    }
    if(argc!=6 || strcmp(argv[1],"--stream-nv12-h264-v1") ||
       strcmp(argv[2],TATER_ATHENA_CODEC_ROOT "/omx-lib/libMtkOmxVenc.so") ||
       strcmp(argv[3],"640") || strcmp(argv[4],"480") || strcmp(argv[5],"30") || !target_ok())return 2;
    trace_enabled=getenv("TATER_CODEC_TRACE") && !strcmp(getenv("TATER_CODEC_TRACE"),"1");
    setvbuf(stderr,NULL,_IONBF,0);signal(SIGPIPE,SIG_IGN);register_mode=1;
    owned_write_fd=dup(STDOUT_FILENO);
    if(owned_write_fd<0 || dup2(STDERR_FILENO,STDOUT_FILENO)<0)return 3;
    int flags=fcntl(owned_write_fd,F_GETFL);
    if(flags<0 || fcntl(owned_write_fd,F_SETFL,flags|O_NONBLOCK))return 3;
    setvbuf(stdout,NULL,_IONBF,0);
    adapter=open("/dev/tater-athena-ap-venc",O_RDWR|O_CLOEXEC);
    struct ap_info info;uint32_t on=1;
    if(adapter<0 || ioctl(adapter,AP_INFO,&info) || info.version!=1 || info.engine!=0 || !info.mmio_enabled ||
       info.powered || ioctl(adapter,AP_POWER,&on))return 3;
    const int keep[]={adapter,owned_write_fd};
    if(close_unrelated(keep,sizeof keep/sizeof keep[0]))return 4;
    if(fence_options(adapter,1))return 4;
    watchdog(5);
    void *library=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"ATHENA_VENC_LOAD_ERROR %s\n",dlerror());return 5;}
    OMX_COMPONENTTYPE *(*factory)(OMX_STRING);
    void *symbol=dlsym(library,"MtkOmxComponentCreate");memcpy(&factory,&symbol,sizeof factory);
    if(!factory)return 6;
    OMX_COMPONENTTYPE *c=factory("OMX.MTK.VIDEO.ENCODER.AVC");
    if(!c || c->nSize<sizeof *c)return 7;
    static struct callback_state state={.mutex=PTHREAD_MUTEX_INITIALIZER,.completed=OMX_StateLoaded,.error=OMX_ErrorNone};
    state.component=c;
    OMX_CALLBACKTYPE callbacks={event_callback,empty_callback,fill_callback};
    REQUIRE(c->SetCallbacks(c,&callbacks,&state));
    OMX_PARAM_PORTDEFINITIONTYPE defs[2];
    for(unsigned p=0;p<2;p++) {
        init_header(&defs[p],sizeof defs[p]);defs[p].nPortIndex=p;
        REQUIRE(c->GetParameter(c,OMX_IndexParamPortDefinition,&defs[p]));
        defs[p].format.video.nFrameWidth=640;defs[p].format.video.nFrameHeight=480;
        defs[p].format.video.nStride=640;defs[p].format.video.nSliceHeight=480;
        defs[p].format.video.xFramerate=30U<<16;defs[p].format.video.nBitrate=1000000;
        if(!p) { defs[p].format.video.eColorFormat=OMX_COLOR_FormatYUV420SemiPlanar;defs[p].nBufferSize=640*480*3/2; }
        REQUIRE(c->SetParameter(c,OMX_IndexParamPortDefinition,&defs[p]));
        REQUIRE(c->GetParameter(c,OMX_IndexParamPortDefinition,&defs[p]));
        AP_TRACE("OMX_AP_PORT p=%u count=%u bytes=%u color=%u\n",p,defs[p].nBufferCountActual,defs[p].nBufferSize,defs[p].format.video.eColorFormat);
        if(!defs[p].nBufferCountActual || defs[p].nBufferCountActual>16 || defs[p].nBufferSize>8*1024*1024)return 8;
        if(!p && (defs[p].nBufferSize<640*480*3/2 || defs[p].format.video.nStride!=640 ||
           defs[p].format.video.nSliceHeight!=480 || defs[p].format.video.eColorFormat!=OMX_COLOR_FormatYUV420SemiPlanar))return 8;
    }
    OMX_VIDEO_PARAM_PORTFORMATTYPE input_format;init_header(&input_format,sizeof input_format);
    input_format.nPortIndex=0;input_format.nIndex=10;input_format.eCompressionFormat=OMX_VIDEO_CodingUnused;
    input_format.eColorFormat=OMX_COLOR_FormatYUV420SemiPlanar;input_format.xFramerate=30U<<16;
    REQUIRE(c->SetParameter(c,OMX_IndexParamVideoPortFormat,&input_format));
    OMX_VIDEO_PARAM_AVCTYPE avc;init_header(&avc,sizeof avc);avc.nPortIndex=1;
    REQUIRE(c->GetParameter(c,OMX_IndexParamVideoAvc,&avc));avc.eProfile=OMX_VIDEO_AVCProfileBaseline;avc.eLevel=OMX_VIDEO_AVCLevel31;avc.nPFrames=29;avc.nBFrames=0;
    REQUIRE(c->SetParameter(c,OMX_IndexParamVideoAvc,&avc));
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;init_header(&bitrate,sizeof bitrate);bitrate.nPortIndex=1;
    REQUIRE(c->GetParameter(c,OMX_IndexParamVideoBitrate,&bitrate));bitrate.eControlRate=OMX_Video_ControlRateVariable;bitrate.nTargetBitrate=1000000;
    REQUIRE(c->SetParameter(c,OMX_IndexParamVideoBitrate,&bitrate));
    REQUIRE(c->SendCommand(c,OMX_CommandStateSet,OMX_StateIdle,NULL));
    VAL_MEMORY_T memory[2][16];OMX_BUFFERHEADERTYPE *buffers[2][16];memset(memory,0,sizeof memory);memset(buffers,0,sizeof buffers);
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<defs[p].nBufferCountActual;i++) {
        VAL_MEMORY_T *m=&memory[p][i];m->eMemType=VAL_MEM_TYPE_FOR_HW_NONCACHEABLE;m->eMemCodec=VAL_MEM_CODEC_FOR_VENC;m->u4MemSize=defs[p].nBufferSize;m->eAlignment=VAL_MEM_ALIGN_4K;
        if(eVideoMemAlloc(m,sizeof *m)!=VAL_RESULT_NO_ERROR)_exit(11);
        REQUIRE(c->UseBuffer(c,&buffers[p][i],p,&state,defs[p].nBufferSize,m->pvMemVa));
        if(p) { state.outputs[i]=buffers[p][i];state.output_count=i+1; }
        else {state.inputs[i]=buffers[p][i];state.input_available[i]=1;state.input_count=i+1;}
    }
    if(wait_state(&state,OMX_StateIdle)) { puts("OMX_AP_IDLE_FAILED");_exit(12); }
    REQUIRE(c->SendCommand(c,OMX_CommandStateSet,OMX_StateExecuting,NULL));
    if(wait_state(&state,OMX_StateExecuting))_exit(18);
    unsigned char output_header[32]={0};memcpy(output_header,"TVE1",4);store32(output_header+4,32);
    store32(output_header+8,640);store32(output_header+12,480);memcpy(output_header+16,"H264",4);
    store32(output_header+20,30);store32(output_header+24,1);store32(output_header+28,4U*1024U*1024U);
    if(transfer(owned_write_fd,output_header,sizeof output_header,1))return 29;
    pthread_mutex_lock(&state.mutex);state.recycle=1;pthread_mutex_unlock(&state.mutex);
    for(unsigned i=0;i<defs[1].nBufferCountActual;i++) {
        pthread_mutex_lock(&state.mutex);state.output_available[i]=1;pthread_mutex_unlock(&state.mutex);
        REQUIRE(c->FillThisBuffer(c,buffers[1][i]));
    }
    uint64_t frames=0,previous_pts=0;
    for(;;) {
        watchdog(0);
        unsigned char record[16];if(transfer(0,record,sizeof record,0))return 36;
        unsigned bytes=load32(record),request_idr=load32(record+4);uint64_t pts=load64(record+8);
        if(!bytes) {if(request_idr || pts)return 37;break;}
        if(bytes!=640*480*3/2 || request_idr>1 || pts>=INT64_MAX || (frames && pts<=previous_pts))return 38;
        watchdog(5);
        unsigned slot=(unsigned)(frames%state.input_count),acquired=0;
        for(unsigned tries=0;tries<200;tries++) {
            pthread_mutex_lock(&state.mutex);
            if(state.error)_exit(25);
            if(state.input_available[slot]){state.input_available[slot]=0;acquired=1;}
            pthread_mutex_unlock(&state.mutex);if(acquired)break;usleep(1000);
        }
        if(!acquired)_exit(26);
        if(transfer(0,memory[0][slot].pvMemVa,bytes,0))return 39;
        if(request_idr) {
            OMX_CONFIG_INTRAREFRESHVOPTYPE refresh;init_header(&refresh,sizeof refresh);
            refresh.nPortIndex=1;refresh.IntraRefreshVOP=OMX_TRUE;
            REQUIRE(c->SetConfig(c,OMX_IndexConfigVideoIntraVOPRefresh,&refresh));
        }
        OMX_BUFFERHEADERTYPE *input=buffers[0][slot];input->nOffset=0;input->nFilledLen=bytes;
        input->nTimeStamp=(OMX_TICKS)pts;input->nFlags=OMX_BUFFERFLAG_ENDOFFRAME;
        __sync_synchronize();REQUIRE(c->EmptyThisBuffer(c,input));
        frames++;previous_pts=pts;
    }
    watchdog(5);
    {
        unsigned end_slot=frames%state.input_count,end_ready=0;
        for(unsigned tries=0;tries<200;tries++) {
            pthread_mutex_lock(&state.mutex);
            if(state.error)_exit(27);
            if(state.input_available[end_slot]){state.input_available[end_slot]=0;end_ready=1;}
            pthread_mutex_unlock(&state.mutex);if(end_ready)break;usleep(1000);
        }
        if(!end_ready)_exit(28);
        OMX_BUFFERHEADERTYPE *end=buffers[0][end_slot];end->nOffset=0;end->nFilledLen=0;
        end->nTimeStamp=(OMX_TICKS)(previous_pts+1);end->nFlags=OMX_BUFFERFLAG_EOS;
        REQUIRE(c->EmptyThisBuffer(c,end));
        unsigned ready=0;
        for(unsigned i=0;i<300;i++) {
            pthread_mutex_lock(&state.mutex);ready=state.eos;unsigned error=state.error;pthread_mutex_unlock(&state.mutex);
            if(error){puts("OMX_AP_FRAME_ERROR");_exit(19);}if(ready)break;usleep(10000);
        }
        pthread_mutex_lock(&state.mutex);
        state.recycle=0;
        if(!ready || !state.bytes)_exit(20);
        pthread_mutex_unlock(&state.mutex);
        REQUIRE(c->SendCommand(c,OMX_CommandStateSet,OMX_StateIdle,NULL));
        if(wait_state(&state,OMX_StateIdle))_exit(22);
    }
    REQUIRE(c->SendCommand(c,OMX_CommandStateSet,OMX_StateLoaded,NULL));
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<defs[p].nBufferCountActual;i++) {
        REQUIRE(c->FreeBuffer(c,p,buffers[p][i]));
        if(eVideoMemFree(&memory[p][i],sizeof memory[p][i])!=VAL_RESULT_NO_ERROR)_exit(13);
    }
    if(wait_state(&state,OMX_StateLoaded))_exit(14);
    REQUIRE(c->ComponentDeInit(c));
    unsigned live=0;for(unsigned i=0;i<64;i++)live+=slots[i].cpu!=NULL;
    fprintf(stderr,"ATHENA_VENC_FINISHED frames=%llu live_buffers=%u\n",(unsigned long long)frames,live);
    _exit(live?15:0);
}


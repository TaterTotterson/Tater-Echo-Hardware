/* SPDX-License-Identifier: MIT
 * Opt-in LAB wrapper. The pinned production encoder source/helper is unchanged.
 * Only balanced Cronos Bayer development is redirected; H.264/PTS/U-V handling
 * remain the existing qualified implementation. Missing/invalid mode = CPU.
 */
#define _POSIX_C_SOURCE 200809L
#include <sys/prctl.h>
#include <sys/wait.h>
#include "cronos_bayer10.h"
#include "../mt8163-gpu/bayer10_trial_protocol.h"

static int trial_convert(const uint8_t *,size_t,const uint8_t *,uint16_t *,uint8_t *,size_t);
#define cronos_bayer10_balanced_yuv420 trial_convert
#define main tater_qualified_venc_main
#include "cronos_venc_android.c"
#undef main
#undef cronos_bayer10_balanced_yuv420

static pid_t gpu_pid=-1;
static int gpu_input=-1,gpu_output=-1,gpu_failed=0;
static uint32_t gpu_sequence=0;

static void stop_gpu(void) {
    if(gpu_input>=0)close(gpu_input);
    if(gpu_output>=0)close(gpu_output);
    gpu_input=gpu_output=-1;
    if(gpu_pid>0) {
        kill(gpu_pid,SIGKILL);
        /* Child is a private GPU process only, never a camera service. */
        for(unsigned n=0;n<100;++n) {
            if(waitpid(gpu_pid,NULL,WNOHANG)==gpu_pid)break;
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        }
    }
    gpu_pid=-1;gpu_sequence=0;
}

static int start_gpu(void) {
    int in[2]={-1,-1},out[2]={-1,-1};
    long fd_limit=sysconf(_SC_OPEN_MAX);
    if(fd_limit<0||fd_limit>65536)return -1;
    if(pipe(in)||pipe(out))goto fail;
    for(unsigned n=0;n<2;++n)if(fcntl(in[n],F_SETFD,FD_CLOEXEC)||fcntl(out[n],F_SETFD,FD_CLOEXEC))goto fail;
    pid_t parent=getpid();gpu_pid=fork();
    if(gpu_pid<0)goto fail;
    if(gpu_pid==0) {
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent||
           dup2(in[0],STDIN_FILENO)<0||dup2(out[1],STDOUT_FILENO)<0)_exit(126);
        close(in[0]);close(in[1]);close(out[0]);close(out[1]);
        /* Do not inherit OMX/ION/device descriptors into the GPU runtime. */
        for(int fd=3;fd<fd_limit;++fd)close(fd);
        char *const args[]={TRIAL_GPU_HELPER,"--stream-raw10","--target","cronos",
            "--acknowledge-gpu-job","private-pbuffer-live-conversion","--bounded","run",NULL};
        char *const env[]={"PATH=/usr/bin:/bin","LD_LIBRARY_PATH=/data/tater-gpu/lib",
            "LD_PRELOAD=/data/tater-gpu/lib/libbionic28compat.so",NULL};
        execve(args[0],args,env);_exit(127);
    }
    close(in[0]);in[0]=-1;close(out[1]);out[1]=-1;
    gpu_input=in[1];gpu_output=out[0];
    if(trial_nonblock(gpu_input)||trial_nonblock(gpu_output))goto child_fail;
    uint8_t ready[16];
    if(trial_transfer(gpu_output,ready,sizeof(ready),0,trial_now_us()+5000000)!=1||
       memcmp(ready,"TGR0",4)||trial_load32(ready+4)!=TRIAL_OUTPUT_BYTES||
       trial_load32(ready+8)||trial_load32(ready+12))goto child_fail;
    return 0;
child_fail:
    stop_gpu();return -1;
fail:
    for(unsigned n=0;n<2;++n){if(in[n]>=0)close(in[n]);if(out[n]>=0)close(out[n]);}
    return -1;
}

static void record_metrics(int mode,int fallback,int64_t elapsed,uint32_t cpu,
                           uint32_t shader,uint32_t shader_cpu) {
    static unsigned frames=0,last_mode=2;
    static int64_t since=0,total=0,maximum=0,child_total=0,cpu_total=0,child_cpu_total=0;
    if(!since)since=trial_now_us();
    if((unsigned)mode!=last_mode){frames=0;total=maximum=child_total=cpu_total=child_cpu_total=0;since=trial_now_us();last_mode=(unsigned)mode;}
    ++frames;total+=elapsed;if(elapsed>maximum)maximum=elapsed;
    cpu_total+=cpu;child_total+=shader;child_cpu_total+=shader_cpu;
    if(!fallback&&frames!=1&&trial_now_us()-since<5000000)return;
    int fd=open(TRIAL_METRICS_PATH,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd>=0) {
        struct stat s;
        if(!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&077)&&s.st_size<131072)
            dprintf(fd,"TATER_GPU_TRIAL pid=%ld mode=%s frames=%u average_us=%lld max_us=%lld cpu_us=%lld gpu_us=%lld gpu_cpu_us=%lld fallback=%d pixels_saved=0\n",
                (long)getpid(),mode?"gpu":"cpu",frames,(long long)(total/frames),(long long)maximum,
                (long long)(cpu_total/frames),(long long)(child_total/frames),(long long)(child_cpu_total/frames),fallback);
        close(fd);
    }
    if(frames>1){frames=0;total=maximum=child_total=cpu_total=child_cpu_total=0;since=trial_now_us();}
}

static int trial_convert(const uint8_t *raw,size_t bytes,const uint8_t *luts,
                          uint16_t *scratch,uint8_t *output,size_t output_bytes) {
    if(!raw||!luts||!scratch||!output||bytes!=TRIAL_RAW_BYTES||output_bytes<TRIAL_OUTPUT_BYTES)return -1;
    int selected=trial_gpu_selected(TRIAL_MODE_PATH)&&!gpu_failed,fallback=0;
    int64_t started=trial_now_us();uint32_t cpu=trial_process_us(),shader=0,shader_cpu=0;
    if(selected) {
        uint8_t h[16]={0},reply[24];
        if(gpu_pid<0&&start_gpu())goto cpu_fallback;
        memcpy(h,"TGB1",4);trial_store32(h+4,TRIAL_INPUT_BYTES);trial_store32(h+8,gpu_sequence);
        int64_t deadline=trial_now_us()+2000000;
        if(trial_transfer(gpu_input,h,sizeof(h),1,deadline)!=1||
           trial_transfer(gpu_input,(void *)luts,6144,1,deadline)!=1||
           trial_transfer(gpu_input,(void *)raw,TRIAL_RAW_BYTES,1,deadline)!=1||
           trial_transfer(gpu_output,reply,sizeof(reply),0,deadline)!=1||
           !trial_response_valid(reply,gpu_sequence)||
           trial_transfer(gpu_output,output,TRIAL_OUTPUT_BYTES,0,deadline)!=1)goto cpu_fallback;
        ++gpu_sequence;shader=trial_load32(reply+12);shader_cpu=trial_load32(reply+16);
        record_metrics(1,0,trial_now_us()-started,trial_process_us()-cpu,shader,shader_cpu);return 0;
cpu_fallback:
        stop_gpu();gpu_failed=1;fallback=1;
    } else if(gpu_pid>0)stop_gpu();
    int rc=cronos_bayer10_balanced_yuv420(raw,bytes,luts,scratch,output,output_bytes);
    record_metrics(0,fallback,trial_now_us()-started,trial_process_us()-cpu,0,0);
    return rc;
}

int main(int argc,char **argv) {
    /* The wrapper cannot be used for non-Cronos or a different sensor profile. */
    return tater_qualified_venc_main(argc,argv);
}

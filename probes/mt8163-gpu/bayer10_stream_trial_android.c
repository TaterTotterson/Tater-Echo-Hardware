/* SPDX-License-Identifier: MIT
 * Explicit, temporary live-pixel conversion trial. Pixels are RAM/pipe only.
 * No camera/sensor node is opened; the qualified camera retains ownership.
 */
#define TATER_GPU_BAYER_ES3
#define main tater_generated_bayer_main
#include "bayer10_offscreen_android.c"
#undef main
#include <signal.h>
#include "bayer10_trial_protocol.h"

int main(int argc,char **argv) {
    struct gpu g={.diagnostic=stderr};struct stat node;int result=1;
    uint8_t *input=NULL,*output=NULL;
    setvbuf(stderr,NULL,_IONBF,0);signal(SIGPIPE,SIG_IGN);
    if(argc!=8||strcmp(argv[1],"--stream-raw10")||strcmp(argv[2],"--target")||
       strcmp(argv[3],"cronos")||strcmp(argv[4],"--acknowledge-gpu-job")||
       strcmp(argv[5],"private-pbuffer-live-conversion")||strcmp(argv[6],"--bounded")||strcmp(argv[7],"run"))return 2;
    if(!cronos_gpu_target_allowed()||stat("/dev/mali0",&node)||!S_ISCHR(node.st_mode))return 2;
    alarm(5);
    input=malloc(TRIAL_INPUT_BYTES);output=malloc(TRIAL_OUTPUT_BYTES);
    if(!input||!output||trial_nonblock(STDIN_FILENO)||trial_nonblock(STDOUT_FILENO)||initialize(&g))goto done;
    uint8_t ready[16]={0};memcpy(ready,"TGR0",4);trial_store32(ready+4,TRIAL_OUTPUT_BYTES);
    if(trial_transfer(STDOUT_FILENO,ready,sizeof(ready),1,trial_now_us()+1000000)!=1)goto done;
    alarm(0);
    int64_t expires=trial_now_us()+600000000;
    for(uint32_t sequence=0;sequence<18000&&trial_now_us()<expires;++sequence) {
        uint8_t request[16],response[24]={0};int64_t deadline=trial_now_us()+10000000;
        int rc=trial_transfer(STDIN_FILENO,request,sizeof(request),0,deadline);
        if(rc==0){result=0;goto done;}
        if(rc!=1||!trial_request_valid(request,sequence)||
           trial_transfer(STDIN_FILENO,input,TRIAL_INPUT_BYTES,0,deadline)!=1)goto done;
        alarm(2);
        int64_t started=trial_now_us();uint32_t cpu=trial_process_us();
        if(convert(&g,input+6144,input,NULL,output))goto done;
        uint32_t elapsed=(uint32_t)(trial_now_us()-started),cpu_elapsed=trial_process_us()-cpu;
        alarm(0);
        memcpy(response,"TGR1",4);trial_store32(response+4,TRIAL_OUTPUT_BYTES);
        trial_store32(response+8,sequence);trial_store32(response+12,elapsed);trial_store32(response+16,cpu_elapsed);
        deadline=trial_now_us()+2000000;
        if(trial_transfer(STDOUT_FILENO,response,sizeof(response),1,deadline)!=1||
           trial_transfer(STDOUT_FILENO,output,TRIAL_OUTPUT_BYTES,1,deadline)!=1)goto done;
    }
    result=0;
done:
    alarm(2);destroy(&g);free(input);free(output);
    fprintf(stderr,"TATER_GPU_STREAM_EXIT status=%d pixels_saved=0\n",result);
    return result;
}

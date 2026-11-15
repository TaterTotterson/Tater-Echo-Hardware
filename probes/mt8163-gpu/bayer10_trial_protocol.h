/* SPDX-License-Identifier: MIT
 * Private pipe protocol for a bounded Cronos GPU color-conversion trial.
 * No sensor API, network, arbitrary paths or variable-sized allocations.
 */
#ifndef TATER_BAYER_TRIAL_PROTOCOL_H
#define TATER_BAYER_TRIAL_PROTOCOL_H
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TRIAL_RAW_BYTES (1600U*1200U*5U/4U)
#define TRIAL_INPUT_BYTES (TRIAL_RAW_BYTES+6144U)
#define TRIAL_OUTPUT_BYTES (640U*480U*3U/2U)
#define TRIAL_DIRECTORY "/tmp/tater-cronos-gpu-live-v1"
#define TRIAL_GPU_HELPER TRIAL_DIRECTORY "/tater-gpu-bayer-stream"
#define TRIAL_MODE_PATH TRIAL_DIRECTORY "/mode"
#define TRIAL_METRICS_PATH TRIAL_DIRECTORY "/metrics.log"

static inline uint32_t trial_load32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void trial_store32(uint8_t *p,uint32_t value) {
    for(unsigned n=0;n<4;++n)p[n]=(uint8_t)(value>>(8*n));
}
static inline int64_t trial_now_us(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static inline uint32_t trial_process_us(void) {
    struct timespec t;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);
    return (uint32_t)((uint64_t)t.tv_sec*1000000+t.tv_nsec/1000);
}
/* Descriptors must be nonblocking; the absolute deadline spans all chunks.
 * EOF before any byte is distinct from a truncated record; never replay input.
 */
static inline int trial_transfer(int fd,void *buffer,size_t bytes,int writing,int64_t deadline) {
    uint8_t *p=buffer;size_t at=0;
    while(at<bytes) {
        int64_t left=deadline-trial_now_us();if(left<=0)return -1;
        struct pollfd ready={.fd=fd,.events=writing?POLLOUT:POLLIN};
        int rc=poll(&ready,1,(int)((left+999)/1000));
        if(rc<0&&errno==EINTR)continue;
        if(rc<=0||ready.revents&(POLLERR|POLLNVAL))return -1;
        ssize_t n=writing?write(fd,p+at,bytes-at):read(fd,p+at,bytes-at);
        if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
        if(n<=0)return !writing&&!n&&!at?0:-1;
        at+=(size_t)n;
    }
    return 1;
}
static inline int trial_nonblock(int fd) {
    int flags=fcntl(fd,F_GETFL);
    return flags<0?-1:fcntl(fd,F_SETFL,flags|O_NONBLOCK);
}
static inline int trial_gpu_selected(const char *path) {
    char value[5];struct stat info;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int safe=fstat(fd,&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==geteuid()&&
        !(info.st_mode&077)&&info.st_size==4;
    ssize_t n=safe?read(fd,value,sizeof(value)):-1;close(fd);
    return n==4&&!memcmp(value,"gpu\n",4);
}
static inline int trial_request_valid(const uint8_t *h,uint32_t sequence) {
    return !memcmp(h,"TGB1",4)&&trial_load32(h+4)==TRIAL_INPUT_BYTES&&
        trial_load32(h+8)==sequence&&trial_load32(h+12)==0;
}
static inline int trial_response_valid(const uint8_t *h,uint32_t sequence) {
    return !memcmp(h,"TGR1",4)&&trial_load32(h+4)==TRIAL_OUTPUT_BYTES&&
        trial_load32(h+8)==sequence&&trial_load32(h+12)<=2000000&&
        trial_load32(h+16)<=2000000&&trial_load32(h+20)==0;
}
#endif

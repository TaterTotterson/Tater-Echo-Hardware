/* Original bounded per-operation stream transport, no network/file access. */
#ifndef TATER_ATHENA_STREAM_IO_H
#define TATER_ATHENA_STREAM_IO_H
#include <poll.h>
#include <sys/time.h>
static void watchdog(unsigned seconds) {
    struct itimerval timer;memset(&timer,0,sizeof timer);timer.it_value.tv_sec=seconds;
    if(setitimer(ITIMER_REAL,&timer,NULL))_exit(45);
}
static uint32_t load32(const unsigned char *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t load64(const unsigned char *p) {return load32(p)|(uint64_t)load32(p+4)<<32;}
static void store32(unsigned char *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void store64(unsigned char *p,uint64_t n) {store32(p,(uint32_t)n);store32(p+4,(uint32_t)(n>>32));}
static int transfer(int fd,void *data,size_t bytes,int writing) {
    unsigned char *p=data;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    int64_t deadline=(int64_t)now.tv_sec*1000+now.tv_nsec/1000000+(writing?2000:10000);
    while(bytes) {
        clock_gettime(CLOCK_MONOTONIC,&now);
        int64_t remaining=deadline-(int64_t)now.tv_sec*1000-now.tv_nsec/1000000;
        if(remaining<=0)return -1;
        struct pollfd wait={fd,writing?POLLOUT:POLLIN,0};int result=poll(&wait,1,(int)remaining);
        if(result<0 && errno==EINTR)continue;if(result<=0)return -1;
        ssize_t n=writing?write(fd,p,bytes):read(fd,p,bytes);
        if(n<0 && errno==EINTR)continue;if(n<=0)return -1;
        p+=n;bytes-=(size_t)n;
    }
    return 0;
}
#endif

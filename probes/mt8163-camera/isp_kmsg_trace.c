/* SPDX-License-Identifier: MIT
 * Bounded, read-only camera/ISP kernel trace. Never opens a camera, changes
 * logging levels, clears the kernel ring, or writes to kernel interfaces.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int relevant(const char *line) {
    static const char *needles[]={"Camera-ISP","CMDQ","cmdq","M4U","m4u",
        "IMGI","ISP_Dump","BUG:","Oops:","Call trace:","watchdog: BUG"};
    for (unsigned n=0;n<sizeof(needles)/sizeof(needles[0]);++n)
        if (strstr(line,needles[n])) return 1;
    return 0;
}

static long long milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)) return -1;
    return (long long)now.tv_sec*1000+now.tv_nsec/1000000;
}

static int trace(unsigned duration_ms) {
    int fd=open("/dev/kmsg",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) { perror("open read-only kmsg"); return 1; }
    if (lseek(fd,0,SEEK_END)<0) { perror("seek kmsg end"); close(fd); return 1; }
    long long started=milliseconds();
    if (started<0) { close(fd); return 1; }
    unsigned emitted=0,overruns=0;
    puts("TATER_ISP_TRACE ready read_only=true future_messages_only=true"); fflush(stdout);
    int status=0;
    for (;;) {
        long long now=milliseconds();
        if (now<0) { status=1; break; }
        long long remaining=started+duration_ms-now;
        if (remaining<=0 || emitted>=128) break;
        struct pollfd watcher={fd,POLLIN,0};
        int event=poll(&watcher,1,(int)(remaining<100?remaining:100));
        if (event<0) { if (errno==EINTR) continue; status=1; break; }
        if (!event) continue;
        if (watcher.revents&(POLLNVAL|POLLHUP)) { status=1; break; }
        char line[8192];
        ssize_t size=read(fd,line,sizeof(line)-1);
        if (size<0) {
            if (errno==EAGAIN || errno==EINTR) continue;
            if (errno==EPIPE && ++overruns<16) continue;
            perror("read kmsg"); status=1; break;
        }
        if (!size) continue;
        line[size]='\0';
        char *message=strchr(line,';');
        if (!message || !relevant(message+1)) continue;
        /* Keep the kernel sequence/timestamp header. Cap each diagnostic so
         * an IRQ storm cannot flood the USB console or the host log. */
        if (size>1024) line[1024]='\0';
        printf("TATER_ISP_KMSG %s%s",line,strchr(line,'\n')?"":"\n"); fflush(stdout);
        ++emitted;
    }
    close(fd);
    printf("TATER_ISP_TRACE finished records=%u overruns=%u limit_reached=%s status=%d\n",
        emitted,overruns,emitted>=128?"true":"false",status); fflush(stdout);
    return status;
}

#ifndef TATER_ISP_TRACE_TEST
int main(int argc,char **argv) {
    if (argc!=4 || strcmp(argv[1],"--read-only-trace") || strcmp(argv[2],"--duration-ms")) {
        fputs("usage: isp-kmsg-trace --read-only-trace --duration-ms 100..15000\n",stderr); return 2;
    }
    errno=0; char *end=NULL; unsigned long duration=strtoul(argv[3],&end,10);
    if (errno || !*argv[3] || *end || duration<100 || duration>15000) return 2;
    alarm(20);
    return trace((unsigned)duration);
}
#endif

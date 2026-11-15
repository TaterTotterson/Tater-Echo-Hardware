/* Original fail-closed syscall boundary; owner libraries remain external. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "val_types_public.h"
#include "../../kernel-patches/athena-codec/ap/athena_ap.h"


static int trace_enabled;
int __android_log_print(int priority, const char *tag, const char *format, ...) {
    if(!trace_enabled && priority<6)return 0;
    fprintf(stderr,"AP_VENDOR_LOG %s: ",tag?tag:"");
    va_list args; va_start(args,format);
    int result=vfprintf(stderr,format,args); va_end(args);
    fputc('\n',stderr);
    return result;
}
int __android_log_write(int priority,const char *tag,const char *message) {
    return __android_log_print(priority,tag,"%s",message?message:"");
}
int __android_log_vprint(int priority,const char *tag,const char *format,va_list args) {
    if(!trace_enabled && priority<6)return 0;
    fprintf(stderr,"AP_VENDOR_LOG %s: ",tag?tag:"");
    int result=vfprintf(stderr,format,args); fputc('\n',stderr); return result;
}
__attribute__((noreturn)) void __android_log_assert(const char *condition,const char *tag,const char *format,...) {
    fprintf(stderr,"AP_VENDOR_ASSERT %s: %s ",tag?tag:"",condition?condition:"");
    if(format) { va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args); }
    fputc('\n',stderr); _exit(22);
}

static int target_ok(void) {
    struct utsname u;
    char bytes[512];
    int fd = open("/proc/device-tree/compatible", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    ssize_t n = read(fd, bytes, sizeof bytes); close(fd);
    return n > 0 && memmem(bytes, n, "amzn,athena\0", 12) &&
        uname(&u) == 0 && !strcmp(u.release, "6.18.56-techo5-athena") &&
        !strcmp(u.version, "#1 SMP PREEMPT 2026-11-14 10:00:00 UTC");
}
static int owned_write_fd=-1;
/* Before vendor threads/load: close every inherited descriptor, not merely
 * the first 1024. Threaded logging/pipes must never reach an inherited socket. */
static int close_unrelated(const int *keep,unsigned count) {
    DIR *directory=opendir("/proc/self/fd");if(!directory)return -1;
    int enumeration_fd=dirfd(directory),result=0;
    struct dirent *entry;
    for(;;) {
        errno=0;entry=readdir(directory);
        if(!entry){if(errno)result=-1;break;}
        char *end=NULL;long value=strtol(entry->d_name,&end,10);
        if(!end || *end || value<3 || value>INT32_MAX || value==enumeration_fd)continue;
        unsigned owned=0;for(unsigned i=0;i<count;i++)owned|=keep[i]==value;
        if(!owned && close((int)value) && errno!=EBADF){result=-1;break;}
    }
    if(closedir(directory))result=-1;
    return result;
}
static int fence_options(int adapter_fd,int threads) {
    /* Applied before owner runtime loading. No writable opens, network,
     * exec, module operations or unknown ioctls; threads only if requested. */
    const unsigned denied = SECCOMP_RET_ERRNO | EPERM;
    const unsigned write_flags = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
    const unsigned thread_required=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM;
    const unsigned thread_allowed=thread_required|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID;
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 9),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K, ~(threads?thread_allowed:0U)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K, thread_required),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, thread_required, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ioctl, 0, 11),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)adapter_fd, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AP_INFO, 5, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AP_ALLOC, 4, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AP_WAIT, 3, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AP_CLEAR, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AP_DCM, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_open, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, write_flags, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, write_flags, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_write, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_writev, 0, 6),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1, 3, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 2, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)owned_write_fd, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, threads?SECCOMP_RET_ALLOW:denied),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        /* Only this process's wall-clock watchdog; no profiling timers. */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setitimer, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, denied),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
#define ALLOW(number) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
        ALLOW(__NR_read), ALLOW(__NR_pread64), ALLOW(__NR_close), ALLOW(__NR_dup),
        ALLOW(__NR_fstat64), ALLOW(__NR_fstatat64), ALLOW(__NR_stat64),
        ALLOW(__NR_fstatfs64), ALLOW(__NR_statfs64),
        ALLOW(__NR_access), ALLOW(__NR_faccessat), ALLOW(__NR_readlink),
        ALLOW(__NR_readlinkat), ALLOW(__NR_lseek), ALLOW(__NR__llseek),
        ALLOW(__NR_mmap2), ALLOW(__NR_mprotect), ALLOW(__NR_munmap),
        ALLOW(__NR_madvise), ALLOW(__NR_brk), ALLOW(__NR_mremap),
        ALLOW(__NR_futex), ALLOW(__NR_gettid), ALLOW(__NR_getpid),
        ALLOW(__NR_getuid32), ALLOW(__NR_geteuid32), ALLOW(__NR_getgid32),
        ALLOW(__NR_getegid32), ALLOW(__NR_clock_gettime), ALLOW(__NR_gettimeofday),
        ALLOW(__NR_nanosleep), ALLOW(__NR_sched_yield), ALLOW(__NR_getrandom),
        ALLOW(__NR_rt_sigaction), ALLOW(__NR_rt_sigprocmask),
        ALLOW(__NR_rt_sigreturn), ALLOW(__NR_sigaltstack), ALLOW(__NR_exit),
        ALLOW(__NR_exit_group), ALLOW(__ARM_NR_cacheflush), ALLOW(__ARM_NR_set_tls),
        ALLOW(__NR_set_robust_list), ALLOW(__NR_set_tid_address),
        ALLOW(__NR_poll), ALLOW(__NR_ppoll), ALLOW(__NR_pselect6), ALLOW(__NR__newselect),
#undef ALLOW
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_pipe, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, threads?SECCOMP_RET_ALLOW:denied),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_pipe2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, threads?SECCOMP_RET_ALLOW:denied),
        BPF_STMT(BPF_RET | BPF_K, denied),
    };
    struct sock_fprog program = {sizeof(code)/sizeof(code[0]), code};
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
           prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}
static inline int fence(int adapter_fd) { return fence_options(adapter_fd,0); }


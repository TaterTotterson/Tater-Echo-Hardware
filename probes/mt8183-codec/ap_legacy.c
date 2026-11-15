/* Original owned VAL / legacy ABI translation, not vendor implementation. */
#include "ap_sandbox.h"
#define AP_TRACE(...) do { if(trace_enabled) fprintf(stderr,__VA_ARGS__); } while(0)
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>
#include "val_types_private.h"
#include "venc_drv_if_public.h"
_Static_assert(sizeof(VAL_MEMORY_T)==96,"Athena VAL memory ABI");

static int adapter=-1;
/* Decoder memory-only inspection selects its own IOMMU device and codec.
 * Encoder behavior remains the default; no decoder register access follows. */
static VAL_MEM_CODEC_T owned_codec=VAL_MEM_CODEC_FOR_VENC;
static int legacy_enabled=1;
static VAL_DRIVER_TYPE_T owned_driver=VAL_DRIVER_TYPE_H264_ENC;
static unsigned char legacy[1024];
static unsigned emi_users,power_users;
static int register_mode;
static uintptr_t locked_owner;
struct register_mapping {uintptr_t va;uint32_t physical;};
static struct register_mapping observed_maps[32];
static unsigned observed_map_count;
static void (*irq_failure_reporter)(void);
static void *observe_map(void *address,uint32_t physical) {
    if(address!=MAP_FAILED && observed_map_count<32)
        observed_maps[observed_map_count++]=(struct register_mapping){(uintptr_t)address,physical};
    return address;
}
static pthread_mutex_t hardware_mutex=PTHREAD_MUTEX_INITIALIZER;
void *mmap64(void *address,size_t length,int prot,int flags,int fd,off64_t offset) {
    if(fd>=0 && fd<1024 && legacy[fd]) {
        AP_TRACE("AP_LEGACY_MMAP bytes=%zu offset=%#lx prot=%d flags=%d\n",
            length,(unsigned long)offset,prot,flags);
        if(owned_codec==VAL_MEM_CODEC_FOR_VDEC && register_mode &&
           prot==(PROT_READ|PROT_WRITE) && flags==MAP_SHARED && !address) {
            struct {uint32_t physical,bytes;} const allowed[]={{0x16000000,256},
                {0x16020000,512},{0x16021000,1024},{0x16021000,4096},
                {0x16022000,4096},{0x16023000,4096},{0x16024000,768},
                {0x16024000,4096},{0x16025000,4096},{0x16026000,4096},
                {0x16027000,4096},{0x16028000,4096}};
            for(unsigned i=0;i<sizeof allowed/sizeof allowed[0];i++)
                if(offset==allowed[i].physical && length==allowed[i].bytes) {
                    /* Partial VP8/HEVC banks are NOT used by H264. Keep
                     * inaccessible guard pages, never a fake register bank. */
                    if(offset==0x16026000 || offset==0x16028000)
                        return observe_map((void *)syscall(__NR_mmap2,NULL,4096,PROT_NONE,
                            MAP_PRIVATE|MAP_ANONYMOUS,-1,0),(uint32_t)offset);
                    int permissions=offset==0x16000000?PROT_READ:prot;
                    return observe_map((void *)syscall(__NR_mmap2,NULL,4096,permissions,
                        MAP_SHARED,adapter,(uint32_t)offset>>12),(uint32_t)offset);
                }
            errno=EPERM;return MAP_FAILED;
        }
        if(!register_mode || offset!=0x17020000 || length!=4096 ||
           prot!=(PROT_READ|PROT_WRITE) || flags!=MAP_SHARED || address) {
            errno=EPERM; return MAP_FAILED;
        }
        return (void *)syscall(__NR_mmap2,NULL,4096,prot,MAP_SHARED,adapter,0x17020);
    }
    if(offset<0 || offset&4095 || (uint64_t)offset>>12>UINT32_MAX) { errno=EINVAL; return MAP_FAILED; }
    return (void *)syscall(__NR_mmap2,address,length,prot,flags,fd,(uint32_t)((uint64_t)offset>>12));
}
void *mmap(void *address,size_t length,int prot,int flags,int fd,off_t offset) {
    return mmap64(address,length,prot,flags,fd,offset);
}
int open(const char *path,int flags,...) {
    if(adapter>=0 && legacy_enabled && !strcmp(path,"/dev/Vcodec") && flags==(O_RDWR)) {
        int fd=(int)syscall(__NR_dup,adapter);
        if(fd>=0 && fd<1024) { legacy[fd]=1; AP_TRACE("AP_LEGACY_OPEN_TRANSLATED\n"); return fd; }
        if(fd>=0) syscall(__NR_close,fd);
        errno=EMFILE; return -1;
    }
    unsigned mode=0;
    if(flags&O_CREAT) { va_list args; va_start(args,flags); mode=va_arg(args,unsigned); va_end(args); }
    return (int)syscall(__NR_openat,AT_FDCWD,path,flags,mode);
}
int __open_2(const char *path,int flags) { return open(path,flags); }
int close(int fd) {
    if(fd>=0 && fd<1024 && legacy[fd]) { legacy[fd]=0; AP_TRACE("AP_LEGACY_CLOSE\n"); }
    return (int)syscall(__NR_close,fd);
}
int ioctl(int fd,int request,...) {
    va_list args; va_start(args,request); void *arg=va_arg(args,void *); va_end(args);
    unsigned command=(unsigned)request;
    if(fd<0 || fd>=1024 || !legacy[fd]) return (int)syscall(__NR_ioctl,fd,command,arg);
    AP_TRACE("AP_LEGACY_IOCTL command=%#x\n",command);
    unsigned emi_increment=owned_codec==VAL_MEM_CODEC_FOR_VDEC?0x17:0x15;
    unsigned emi_decrement=owned_codec==VAL_MEM_CODEC_FOR_VDEC?0x18:0x16;
    if(command==_IOW('M',emi_increment,unsigned)) { if(!arg)return -1; *(unsigned *)arg=++emi_users; return 255; }
    if(command==_IOW('M',emi_decrement,unsigned)) {
        if(!arg || !emi_users) { errno=EINVAL; return -1; }
        *(unsigned *)arg=--emi_users; return 255;
    }
    if(command==_IOW('M',0x27,unsigned) || command==_IOW('M',0x28,unsigned)) {
        VAL_POWER_T *power=arg;
        if(!power || power->eDriverType!=owned_driver) { errno=EINVAL; return -1; }
        if(command==_IOW('M',0x27,unsigned)) power_users++;
        else if(power_users) power_users--;
        else { errno=EINVAL; return -1; }
        return 255; /* Stock no-op L2 user accounting, not real power. */
    }
    if(command==_IOW('M',0x24,unsigned) || command==_IOW('M',0x20,unsigned) ||
       command==_IOW('M',0x21,unsigned)) return 255; /* Stock explicitly empty. */
    if(command==_IOW('M',0x35,unsigned)) return arg ? 255 : -1; /* Process-local logging only. */
    if(command==_IOW('M',0x34,unsigned)) { __sync_synchronize();return 255; }
    if(command==_IOW('M',0x0d,unsigned) || command==_IOW('M',0x33,unsigned)) {
        VAL_HW_LOCK_T *lock=arg;
        if(!register_mode || !lock || !lock->pvHandle || lock->bSecureInst ||
           lock->eDriverType!=owned_driver || lock->u4HandleSize>4096) {
            errno=EINVAL;return -1;
        }
        pthread_mutex_lock(&hardware_mutex);
        int result=255;
        if(command==_IOW('M',0x0d,unsigned)) {
            if(locked_owner) { errno=EBUSY;result=-1; }
            else if(syscall(__NR_ioctl,adapter,AP_CLEAR,NULL))result=-1;
            else locked_owner=(uintptr_t)lock->pvHandle;
        } else if(locked_owner!=(uintptr_t)lock->pvHandle) { errno=EPERM;result=-1; }
        else locked_owner=0;
        pthread_mutex_unlock(&hardware_mutex);return result;
    }
    if(command==_IOW('M',0x0b,unsigned)) {
        VAL_ISR_T *isr=arg;
        if(isr)AP_TRACE("AP_ISR_CONTRACT driver=%u expected=%u status_count=%u timeout=%u\n",
            isr->eDriverType,owned_driver,isr->u4IrqStatusNum,isr->u4TimeoutMs);
        if(!register_mode || !isr || !isr->pvHandle ||
           isr->eDriverType!=owned_driver ||
           (owned_codec==VAL_MEM_CODEC_FOR_VENC && isr->u4IrqStatusNum>16)) {
            errno=EINVAL;return -1;
        }
        pthread_mutex_lock(&hardware_mutex);
        int owned=locked_owner==(uintptr_t)isr->pvHandle;
        pthread_mutex_unlock(&hardware_mutex);
        if(!owned) { errno=EPERM;if(irq_failure_reporter)irq_failure_reporter();return -1; }
        unsigned timeout=isr->u4TimeoutMs;
        if(!timeout || timeout>1000)timeout=1000;
        struct ap_wait wait={timeout,0};
        int result=(int)syscall(__NR_ioctl,adapter,AP_WAIT,&wait);
        AP_TRACE("AP_ISR_WAIT_RESULT result=%d errno=%d status=%#x\n",result,result?errno:0,wait.status);
        if(result){if(irq_failure_reporter)irq_failure_reporter();return -1;}
        /* Exact GPL vcodec_waitisr uses count/output registers ONLY for VENC.
         * VDEC callers legitimately leave that irrelevant tail uninitialized. */
        if(owned_codec==VAL_MEM_CODEC_FOR_VENC && isr->u4IrqStatusNum)isr->u4IrqStatus[0]=wait.status;
        AP_TRACE("AP_HARDWARE_IRQ status=%#x\n",wait.status);return 255;
    }
    errno=ENOTTY; return -1;
}
__attribute__((noreturn)) void exit(int code) {
    Dl_info info;
    void *caller=__builtin_return_address(0);
    if(dladdr(caller,&info))
        fprintf(stderr,"AP_VENDOR_EXIT code=%d library=%s symbol=%s offset=%lu\n",code,
            info.dli_fname?info.dli_fname:"",info.dli_sname?info.dli_sname:"",
            (unsigned long)((uintptr_t)caller-(uintptr_t)info.dli_fbase));
    _exit(code);
}
void VCodecDrvAssertFail(char *file,int line,int argument) {
    fprintf(stderr,"AP_PUBLIC_ASSERT file=%s line=%d argument=%d\n",file?file:"",line,argument);
    _exit(23); /* Preserve fail-closed assertion, never skip it. */
}
static struct allocation { void *cpu; size_t bytes; int fd; uint32_t dma; } slots[64];
static pthread_mutex_t allocation_mutex=PTHREAD_MUTEX_INITIALIZER;
static VAL_RESULT_T allocate_impl(VAL_MEMORY_T *memory, unsigned size) {
    if (!memory || size!=sizeof *memory || !memory->u4MemSize ||
        memory->u4MemSize>8*1024*1024 || memory->eMemCodec!=owned_codec)
        return VAL_RESULT_INVALID_PARAMETER;
    unsigned slot;
    for(slot=0;slot<64 && slots[slot].cpu;slot++) {}
    if(slot==64) return VAL_RESULT_INVALID_MEMORY;
    struct allocation *out=&slots[slot];
    out->fd=-1;
    if (memory->eMemType==VAL_MEM_TYPE_FOR_SW) {
        size_t alignment=memory->eAlignment;
        if (!alignment || alignment>8192 || (alignment&(alignment-1))) return VAL_RESULT_INVALID_PARAMETER;
        if(alignment<sizeof(void *)) alignment=sizeof(void *);
        if(posix_memalign(&out->cpu,alignment,memory->u4MemSize)) return VAL_RESULT_INVALID_MEMORY;
        out->bytes=memory->u4MemSize; out->dma=0;
        memset(out->cpu,0,out->bytes);
    } else if(memory->eMemType>=VAL_MEM_TYPE_FOR_HW_CACHEABLE &&
              memory->eMemType<=VAL_MEM_TYPE_FOR_HW_NONCACHEABLE) {
        if(memory->eAlignment>4096) return VAL_RESULT_INVALID_PARAMETER;
        struct ap_alloc request={.bytes=memory->u4MemSize,.fd=-1};
        if(ioctl(adapter,AP_ALLOC,&request)) return VAL_RESULT_INVALID_MEMORY;
        out->bytes=request.bytes; out->fd=request.fd; out->dma=(uint32_t)request.dma;
        out->cpu=mmap(NULL,out->bytes,PROT_READ|PROT_WRITE,MAP_SHARED,out->fd,0);
        if(out->cpu==MAP_FAILED) { close(out->fd); memset(out,0,sizeof *out); return VAL_RESULT_INVALID_MEMORY; }
    } else return VAL_RESULT_INVALID_PARAMETER;
    memory->pvMemVa_ext64=memory->pvAlignMemVa_ext64=0;
    memory->pvMemPa_ext64=memory->pvAlignMemPa_ext64=0;
    memory->pvMemVa=memory->pvAlignMemVa=out->cpu;
    memory->pvMemPa=memory->pvAlignMemPa=(void *)(uintptr_t)out->dma;
    memory->pIonBufhandle_ext64=0; memory->pIonBufhandle=(void *)out;
    memory->i4IonShareFd=out->fd;
    memory->i4IonDevFd=(uint32_t)-1;
    AP_TRACE("AP_OWNED_ALLOCATION slot=%u bytes=%zu type=%u\n",slot,out->bytes,memory->eMemType);
    return VAL_RESULT_NO_ERROR;
}
static VAL_RESULT_T free_impl(VAL_MEMORY_T *memory, unsigned size) {
    if(!memory || size!=sizeof *memory) return VAL_RESULT_INVALID_PARAMETER;
    for(unsigned i=0;i<64;i++) if(slots[i].cpu && slots[i].cpu==memory->pvMemVa) {
        struct allocation *out=&slots[i];
        if(out->fd>=0) { munmap(out->cpu,out->bytes); close(out->fd); }
        else free(out->cpu);
        memset(out,0,sizeof *out);
        memory->pvMemVa_ext64=memory->pvAlignMemVa_ext64=0;
        memory->pvMemPa_ext64=memory->pvAlignMemPa_ext64=0;
        memory->pIonBufhandle_ext64=0;
        AP_TRACE("AP_OWNED_FREE slot=%u\n",i);
        return VAL_RESULT_NO_ERROR;
    }
    return VAL_RESULT_INVALID_MEMORY;
}
VAL_RESULT_T eVideoMemAlloc(VAL_MEMORY_T *memory,unsigned size) {
    pthread_mutex_lock(&allocation_mutex);
    VAL_RESULT_T result=allocate_impl(memory,size);
    pthread_mutex_unlock(&allocation_mutex);return result;
}
VAL_RESULT_T eVideoMemFree(VAL_MEMORY_T *memory,unsigned size) {
    pthread_mutex_lock(&allocation_mutex);
    VAL_RESULT_T result=free_impl(memory,size);
    pthread_mutex_unlock(&allocation_mutex);return result;
}
unsigned eVideoInitMVA(void **handle) { if(!handle)return (unsigned)-1;*handle=&adapter;return 0; }
unsigned eVideoDeInitMVA(void *handle) { return handle==&adapter ? 0 : (unsigned)-1; }
static unsigned owned_mva(uint32_t va,uint32_t bytes,uint32_t *dma) {
    for(unsigned i=0;i<64;i++) if(slots[i].cpu && slots[i].fd>=0) {
        uintptr_t start=(uintptr_t)slots[i].cpu;
        if(va>=start && (uint64_t)va+bytes<=(uint64_t)start+slots[i].bytes) {
            *dma=slots[i].dma+(va-start);return 0;
        }
    }
    return (unsigned)-1;
}
unsigned eVideoAllocMVA(void *handle,unsigned va,unsigned *dma,unsigned bytes,VAL_VCODEC_M4U_BUFFER_CONFIG_T *config) {
    if(handle!=&adapter || !dma || !bytes || !config || config->security ||
       config->eMemCodec!=owned_codec) return (unsigned)-1;
    pthread_mutex_lock(&allocation_mutex);
    unsigned result=owned_mva(va,bytes,dma);
    pthread_mutex_unlock(&allocation_mutex);
    AP_TRACE("AP_OWNED_MVA_MAP bytes=%u success=%u\n",bytes,!result);
    return result;
}
unsigned eVideoFreeMVA(void *handle,unsigned va,unsigned dma,unsigned bytes,VAL_VCODEC_M4U_BUFFER_CONFIG_T *config) {
    unsigned expected=0;
    unsigned result=eVideoAllocMVA(handle,va,&expected,bytes,config);
    return result || expected!=dma ? (unsigned)-1 : 0;
}
static VAL_RESULT_T coherent_cache(VAL_MEMORY_T *memory,unsigned size,unsigned operation) {
    AP_TRACE("AP_COHERENT_CACHE abi=%u operation=%u memory_present=%u\n",size,operation,memory!=NULL);
    if(operation>1)return VAL_RESULT_INVALID_PARAMETER;
    /* Published optype0 is global cache maintenance, not a requested range.
     * Every engine-visible allocation in this process is DMA-API coherent. */
    if(operation==0) {
        if((memory && size!=sizeof *memory) || (!memory && size))return VAL_RESULT_INVALID_PARAMETER;
        __sync_synchronize();return VAL_RESULT_NO_ERROR;
    }
    if(!memory || size!=sizeof *memory)return VAL_RESULT_INVALID_PARAMETER;
    uint32_t ignored=0;
    pthread_mutex_lock(&allocation_mutex);
    unsigned result=owned_mva((uint32_t)(uintptr_t)memory->pvMemVa,memory->u4MemSize,&ignored);
    pthread_mutex_unlock(&allocation_mutex);
    if(result)return VAL_RESULT_INVALID_MEMORY;
    __sync_synchronize();return VAL_RESULT_NO_ERROR;
}
VAL_RESULT_T eVideoFlushCache(VAL_MEMORY_T *memory,unsigned size,unsigned operation) { return coherent_cache(memory,size,operation); }
VAL_RESULT_T eVideoInvalidateCache(VAL_MEMORY_T *memory,unsigned size,unsigned operation) { return coherent_cache(memory,size,operation); }


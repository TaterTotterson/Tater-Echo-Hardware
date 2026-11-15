/* SPDX-License-Identifier: MIT
 * Experimental Cronos ABI adapter. Generated Bayer input only, one bounded
 * 128x128 memory job. No sensor/framebuffer node is opened and no pixel is
 * saved. Production camera integration is deliberately separate.
 * Hardware execution is disabled by default: the current runtime job faults
 * IMGI and has been followed by device lockups. See the findings document.
 */
#define main tater_isp_query_main
#include "isp_runtime_probe_android.c"
#undef main
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <ucontext.h>

#define W 128U
#define H 128U
#define PORT_BYTES 0x1acU
#ifndef TATER_ISP_UNSAFE_LAB_JOB
#define TATER_ISP_UNSAFE_LAB_JOB 0
#endif

struct ion_allocation { uint32_t length, alignment, heaps, flags; int32_t handle; };
struct ion_shared { int32_t handle, fd; };
struct ion_custom { uint32_t command, argument; };
struct ion_configuration { uint32_t command; int32_t handle; uint32_t port, security, coherent, start, end; uint8_t spare[64]; };
struct ion_physical { uint32_t command; int32_t handle; uint32_t address, length; uint8_t spare[128]; };
struct memory { int ion, fd, allocated; int32_t handle; uint32_t device; size_t size; uint8_t *pixels; };
struct vector_view { const void **begin, **end, **capacity; };
struct port_id { uint32_t key, group, capability, fps; };
_Static_assert(sizeof(void *)==4,"this probe requires the ARM32 owner ABI");
_Static_assert(sizeof(struct port_id)==16,"PortID must occupy four words");
_Static_assert(sizeof(struct vector_view)==12,"ARM32 vector view mismatch");
_Static_assert(sizeof(struct ion_allocation)==20,"ION allocation ABI mismatch");

/* Numeric-only, allocation-free crash diagnostics for this experimental ABI. */
static void probe_fault(int sig,siginfo_t *info,void *context) {
    (void)info;
    ucontext_t *u=context;
    static const char digits[]="0123456789abcdef";
    char line[]="TATER_ISP_FAULT pc=00000000 lr=00000000\n";
    uint32_t values[2]={u->uc_mcontext.arm_pc,u->uc_mcontext.arm_lr};
    const unsigned offsets[2]={19,31};
    for (unsigned n=0;n<2;++n) for (unsigned d=0;d<8;++d)
        line[offsets[n]+d]=digits[(values[n]>>((7-d)*4))&15];
    (void)write(STDERR_FILENO,line,sizeof(line)-1);
    _exit(128+sig);
}

static void runtime_maps(void) {
    FILE *maps=fopen("/proc/self/maps","r");
    if (!maps) return;
    char line[512];
    while (fgets(line,sizeof(line),maps))
        if (strstr(line,"r-xp") && (strstr(line,"libimageio") ||
            strstr(line,"libcamdrv") || strstr(line,"libdpframework") || strstr(line,"libc.so")))
            fprintf(stderr,"TATER_ISP_MAP %s",line);
    fclose(maps);
}

/* Read only the prepared command buffer, never the hardware register window.
 * Require the whole inspected range inside a readable/writable CPU mapping.
 * Reject known physical-register mappings even if the runtime returns one. */
static int shadow_mapping_allowed(const void *address) {
    uintptr_t start=(uintptr_t)address;
    if (!start || start>UINTPTR_MAX-0x4244U) return 0;
    FILE *maps=fopen("/proc/self/maps","r");
    if (!maps) return 0;
    char line[512],permissions[5]; unsigned long low,high;
    int allowed=0;
    while (fgets(line,sizeof(line),maps)) {
        if (sscanf(line,"%lx-%lx %4s",&low,&high,permissions)!=3) continue;
        if (start>=low && start+0x4244U<=high && permissions[0]=='r' && permissions[1]=='w' &&
            !strstr(line,"/dev/camera-isp") && !strstr(line,"/dev/mem")) { allowed=1; break; }
    }
    fclose(maps);
    return allowed;
}

/* Probe-local Android log sink. Keep these diagnostics on stderr instead of
 * requiring Android's log service or changing its logging configuration. */
int __android_log_print(int priority,const char *tag,const char *format,...) {
    char detail[1536];
    va_list args; va_start(args,format);
    int count=vsnprintf(detail,sizeof(detail),format,args); va_end(args);
    fprintf(stderr,"TATER_ISP_VENDOR priority=%d tag=%s %s\n",priority,tag?tag:"",detail);
    return count;
}

int __android_log_write(int priority,const char *tag,const char *message) {
    return __android_log_print(priority,tag,"%s",message?message:"");
}

static void memory_close(struct memory *m) {
    if (m->pixels && m->pixels != MAP_FAILED) munmap(m->pixels,m->size);
    if (m->fd >= 0) close(m->fd);
    if (m->allocated) (void)ioctl(m->ion,_IOWR('I',1,int32_t),&m->handle);
    if (m->ion >= 0) close(m->ion);
    memset(m,0,sizeof(*m)); m->ion=m->fd=-1;
}

static int memory_open(struct memory *m, size_t size, uint32_t port) {
    memset(m,0,sizeof(*m)); m->ion=m->fd=-1; m->size=size;
    m->ion=open("/dev/ion",O_RDWR|O_CLOEXEC);
    if (m->ion < 0) return -1;
    struct ion_allocation allocation={(uint32_t)size,4096,1U<<10,0,0};
    if (ioctl(m->ion,_IOWR('I',0,struct ion_allocation),&allocation)) return -1;
    m->handle=allocation.handle; m->allocated=1;
    struct ion_shared shared={m->handle,-1};
    if (ioctl(m->ion,_IOWR('I',4,struct ion_shared),&shared)) return -1;
    m->fd=shared.fd;
    m->pixels=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,m->fd,0);
    if (m->pixels == MAP_FAILED) return -1;
    struct ion_configuration configuration={0};
    configuration.handle=m->handle; configuration.port=port;
    struct ion_custom custom={1,(uint32_t)(uintptr_t)&configuration};
    if (ioctl(m->ion,_IOWR('I',6,struct ion_custom),&custom)) return -1;
    struct ion_physical physical={0}; physical.command=1; physical.handle=m->handle;
    custom.command=0; custom.argument=(uint32_t)(uintptr_t)&physical;
    if (ioctl(m->ion,_IOWR('I',6,struct ion_custom),&custom) ||
        !physical.address || physical.length < size) return -1;
    m->device=physical.address;
    return 0;
}

/* Only verified ARM32 ABI fields are described here. The remaining bytes
 * are zero and all optional vectors/tuning/secure modes are disabled. */
static void port(uint32_t words[PORT_BYTES/4],struct memory *m,int output) {
    memset(words,0,PORT_BYTES);
    words[0]=output ? 0x2008U : 0x2201U;
    words[0x20/4]=0; /* red-first Bayer for generated input */
    words[0x2c/4]=W; words[0x30/4]=H;
    words[0x38/4]=output ? W : W*5/4;
    words[0x3c/4]=output ? W/2 : 0;
    words[0x40/4]=output ? W/2 : 0;
    words[0x54/4]=W; words[0x58/4]=H;
    /* The memory-input MDP crop is distinct from the DMA crop above.
     * Its x/y/fractional coordinates remain zero for the complete frame. */
    words[0x8c/4]=W; words[0x90/4]=H;
    words[0xec/4]=output ? 0x10a00 : 0x200; /* memory DISPO / IMGI */
    words[0xf0/4]=0; words[0xf8/4]=30;
    uint32_t first=output ? W*H : W*H*5/4;
    words[0xfc/4]=first;
    words[0x108/4]=(uint32_t)(uintptr_t)m->pixels;
    words[0x114/4]=m->device;
    if (output) {
        words[0x100/4]=words[0x104/4]=W*H/4;
        words[0x10c/4]=(uint32_t)(uintptr_t)(m->pixels+W*H);
        words[0x110/4]=(uint32_t)(uintptr_t)(m->pixels+W*H*5/4);
        words[0x118/4]=m->device+W*H;
        words[0x11c/4]=m->device+W*H*5/4;
    }
    words[0x120/4]=(uint32_t)m->fd;
}

static void generated_bayer(uint8_t *raw) {
    for (unsigned y=0;y<H;++y) for (unsigned x=0;x<W;x+=4) {
        unsigned values[4];
        for (unsigned n=0;n<4;++n) {
            unsigned channel=(y&1) ? ((x+n)&1 ? 2:1) : ((x+n)&1 ? 1:0);
            unsigned band=(x+n)/(W/4);
            values[n]=band==3 ? 600 : (channel==band ? 800:64);
        }
        unsigned at=y*(W*5/4)+x*5/4;
        for (unsigned n=0;n<5;++n) raw[at+n]=0;
        for (unsigned n=0;n<4;++n) {
            unsigned shift=n*10, byte=shift/8, bit=shift%8;
            raw[at+byte]|=(uint8_t)(values[n]<<bit);
            raw[at+byte+1]|=(uint8_t)(values[n]>>(8-bit));
        }
    }
}

#define RESOLVE(VAR, NAME) do { void *s=dlsym(library,NAME); memcpy(&(VAR),&s,sizeof(VAR)); if (!(VAR)) { fprintf(stderr,"missing ISP ABI %s\n",NAME); goto cleanup; } } while(0)

int main(int argc,char **argv) {
    if (argc != 7 || (strcmp(argv[1],"--configure-synthetic") && strcmp(argv[1],"--process-synthetic")) ||
        strcmp(argv[2],"--target") || strcmp(argv[3],"cronos") ||
        strcmp(argv[4],"--acknowledge-isp-job") ||
        strcmp(argv[5],"generated-bayer-no-sensor") || strcmp(argv[6],"run") || !target_allowed()) {
        fputs("refusing: guarded synthetic Cronos ISP invocation required\n",stderr); return 2;
    }
    int process=!strcmp(argv[1],"--process-synthetic");
    if (process && !TATER_ISP_UNSAFE_LAB_JOB) {
        fputs("refusing: ISP execution quarantined pending IMGI DMA/lockup investigation\n",stderr);
        return 2;
    }
    struct memory input={.ion=-1,.fd=-1},output={.ion=-1,.fd=-1};
    void *pipe=NULL;
    int initialized=0,started=0,result=1;
    void *isp=NULL;
    void *(*isp_factory)(void)=NULL;
    void *(*cq_buffer)(void *,int,int,int)=NULL;
    void *(*factory)(unsigned,unsigned,const char *)=NULL;
    int (*initialize)(void *)=NULL,(*uninitialize)(void *)=NULL;
    void (*destroy)(void *)=NULL;
    int (*configure)(void *,const struct vector_view *,const struct vector_view *,void *)=NULL;
    int (*start)(void *)=NULL,(*stop)(void *)=NULL,(*finish)(void *,int)=NULL;
    int (*command)(void *,int,int,int,int)=NULL;
    int (*dequeue)(void *,struct port_id,void *,int,int,int,int,unsigned)=NULL;
    int (*dequeue_input)(void *,struct port_id,void *,int,int,int,int,unsigned)=NULL;
    void (*release_vector)(void *)=NULL;
    uint32_t completed_buffer[64]={0};
    uint32_t completed_input[64]={0};
    struct sigaction fault; memset(&fault,0,sizeof(fault));
    fault.sa_sigaction=probe_fault; fault.sa_flags=SA_SIGINFO;
    sigemptyset(&fault.sa_mask); sigaction(SIGSEGV,&fault,NULL); sigaction(SIGBUS,&fault,NULL);
    alarm(12);
    void *library=dlopen("libimageio_FrmB.so",RTLD_NOW|RTLD_LOCAL);
    if (!library) { fprintf(stderr,"ISP load: %s\n",dlerror()); goto cleanup; }
    RESOLVE(factory,"_ZN14NSImageio_FrmB12NSIspio_FrmB13IPostProcPipe14createInstanceENS0_12EDrvScenarioENS0_12EScenarioFmtEPKa");
    RESOLVE(initialize,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge4initEv");
    RESOLVE(uninitialize,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge6uninitEv");
    RESOLVE(destroy,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge15destroyInstanceEv");
    RESOLVE(configure,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge10configPipeERKNSt3__16vectorIPKNS0_8PortInfoENS2_9allocatorIS6_EEEESB_PNS0_15PipePackageInfoE");
    RESOLVE(start,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge5startEv");
    RESOLVE(stop,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge4stopEv");
    RESOLVE(finish,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge16dequeMdpFrameEndEi");
    RESOLVE(command,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge11sendCommandEiiii");
    RESOLVE(dequeue,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge11dequeOutBufENS0_6PortIDERNS0_17QTimeStampBufInfoENS0_12EDrvScenarioEiiij");
    RESOLVE(dequeue_input,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge10dequeInBufENS0_6PortIDERNS0_17QTimeStampBufInfoENS0_12EDrvScenarioEiiij");
    RESOLVE(isp_factory,"_ZN13NSIspDrv_FrmB6IspDrv14createInstanceEv");
    RESOLVE(cq_buffer,"_ZN13NSIspDrv_FrmB6IspDrv18getCQVirBufVirAddrEiii");
    RESOLVE(release_vector,"_ZdlPv");
    puts("TATER_ISP_SYNTHETIC allocate_begin"); fflush(stdout);
    if (memory_open(&input,W*H*5/4,27) || memory_open(&output,W*H*3/2,15)) {
        fprintf(stderr,"synthetic ION allocation failed: %s\n",strerror(errno)); goto cleanup;
    }
    generated_bayer(input.pixels); memset(output.pixels,0xa5,output.size);
    pipe=factory(0,0,"TaterISPSyntheticQualification");
    if (!pipe) goto cleanup;
    initialized=initialize(pipe); if (!initialized) goto cleanup;
    isp=isp_factory(); if (!isp) goto cleanup;
    uint32_t source[PORT_BYTES/4],destination[PORT_BYTES/4],package[32]={0};
    port(source,&input,0); port(destination,&output,1);
    const void *in_ports[]={source},*out_ports[]={destination};
    struct vector_view ins={in_ports,in_ports+1,in_ports+1},outs={out_ports,out_ports+1,out_ports+1};
    puts("TATER_ISP_SYNTHETIC configure_begin"); fflush(stdout);
    int configured=configure(pipe,&ins,&outs,package);
    printf("TATER_ISP_SYNTHETIC configure_return=%d\n",configured); fflush(stdout);
    if (!configured) goto cleanup;
    const uint32_t *shadow=cq_buffer(isp,3,0,0);
    if (!shadow_mapping_allowed(shadow)) {
        fputs("refusing: CQ shadow buffer mapping not established\n",stderr); goto cleanup;
    }
    static const uint32_t config_registers[]={0x4004,0x4008,0x400c,0x4010,0x4018,0x4230,0x4234,0x4238,0x423c,0x4240};
    for (unsigned r=0;r<sizeof(config_registers)/sizeof(config_registers[0]);++r)
        printf("TATER_ISP_SHADOW offset=%04x value=%08x\n",config_registers[r],shadow[config_registers[r]/4]);
    printf("TATER_ISP_SYNTHETIC expected_input_mva=%08x stride=%u byte_width=%u height=%u\n",input.device,W*5/4,W*5/4,H); fflush(stdout);
    if (process) {
        struct port_id input_id,output_id;
        memcpy(&input_id,&source[0xec/4],sizeof(input_id));
        memcpy(&output_id,&destination[0xec/4],sizeof(output_id));
        /* configPipe already enqueues each descriptor once. */
        int flushed=command(pipe,0x1103,3,0,0);
        printf("TATER_ISP_SYNTHETIC command_flush_return=%d\n",flushed); fflush(stdout);
        if (!flushed) goto cleanup;
        puts("TATER_ISP_SYNTHETIC start_begin"); fflush(stdout);
        started=start(pipe);
        printf("TATER_ISP_SYNTHETIC start_return=%d\n",started); fflush(stdout);
        if (!started) goto cleanup;
        runtime_maps();
        int input_dequeued=dequeue_input(pipe,input_id,completed_input,0,3,0,0,1000);
        printf("TATER_ISP_SYNTHETIC input_dequeue_return=%d\n",input_dequeued); fflush(stdout);
        printf("TATER_ISP_SYNTHETIC input_mva=%08x input_size=%u output_mva=%08x\n",input.device,(unsigned)input.size,output.device);
        /* Do not read MMIO after a failed hardware job: its clock/reset
         * state is not established. Preserve logs via a separate channel. */
        if (input_dequeued<0) goto cleanup;
        /* This owner's ABI orders the selector integers as CQ, duplicate,
         * burst, then timeout. A burst index is never a millisecond value. */
        int dequeued=dequeue(pipe,output_id,completed_buffer,0,3,0,0,1000);
        printf("TATER_ISP_SYNTHETIC dequeue_return=%d\n",dequeued); fflush(stdout);
        if (dequeued<0) goto cleanup;
        int complete=finish(pipe,1000);
        printf("TATER_ISP_SYNTHETIC frame_end_return=%d\n",complete); fflush(stdout);
        size_t changed=0;
        for (size_t n=0;n<output.size;++n) changed+=output.pixels[n]!=0xa5;
        printf("TATER_ISP_SYNTHETIC changed_bytes=%u total_bytes=%u\n",(unsigned)changed,(unsigned)output.size);
        for (unsigned channel=0;channel<3;++channel) {
            unsigned plane_width=channel ? W/2:W,plane_height=channel ? H/2:H;
            size_t offset=channel==0 ? 0:channel==1 ? W*H:W*H*5/4;
            for (unsigned band=0;band<4;++band) {
                uint64_t sum=0,count=0;
                for (unsigned y=8;y<plane_height-8;++y) for (unsigned x=band*plane_width/4+4;x<(band+1)*plane_width/4-4;++x) {
                    sum+=output.pixels[offset+y*plane_width+x]; ++count;
                }
                printf("TATER_ISP_SYNTHETIC channel=%u band=%u mean=%u\n",channel,band,(unsigned)(sum/count));
            }
        }
        if (!complete || changed<output.size/2) goto cleanup;
        puts("TATER_ISP_SYNTHETIC color_and_dma_qualification_pending=true");
        goto cleanup;
    }
    result=0;
cleanup:
    if (started && stop) { puts("TATER_ISP_SYNTHETIC stop_begin"); fflush(stdout); (void)stop(pipe); }
    if (initialized && uninitialize) (void)uninitialize(pipe);
    if (pipe && destroy) destroy(pipe);
    if (completed_buffer[3] && release_vector) release_vector((void *)(uintptr_t)completed_buffer[3]);
    if (completed_input[3] && release_vector) release_vector((void *)(uintptr_t)completed_input[3]);
    memory_close(&output); memory_close(&input);
    printf("TATER_ISP_SYNTHETIC exit=%d processing_requested=%d sensor_opened=false\n",result,process);
    fflush(stdout);
    _exit(result);
}

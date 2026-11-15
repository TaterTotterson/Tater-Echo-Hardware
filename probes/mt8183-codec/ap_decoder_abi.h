/* Original native-clock adaptation and public decoder ABI declarations. */
#include "ap_legacy.c"
#include "vdec_drv_if_private.h"
#include "hal_types_private.h"
#include "mtk_yuv420_block.h"
#include <signal.h>
typedef VAL_RESULT_T (*hal_cmd_fn)(VAL_HANDLE_T *,HAL_CMD_T,void *,void *);
static hal_cmd_fn original_hal_command;
VAL_RESULT_T eHalCmdProc(VAL_HANDLE_T *handle,HAL_CMD_T command,void *input,void *output) {
    if(!original_hal_command)_exit(39);
    unsigned replacements[8],replacement_count=0;
    if(command==HAL_CMD_SET_CMD_QUEUE && input) {
        VCODEC_DRV_CMD_T *queue=input;
        unsigned ended=0;
        for(unsigned i=0;i<2048;i++) {
            if(queue[i].type>END_CMD)_exit(42);
            if(queue[i].address==0x16000000) {
                AP_TRACE("RAW_DEC_SYS_QUEUE index=%u type=%u offset=%#lx value=%#lx mask=%#lx\n",i,queue[i].type,queue[i].offset,queue[i].value,queue[i].mask);
                unsigned enabled=0;
                for(unsigned m=0;m<observed_map_count;m++)if(observed_maps[m].physical==0x16000000)
                    enabled=!!(*(volatile unsigned *)observed_maps[m].va&1);
                if(queue[i].type!=MASTER_WRITE_CMD || queue[i].offset || queue[i].value!=1 ||
                   queue[i].mask!=0xfffffffeUL || !enabled || replacement_count==8)_exit(43);
                replacements[replacement_count++]=i;
            }
            if(queue[i].type==END_CMD){ended=1;break;}
        }
        if(!ended)_exit(44);
        /* Native clock remains enabled for this exclusive AP session. Replace
         * ONLY the observed idempotent GCON enable with a real CPU barrier;
         * WAIT_ISR and every actual video-register operation stay original.
         * Restore the caller's command types after synchronous execution. */
        for(unsigned n=0;n<replacement_count;n++)queue[replacements[n]].type=MB_CMD;
    }
    VAL_RESULT_T result=original_hal_command(handle,command,input,output);
    for(unsigned n=0;n<replacement_count;n++)((VCODEC_DRV_CMD_T *)input)[replacements[n]].type=MASTER_WRITE_CMD;
    if(replacement_count)AP_TRACE("RAW_DEC_NATIVE_CLOCK_QUEUE translated=%u\n",replacement_count);
    return result;
}
static void irq_failure_snapshot(void) { _exit(38); }
/* The original Android HAL redundantly W1S-enables VDEC through SYS+0.
 * Linux owns that clock here. Accept ONLY a source-defined already-satisfied
 * enable request, confirmed against the real read-only hardware status. */
unsigned H264_HAL_WriteSYS(void *handle,unsigned offset,unsigned value,unsigned trace) {
    (void)handle;(void)trace;
    if(register_mode && owned_codec==VAL_MEM_CODEC_FOR_VDEC && offset==0x18 && value==1) {
        int result=ioctl(adapter,AP_DCM,NULL);
        AP_TRACE("RAW_DEC_NATIVE_DCM_DISABLE result=%d errno=%d\n",result,errno);
        if(!result)return 0;
    }
    if(register_mode && owned_codec==VAL_MEM_CODEC_FOR_VDEC && offset==0 && value==1)
        for(unsigned i=0;i<observed_map_count;i++)if(observed_maps[i].physical==0x16000000) {
            __sync_synchronize();unsigned status=*(volatile unsigned *)observed_maps[i].va;
            AP_TRACE("RAW_DEC_NATIVE_CLOCK_ENABLE status=%#x already_enabled=%u\n",status,!!(status&1));
            if(status&1){__sync_synchronize();return 0;}
        }
    AP_TRACE("RAW_DEC_CLOCK_POLICY_REFUSED offset=%#x value=%#x\n",offset,value);
    _exit(37); /* Never write/disable a clock behind the native clock framework. */
}
_Static_assert(sizeof(VAL_MEMORY_T)==96,"Athena VAL memory ABI");
_Static_assert(sizeof(VDEC_DRV_RINGBUF_T)==56,"published ring ABI");
_Static_assert(sizeof(VDEC_DRV_SEQINFO_T)==40,"published sequence ABI");
_Static_assert(sizeof(VDEC_DRV_FRAMEBUF_T)==120,"published frame ABI");
#define LOAD(name,type) type name;do {void *symbol=dlsym(library,#name);memcpy(&name,&symbol,sizeof name);if(!name)return 8;}while(0)
typedef VDEC_DRV_MRESULT_T (*create_fn)(VAL_HANDLE_T *,VDEC_DRV_VIDEO_FORMAT_T);
typedef VDEC_DRV_MRESULT_T (*init_fn)(VAL_HANDLE_T,VDEC_DRV_RINGBUF_T *,VDEC_DRV_SEQINFO_T *);
typedef VDEC_DRV_MRESULT_T (*release_fn)(VAL_HANDLE_T);
typedef VDEC_DRV_MRESULT_T (*set_fn)(VAL_HANDLE_T,VDEC_DRV_SET_TYPE_T,void *,void *);
typedef VDEC_DRV_MRESULT_T (*get_fn)(VAL_HANDLE_T,VDEC_DRV_GET_TYPE_T,void *,void *);
typedef VDEC_DRV_MRESULT_T (*decode_ex_fn)(VAL_HANDLE_T,VDEC_DRV_RINGBUF_T *,void *,void *);
typedef int32_t (*output_fn)(unsigned long,VDEC_DRV_FRAMEBUF_T **,uint32_t,int8_t,void *);
struct output_callback {void *context,*reserved;output_fn callback;};
_Static_assert(sizeof(struct output_callback)==12,"Athena callback ABI");


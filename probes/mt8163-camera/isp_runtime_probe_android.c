/* SPDX-License-Identifier: MIT
 * Owner-local ISP runtime qualification. The query mode only resolves ABI
 * symbols; the explicit lifecycle mode opens/closes a postprocess object.
 * Neither mode configures a frame, starts processing, opens a sensor, or
 * retains scene pixels.
 * Symbol presence is evidence of an ABI, never a claim that processing works.
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static int target_allowed(void) {
    struct utsname kernel;
    char command_line[8192] = {0};
    struct stat node;
    FILE *input = fopen("/proc/cmdline", "r");
    if (!input) return 0;
    size_t size = fread(command_line, 1, sizeof(command_line)-1, input);
    int failed = ferror(input);
    fclose(input);
    int product = 0;
    char *token = strtok(command_line, " \t\r\n");
    while (token) {
        if (!strncmp(token, "androidboot.product=", 20)) {
            if (product || strcmp(token, "androidboot.product=cronos")) return 0;
            product = 1;
        }
        token = strtok(NULL, " \t\r\n");
    }
    if (!size || failed || !product || uname(&kernel) != 0 ||
        strcmp(kernel.release, "4.9.337-g8d928c5176cc") != 0 ||
        stat("/dev/camera-isp", &node) != 0 || !S_ISCHR(node.st_mode)) return 0;
    return sizeof(void *) == 4;
}

static void json_string(const char *value) {
    putchar('"');
    for (const unsigned char *p=(const unsigned char *)value; p && *p; ++p) {
        if (*p == '\\' || *p == '"') printf("\\%c", *p);
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

struct query { const char *library; const char *symbol; const char *role; };

static int lifecycle(void) {
    void *library = dlopen("libimageio_FrmB.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "ISP library load failed: %s\n", dlerror()); return 1; }
    void *(*factory)(unsigned, unsigned, const char *) = NULL;
    int (*initialize)(void *) = NULL;
    int (*uninitialize)(void *) = NULL;
    void (*destroy)(void *) = NULL;
    void *symbol = dlsym(library,
        "_ZN14NSImageio_FrmB12NSIspio_FrmB13IPostProcPipe14createInstanceENS0_12EDrvScenarioENS0_12EScenarioFmtEPKa");
    memcpy(&factory, &symbol, sizeof(factory));
    symbol = dlsym(library,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge4initEv");
    memcpy(&initialize, &symbol, sizeof(initialize));
    symbol = dlsym(library,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge6uninitEv");
    memcpy(&uninitialize, &symbol, sizeof(uninitialize));
    symbol = dlsym(library,"_ZN14NSImageio_FrmB12NSIspio_FrmB19IPostProcPipeBridge15destroyInstanceEv");
    memcpy(&destroy, &symbol, sizeof(destroy));
    if (!factory || !initialize || !uninitialize || !destroy) {
        fputs("ISP lifecycle ABI unavailable\n",stderr); return 1;
    }
    puts("TATER_ISP_LIFECYCLE factory_begin"); fflush(stdout);
    /* No sensor scenario is selected and no frame configuration/start API is
     * called. Scenario and raw format zero only select a postprocess object. */
    void *pipe = factory(0, 0, "TaterISPSyntheticQualification");
    if (!pipe) { fputs("ISP factory returned null\n",stderr); return 1; }
    puts("TATER_ISP_LIFECYCLE init_begin"); fflush(stdout);
    int initialized = initialize(pipe);
    puts("TATER_ISP_LIFECYCLE uninit_begin"); fflush(stdout);
    int uninitialized = initialized ? uninitialize(pipe) : 0;
    puts("TATER_ISP_LIFECYCLE destroy_begin"); fflush(stdout);
    destroy(pipe);
    printf("TATER_ISP_RESULT {\"schema\":1,\"target\":\"cronos\","
           "\"initialization_return\":%d,\"uninitialization_return\":%d,"
           "\"capture_performed\":false,\"processing_performed\":false}\n",
           initialized,uninitialized);
    fflush(stdout);
    return initialized && uninitialized ? 0 : 1;
}

int main(int argc, char **argv) {
    static const struct query queries[] = {
        {"libimageio_FrmB.so", "_ZN14NSImageio_FrmB12NSIspio_FrmB13IPostProcPipe14createInstanceENS0_12EDrvScenarioENS0_12EScenarioFmtEPKa", "postprocess_factory"},
        {"libimageio_plat_drv_FrmB.so", "_ZN13NSDrvCam_FrmB12ISP_TOP_CTRL7_configEv", "isp_top_config"},
        {"libdpframework.so", "_ZN11DpIspStreamC1ENS_13ISPStreamTypeE", "mdp_isp_stream_constructor"},
        {"libdpframework.so", "_ZN12DpBlitStream14queryHWSupportEjjjji13DP_COLOR_ENUMS0_", "mdp_format_query"},
    };
    if (argc != 7 ||
        (strcmp(argv[1], "--query-runtime") && strcmp(argv[1], "--initialize-postprocess")) ||
        strcmp(argv[2], "--target") || strcmp(argv[3], "cronos") ||
        strcmp(argv[4], "--acknowledge-load") ||
        strcmp(argv[5], "owner-isp-no-sensor") || strcmp(argv[6], "run") ||
        !target_allowed()) {
        fputs("refusing: guarded Cronos ISP runtime query required\n", stderr);
        return 2;
    }
    alarm(10);
    if (!strcmp(argv[1], "--initialize-postprocess")) _exit(lifecycle());
    puts("{\"schema\":1,\"target\":\"cronos\",\"capture_performed\":false,"
         "\"processing_performed\":false,\"queries\":[");
    int success = 1;
    for (size_t i=0; i<sizeof(queries)/sizeof(queries[0]); ++i) {
        if (i) puts(",");
        const struct query *q = &queries[i];
        printf("{\"library\":"); json_string(q->library);
        printf(",\"role\":"); json_string(q->role);
        dlerror();
        void *library = dlopen(q->library, RTLD_NOW | RTLD_LOCAL);
        const char *error = library ? NULL : dlerror();
        void *symbol = NULL;
        if (library) { dlerror(); symbol=dlsym(library,q->symbol); error=dlerror(); }
        printf(",\"loaded\":%s,\"symbol_present\":%s,\"error\":",
               library ? "true" : "false", symbol ? "true" : "false");
        json_string(error ? error : ""); puts("}");
        if (!library || !symbol) success=0;
        /* Retained vendor runtimes are process-lifetime. Do not exercise
         * unload-time destructors while qualifying their headless ABI. */
    }
    puts("]}");
    fflush(stdout);
    _exit(success ? 0 : 1);
}

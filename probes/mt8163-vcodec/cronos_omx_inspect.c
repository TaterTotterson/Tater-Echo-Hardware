/* SPDX-License-Identifier: MIT
 * Cronos OpenMAX Loaded-state inventory. No buffers or stream transitions.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <media/openmax/OMX_Component.h>
#include "../mt8163-gpu/cronos_target.h"

static OMX_ERRORTYPE event(OMX_HANDLETYPE h, OMX_PTR p, OMX_EVENTTYPE e,
                          OMX_U32 a, OMX_U32 b, OMX_PTR d) {
    (void)h; (void)p; (void)e; (void)a; (void)b; (void)d;
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE buffer(OMX_HANDLETYPE h, OMX_PTR p, OMX_BUFFERHEADERTYPE *b) {
    (void)h; (void)p; (void)b;
    return OMX_ErrorNone;
}

int main(int argc, char **argv) {
    OMX_COMPONENTTYPE *(*create)(OMX_STRING) = NULL;
    OMX_COMPONENTTYPE *component;
    OMX_CALLBACKTYPE callbacks = {event, buffer, buffer};
    OMX_STATETYPE state = OMX_StateInvalid;
    OMX_PARAM_PORTDEFINITIONTYPE definitions[2];
    OMX_U32 parameter_version = 0;
    void *library, *symbol;
    const char *name;
    int result = 1;
    if (argc != 6 || strcmp(argv[1], "--inspect") || strcmp(argv[3], "--target") ||
        strcmp(argv[4], "cronos") || strcmp(argv[5], "bounded-private-buffers") ||
        !cronos_gpu_target_allowed()) {
        fprintf(stderr, "refusing: guarded Cronos Loaded-state inspection required argc=%d target_ok=%d\n",
                argc, cronos_gpu_target_allowed());
        return 2;
    }
    name = strstr(argv[2], "Venc") ? "OMX.MTK.VIDEO.ENCODER.AVC" :
                                   "OMX.MTK.VIDEO.DECODER.AVC";
    alarm(20);
    fputs("CRONOS_INSPECT dlopen\n", stderr); fflush(stderr);
    library = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "component load failed: %s\n", dlerror());
        return 1;
    }
    symbol = dlsym(library, "MtkOmxComponentCreate");
    if (!symbol || sizeof(symbol) != sizeof(create)) return 1;
    memcpy(&create, &symbol, sizeof(create));
    fputs("CRONOS_INSPECT create\n", stderr); fflush(stderr);
    component = create((OMX_STRING)name);
    if (!component || component->nSize < sizeof(*component)) return 1;
    fputs("CRONOS_INSPECT callbacks/state\n", stderr); fflush(stderr);
    if (component->SetCallbacks(component, &callbacks, NULL) != OMX_ErrorNone ||
        component->GetState(component, &state) != OMX_ErrorNone || state != OMX_StateLoaded)
        goto cleanup;
    /* These are the public IL version word and the legacy MediaTek value
     * already observed in the separate encoder. No hardware transition.
     */
    const OMX_U32 versions[] = {1, 0x00000101, 0x00020101};
    for (unsigned attempt = 0; attempt < sizeof(versions)/sizeof(versions[0]); ++attempt) {
        memset(&definitions[0], 0, sizeof(definitions[0]));
        definitions[0].nSize = sizeof(definitions[0]);
        definitions[0].nVersion.nVersion = versions[attempt];
        OMX_ERRORTYPE rc = component->GetParameter(component, OMX_IndexParamPortDefinition, &definitions[0]);
        fprintf(stderr, "CRONOS_INSPECT header size=%u version=%u result=0x%08x\n",
                (unsigned)sizeof(definitions[0]), versions[attempt], rc);
        if (rc == OMX_ErrorNone) { parameter_version = versions[attempt]; break; }
    }
    if (!parameter_version) goto cleanup;
    printf("{\"component\":\"%s\",\"loaded_state\":true,\"parameter_version\":%u,\"ports\":[", name, parameter_version);
    for (unsigned port = 0; port < 2; ++port) {
        OMX_PARAM_PORTDEFINITIONTYPE definition;
        memset(&definition, 0, sizeof(definition));
        definition.nSize = sizeof(definition);
        definition.nVersion.nVersion = parameter_version;
        definition.nPortIndex = port;
        OMX_ERRORTYPE rc = component->GetParameter(component, OMX_IndexParamPortDefinition, &definition);
        if (rc != OMX_ErrorNone) {
            fprintf(stderr, "port query failed: 0x%08x\n", rc);
            goto cleanup;
        }
        if (port) putchar(',');
        printf("{\"index\":%u,\"buffers\":%u,\"bytes\":%u,\"width\":%u,\"height\":%u,\"color\":%u}",
            port, definition.nBufferCountActual, definition.nBufferSize,
            definition.format.video.nFrameWidth, definition.format.video.nFrameHeight,
            definition.format.video.eColorFormat);
    }
    puts("],\"camera_opened\":false,\"buffers_allocated\":false}");
    result = 0;
cleanup:
    fputs("CRONOS_INSPECT deinit\n", stderr); fflush(stderr);
    if (component->ComponentDeInit(component) != OMX_ErrorNone) result = 1;
    fflush(NULL);
    /* Vendor DSOs live for this isolated process. The raw-driver query showed
     * an unload-time crash; do not claim dlclose compatibility from this test.
     * No stream/ION allocation was made; the process owns its remaining heap.
     */
    _exit(result);
}

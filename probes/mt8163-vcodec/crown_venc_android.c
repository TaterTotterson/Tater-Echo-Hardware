/* SPDX-License-Identifier: MIT
 * Separate Crown qualification entry point. Other targets remain unchanged.
 * Only synthetic YUV/Loaded-state inventory is enabled in this first milestone.
 */
/* Original bounded OpenMAX IL client. No vendor implementation is included. */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <media/openmax/OMX_Component.h>
#include <media/openmax/OMX_Core.h>
#include <media/openmax/OMX_Video.h>
#include "tater_bayer10.h"
#include "planar_chroma.h"
#include "../mt8163-gpu/crown_target.h"

#define MAX_BUFFERS 16U
#define MAX_OUTPUT (16U * 1024U * 1024U)
#define MAX_FRAMES 120U
#define COMPONENT_NAME "OMX.MTK.VIDEO.ENCODER.AVC"

/* Owner-local library diagnostics otherwise go to an absent Android logger.
 * This probe-only compatibility entry point forwards them to stderr. */
int lab126_log_write(int priority, const char *tag, const char *area,
                     const char *format, ...) {
    (void)priority; (void)tag;
    fprintf(stderr, "VENDOR %s: ", area ? area : "");
    va_list args;
    va_start(args, format);
    int result = vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return result;
}

struct state {
    pthread_mutex_t mutex;
    OMX_ERRORTYPE error;
    OMX_STATETYPE completed;
    unsigned commands, emptied, filled, pictures, eos;
    OMX_BUFFERHEADERTYPE *returned[MAX_BUFFERS];
    OMX_BUFFERHEADERTYPE *outputs[MAX_BUFFERS];
    unsigned output_count;
    uint8_t *bytes;
    size_t length;
    int recycle;
    int stream_fd;
};

static void le32(uint8_t *p, uint32_t v) {
    for (unsigned n = 0; n < 4; ++n) p[n] = (uint8_t)(v >> (8*n));
}
static void le64(uint8_t *p, uint64_t v) {
    for (unsigned n = 0; n < 8; ++n) p[n] = (uint8_t)(v >> (8*n));
}
static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t load64(const uint8_t *p) {
    return (uint64_t)load32(p) | (uint64_t)load32(p+4)<<32;
}
static int transfer(int fd, void *bytes, size_t length, int writing) {
    uint8_t *p = bytes;
    size_t offset = 0;
    while (offset < length) {
        struct pollfd ready = {.fd=fd, .events=writing ? POLLOUT : POLLIN};
        int rc = poll(&ready, 1, 10000);
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0) return -1;
        ssize_t n = writing ? write(fd, p+offset, length-offset) : read(fd, p+offset, length-offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return !writing && !n && !offset ? 0 : -1;
        offset += (size_t)n;
    }
    return 1;
}

static int packet(struct state *s, OMX_BUFFERHEADERTYPE *b) {
    uint8_t h[16] = {0};
    le32(h, b->nFilledLen);
    unsigned flags = (b->nFlags & OMX_BUFFERFLAG_EOS ? 1U : 0U) |
                     (b->nFlags & OMX_BUFFERFLAG_CODECCONFIG ? 2U : 0U) |
                     (b->nFlags & OMX_BUFFERFLAG_SYNCFRAME ? 4U : 0U);
    le32(h+4, flags);
    le64(h+8, (uint64_t)b->nTimeStamp);
    if (transfer(s->stream_fd, h, sizeof(h), 1) < 0) return -1;
    return b->nFilledLen && transfer(s->stream_fd, b->pBuffer+b->nOffset, b->nFilledLen, 1) < 0 ? -1 : 0;
}

static void header(void *value, size_t size) {
    OMX_U32 *words = value;
    memset(value, 0, size);
    words[0] = (OMX_U32)size;
    /* Exact owner-local encoder ABI: unlike the decoder, parameter queries
     * compare the complete version word with 1, not IL's 0x00020101. */
    words[1] = 1;
}

static OMX_ERRORTYPE event(OMX_HANDLETYPE component, OMX_PTR data,
                          OMX_EVENTTYPE type, OMX_U32 a, OMX_U32 b,
                          OMX_PTR extra) {
    struct state *s = data;
    (void)component; (void)extra;
    pthread_mutex_lock(&s->mutex);
    if (type == OMX_EventCmdComplete && a == OMX_CommandStateSet) {
        s->completed = b;
        ++s->commands;
    } else if (type == OMX_EventError) {
        s->error = a;
    }
    pthread_mutex_unlock(&s->mutex);
    fprintf(stderr, "VENC_EVENT type=%u a=%u b=%u\n", type, a, b);
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE empty(OMX_HANDLETYPE component, OMX_PTR data,
                          OMX_BUFFERHEADERTYPE *buffer) {
    struct state *s = data;
    (void)component;
    pthread_mutex_lock(&s->mutex);
    s->returned[s->emptied++ % MAX_BUFFERS] = buffer;
    pthread_mutex_unlock(&s->mutex);
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE fill(OMX_HANDLETYPE handle, OMX_PTR data,
                         OMX_BUFFERHEADERTYPE *buffer) {
    struct state *s = data;
    OMX_COMPONENTTYPE *component = handle;
    unsigned i;
    pthread_mutex_lock(&s->mutex);
    for (i = 0; i < s->output_count; ++i)
        if (s->outputs[i] == buffer) break;
    if (i == s->output_count || buffer->nOffset > buffer->nAllocLen ||
        buffer->nFilledLen > buffer->nAllocLen - buffer->nOffset ||
        buffer->nFilledLen > MAX_OUTPUT ||
        (s->stream_fd < 0 && buffer->nFilledLen > MAX_OUTPUT - s->length)) {
        s->error = OMX_ErrorOverflow;
    } else {
        if (s->stream_fd >= 0) {
            if (packet(s, buffer)) s->error = OMX_ErrorUndefined;
        } else {
            memcpy(s->bytes + s->length, buffer->pBuffer + buffer->nOffset,
                   buffer->nFilledLen);
        }
        s->length += buffer->nFilledLen;
        if (buffer->nFilledLen && !(buffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG))
            ++s->pictures;
        if (buffer->nFlags & OMX_BUFFERFLAG_EOS) ++s->eos;
    }
    ++s->filled;
    int recycle = s->recycle && !s->error && !(buffer->nFlags & OMX_BUFFERFLAG_EOS);
    pthread_mutex_unlock(&s->mutex);
    if (recycle) {
        buffer->nFilledLen = buffer->nOffset = buffer->nFlags = 0;
        OMX_ERRORTYPE result = component->FillThisBuffer(component, buffer);
        if (result) {
            pthread_mutex_lock(&s->mutex);
            s->error = result;
            pthread_mutex_unlock(&s->mutex);
        }
    }
    return OMX_ErrorNone;
}

static int wait_state(struct state *s, unsigned before, OMX_STATETYPE target) {
    for (unsigned n = 0; n < 10000; ++n) {
        pthread_mutex_lock(&s->mutex);
        int failed = s->error != OMX_ErrorNone;
        int ready = s->commands > before && s->completed == target;
        pthread_mutex_unlock(&s->mutex);
        if (failed) return -1;
        if (ready) return 0;
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    fprintf(stderr, "VENC state timeout target=%u\n", target);
    return -1;
}

static unsigned command_count(struct state *s) {
    pthread_mutex_lock(&s->mutex);
    unsigned count = s->commands;
    pthread_mutex_unlock(&s->mutex);
    return count;
}

static int target_ok(void) {
    return crown_gpu_target_allowed();
}

static void synthetic(uint8_t *dst, unsigned width, unsigned height,
                      unsigned stride, unsigned slice, unsigned index,
                      unsigned color) {
    size_t ybytes = (size_t)stride * slice;
    memset(dst, 16, ybytes);
    memset(dst + ybytes, 128, ybytes / 2);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            dst[y * stride + x] = (uint8_t)(32 + ((x + y + index * 8) % 192));
    /* Distinct moving colored bars: no camera or ambient audio is opened. */
    for (unsigned y = 0; y < height / 2; ++y)
        for (unsigned x = 0; x < width / 2; ++x) {
            uint8_t u = (uint8_t)(64 + ((x + index * 4) % 128));
            uint8_t v = (uint8_t)(192 - ((y + index * 3) % 128));
            if (color == OMX_COLOR_FormatYUV420Planar) {
                dst[ybytes + y * (stride / 2) + x] = u;
                dst[ybytes + ybytes / 4 + y * (stride / 2) + x] = v;
            } else {
                dst[ybytes + y * stride + x * 2] = u;
                dst[ybytes + y * stride + x * 2 + 1] = v;
            }
        }
}

#define REQUIRE(call) do { OMX_ERRORTYPE rc_ = (call); if (rc_) { \
    fprintf(stderr, "VENC failed line=%d result=0x%08x\n", __LINE__, rc_); goto cleanup; \
} } while (0)

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--capabilities")) {
        puts("{\"adapter\":\"tater-venc\",\"version\":1,\"codec\":\"h264\",\"input\":\"crown-bggr1280x720-packed10-lut\",\"stream_versions\":[1],\"max_width\":1280,\"max_height\":720,\"max_fps\":30}");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--describe")) {
        puts("{\"component\":\"" COMPONENT_NAME "\",\"target\":\"crown\",\"camera_opened\":false,\"max_frames\":120,\"max_width\":1280,\"max_height\":720}");
        return 0;
    }
    int encode = argc == 10 && !strcmp(argv[1], "--encode-synthetic");
    int streaming = argc == 10 && !strcmp(argv[1], "--stream-bayer10");
    int inspect = argc == 6 && !strcmp(argv[1], "--inspect");
    if ((!encode && !inspect && !streaming) || strcmp(argv[3], "--target") ||
        strcmp(argv[4], "crown") || strcmp(argv[5], "bounded-private-buffers") || !target_ok()) {
        fprintf(stderr, "usage: %s --inspect COMPONENT --target crown bounded-private-buffers\n"
                "       %s --encode-synthetic COMPONENT --target crown bounded-private-buffers WIDTH HEIGHT FRAMES OUTPUT\n", argv[0], argv[0]);
        return 2;
    }
    unsigned width = (encode || streaming) ? (unsigned)atoi(argv[6]) : 128;
    unsigned height = (encode || streaming) ? (unsigned)atoi(argv[7]) : 128;
    unsigned frames = encode ? (unsigned)atoi(argv[8]) : 1;
    if (width < 128 || width > 1280 || height < 96 || height > 720 ||
        width % 16 || height % 16 || !frames || frames > MAX_FRAMES) return 2;
    if (streaming && (strcmp(argv[8], "30") || strcmp(argv[9], "-") ||
        !((width == 1280 && height == 720) || (width == 768 && height == 432)))) return 2;
    signal(SIGPIPE, SIG_IGN);
    alarm(45);
    int result = 1, allocated = 0, output_fd = -1;
    void *library = NULL;
    OMX_COMPONENTTYPE *component = NULL;
    OMX_BUFFERHEADERTYPE *buffers[2][MAX_BUFFERS] = {{0}};
    unsigned counts[2] = {0};
    OMX_PARAM_PORTDEFINITIONTYPE defs[2];
    struct state state = {.mutex = PTHREAD_MUTEX_INITIALIZER, .stream_fd = -1};
    uint8_t *raw = NULL;
    uint16_t *scratch = NULL;
    size_t raw_size = (size_t)1280 * 720 * 5 / 4;
    if (!streaming) {
        state.bytes = malloc(MAX_OUTPUT);
        if (!state.bytes) goto cleanup;
    } else {
        raw = malloc(raw_size + 6144);
        scratch = malloc((size_t)1280 * 720 * sizeof(*scratch));
        if (!raw || !scratch) goto cleanup;
    }
    library = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "VENC dlopen: %s\n", dlerror()); goto cleanup; }
    void *symbol = dlsym(library, "MtkOmxComponentCreate");
    OMX_COMPONENTTYPE *(*create)(OMX_STRING) = NULL;
    if (!symbol || sizeof(symbol) != sizeof(create)) goto cleanup;
    memcpy(&create, &symbol, sizeof(create));
    component = create((OMX_STRING)COMPONENT_NAME);
    if (!component || component->nSize < sizeof(*component)) goto cleanup;
    OMX_CALLBACKTYPE callbacks = {event, empty, fill};
    REQUIRE(component->SetCallbacks(component, &callbacks, &state));
    OMX_STATETYPE initial = OMX_StateInvalid;
    REQUIRE(component->GetState(component, &initial));
    char component_name[128] = {0};
    OMX_VERSIONTYPE component_version, spec_version;
    OMX_UUIDTYPE uuid;
    OMX_ERRORTYPE version_result = component->GetComponentVersion(component,
        component_name, &component_version, &spec_version, &uuid);
    fprintf(stderr, "VENC_INITIAL state=%u version_result=0x%08x component=%s spec=0x%08x size=%u\n",
        initial, version_result, component_name, spec_version.nVersion, component->nSize);
    OMX_PORT_PARAM_TYPE ports;
    header(&ports, sizeof(ports));
    OMX_ERRORTYPE inventory = component->GetParameter(component, OMX_IndexParamVideoInit, &ports);
    /* Some legacy encoders reject VideoInit; independently validate both
     * conventional video ports below, including their directions and bounds. */
    fprintf(stderr, "VENC_VIDEO_INIT result=0x%08x start=%u count=%u\n", inventory, ports.nStartPortNumber, ports.nPorts);
    if (inventory == OMX_ErrorNone && (ports.nPorts != 2 || ports.nStartPortNumber != 0)) goto cleanup;
    for (unsigned p = 0; p < 2; ++p) {
        header(&defs[p], sizeof(defs[p]));
        defs[p].nPortIndex = p;
        REQUIRE(component->GetParameter(component, OMX_IndexParamPortDefinition, &defs[p]));
        fprintf(stderr, "VENC_PORT p=%u dir=%u count=%u bytes=%u w=%u h=%u stride=%d slice=%u color=%u codec=%u\n", p,
                defs[p].eDir, defs[p].nBufferCountActual, defs[p].nBufferSize,
                defs[p].format.video.nFrameWidth, defs[p].format.video.nFrameHeight,
                defs[p].format.video.nStride, defs[p].format.video.nSliceHeight,
                defs[p].format.video.eColorFormat, defs[p].format.video.eCompressionFormat);
        if (!defs[p].nBufferCountActual || defs[p].nBufferCountActual > MAX_BUFFERS ||
            defs[p].nBufferSize > MAX_OUTPUT || defs[p].eDir != (p ? OMX_DirOutput : OMX_DirInput)) goto cleanup;
    }
    for (unsigned n = 0; n < 32; ++n) {
        OMX_VIDEO_PARAM_PORTFORMATTYPE f;
        header(&f, sizeof(f)); f.nPortIndex = 0; f.nIndex = n;
        OMX_ERRORTYPE rc = component->GetParameter(component, OMX_IndexParamVideoPortFormat, &f);
        if (rc == OMX_ErrorNoMore) break;
        if (rc) goto cleanup;
        fprintf(stderr, "VENC_INPUT_FORMAT index=%u color=%u compression=%u\n", n, f.eColorFormat, f.eCompressionFormat);
    }
    if (inspect) { result = 0; goto cleanup; }
    /* Exclusive creation prevents accidental truncation, symlinks, or device outputs. */
    if (!streaming) {
        output_fd = open(argv[9], O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (output_fd < 0) goto cleanup;
    }
    for (unsigned p = 0; p < 2; ++p) {
        defs[p].format.video.nFrameWidth = width;
        defs[p].format.video.nFrameHeight = height;
        defs[p].format.video.nStride = width;
        defs[p].format.video.nSliceHeight = height;
        defs[p].format.video.xFramerate = 30U << 16;
        defs[p].format.video.nBitrate = 2000000;
        if (!p) {
            defs[p].format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
            defs[p].nBufferSize = width * height * 3 / 2;
        }
        REQUIRE(component->SetParameter(component, OMX_IndexParamPortDefinition, &defs[p]));
        REQUIRE(component->GetParameter(component, OMX_IndexParamPortDefinition, &defs[p]));
    }
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    header(&bitrate, sizeof(bitrate)); bitrate.nPortIndex = 1;
    bitrate.eControlRate = OMX_Video_ControlRateConstant; bitrate.nTargetBitrate = 2000000;
    REQUIRE(component->SetParameter(component, OMX_IndexParamVideoBitrate, &bitrate));
    OMX_VIDEO_PARAM_AVCTYPE avc;
    header(&avc, sizeof(avc)); avc.nPortIndex = 1;
    REQUIRE(component->GetParameter(component, OMX_IndexParamVideoAvc, &avc));
    avc.nPFrames = 29; avc.nBFrames = 0;
    avc.eProfile = OMX_VIDEO_AVCProfileBaseline;
    avc.eLevel = OMX_VIDEO_AVCLevel31;
    avc.bEntropyCodingCABAC = OMX_FALSE;
    REQUIRE(component->SetParameter(component, OMX_IndexParamVideoAvc, &avc));
    unsigned before = command_count(&state);
    REQUIRE(component->SendCommand(component, OMX_CommandStateSet, OMX_StateIdle, NULL));
    allocated = 1;
    for (unsigned p = 0; p < 2; ++p) {
        if (!defs[p].nBufferCountActual || defs[p].nBufferCountActual > MAX_BUFFERS || defs[p].nBufferSize > MAX_OUTPUT) goto cleanup;
        for (unsigned n = 0; n < defs[p].nBufferCountActual; ++n) {
            REQUIRE(component->AllocateBuffer(component, &buffers[p][n], p, NULL, defs[p].nBufferSize));
            ++counts[p];
        }
    }
    if (wait_state(&state, before, OMX_StateIdle)) goto cleanup;
    before = command_count(&state);
    REQUIRE(component->SendCommand(component, OMX_CommandStateSet, OMX_StateExecuting, NULL));
    if (wait_state(&state, before, OMX_StateExecuting)) goto cleanup;
    if (streaming) {
        uint8_t h[32] = {0};
        memcpy(h, "TVE1", 4); le32(h+4, sizeof(h));
        le32(h+8, width); le32(h+12, height); memcpy(h+16, "H264", 4);
        le32(h+20, 30); le32(h+24, 1); le32(h+28, 4U*1024U*1024U);
        if (transfer(STDOUT_FILENO, h, sizeof(h), 1) < 0) goto cleanup;
        state.stream_fd = STDOUT_FILENO;
        alarm(0);
    }
    pthread_mutex_lock(&state.mutex);
    state.recycle = 1; state.output_count = counts[1];
    memcpy(state.outputs, buffers[1], counts[1] * sizeof(buffers[1][0]));
    pthread_mutex_unlock(&state.mutex);
    for (unsigned n = 0; n < counts[1]; ++n)
        REQUIRE(component->FillThisBuffer(component, buffers[1][n]));
    struct timespec started, ended;
    clock_gettime(CLOCK_MONOTONIC, &started);
    unsigned fed = 0;
    int input_eos = 0;
    int64_t previous_pts = -1;
    for (unsigned n = 0; streaming || n < frames; ++n) {
        int64_t pts = (int64_t)n * 1000000 / 30;
        int request_idr = 0;
        if (streaming) {
            uint8_t h[16];
            int read_result = transfer(STDIN_FILENO, h, sizeof(h), 0);
            if (read_result < 0) goto cleanup;
            input_eos = read_result == 0 || load32(h) == 0;
            if (!input_eos) {
                if (load32(h) != raw_size + 6144 || load32(h+4) > 1 ||
                    transfer(STDIN_FILENO, raw, raw_size+6144, 0) <= 0) goto cleanup;
                request_idr = load32(h+4) == 1;
                pts = (int64_t)load64(h+8);
                if (pts < 0 || pts <= previous_pts) goto cleanup;
                previous_pts = pts;
            }
        }
        OMX_BUFFERHEADERTYPE *b = NULL;
        if (n < counts[0]) b = buffers[0][n];
        else {
            unsigned returned_index = n - counts[0];
            for (unsigned wait = 0; wait < 10000 && !b; ++wait) {
                pthread_mutex_lock(&state.mutex);
                int failed = state.error != OMX_ErrorNone;
                if (state.emptied > returned_index) b = state.returned[returned_index % MAX_BUFFERS];
                pthread_mutex_unlock(&state.mutex);
                if (failed) goto cleanup;
                if (!b) { struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL); }
            }
        }
        unsigned stride = defs[0].format.video.nStride;
        unsigned slice = defs[0].format.video.nSliceHeight;
        if (!stride) stride = width;
        if (!slice) slice = height;
        size_t size = (size_t)stride * slice * 3 / 2;
        if (!b || stride < width || slice < height || size > b->nAllocLen ||
            defs[0].format.video.eColorFormat != OMX_COLOR_FormatYUV420Planar) goto cleanup;
        if (streaming && !input_eos) {
            if (stride != width || slice != height ||
                (width == 768 ?
                 tater_bayer10_balanced_yuv420(raw+6144, raw_size, raw, scratch, b->pBuffer, b->nAllocLen) :
                 tater_bayer10_yuv420(raw+6144, raw_size, raw, width, height, scratch, b->pBuffer, b->nAllocLen))) goto cleanup;
        } else if (!streaming) {
            synthetic(b->pBuffer, width, height, stride, slice, n, defs[0].format.video.eColorFormat);
        }
        /* Generated-pattern hardware/software comparison verifies Y,V,U input
         * despite this qualified component advertising planar YUV. */
        if (!input_eos && tater_i420_to_yv12(b->pBuffer, stride, slice, b->nAllocLen)) goto cleanup;
        b->nOffset = 0; b->nFilledLen = input_eos ? 0 : size; b->nTimeStamp = pts;
        b->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | (input_eos || (!streaming && n + 1 == frames) ? OMX_BUFFERFLAG_EOS : 0);
        alarm(10);
        if (request_idr) {
            OMX_CONFIG_INTRAREFRESHVOPTYPE refresh;
            header(&refresh,sizeof(refresh)); refresh.nPortIndex=1;
            refresh.IntraRefreshVOP=OMX_TRUE;
            REQUIRE(component->SetConfig(component,OMX_IndexConfigVideoIntraVOPRefresh,&refresh));
        }
        REQUIRE(component->EmptyThisBuffer(component, b));
        alarm(0);
        if (input_eos) break;
        ++fed;
    }
    for (unsigned wait = 0; wait < 10000; ++wait) {
        pthread_mutex_lock(&state.mutex);
        unsigned eos = state.eos;
        OMX_ERRORTYPE error = state.error;
        pthread_mutex_unlock(&state.mutex);
        if (error) goto cleanup;
        if (eos) break;
        struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &ended);
    pthread_mutex_lock(&state.mutex);
    state.recycle = 0;
    unsigned pictures = state.pictures, eos = state.eos;
    pthread_mutex_unlock(&state.mutex);
    if (!eos || pictures != fed || state.length == 0) {
        fprintf(stderr, "VENC incomplete pictures=%u expected=%u eos=%u bytes=%zu\n", pictures, fed, eos, state.length);
        goto cleanup;
    }
    before = command_count(&state);
    alarm(15);
    REQUIRE(component->SendCommand(component, OMX_CommandStateSet, OMX_StateIdle, NULL));
    if (wait_state(&state, before, OMX_StateIdle)) goto cleanup;
    before = command_count(&state);
    REQUIRE(component->SendCommand(component, OMX_CommandStateSet, OMX_StateLoaded, NULL));
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned n = 0; n < counts[p]; ++n)
            REQUIRE(component->FreeBuffer(component, p, buffers[p][n]));
    if (wait_state(&state, before, OMX_StateLoaded)) goto cleanup;
    allocated = 0;
    size_t offset = 0;
    while (!streaming && offset < state.length) {
        ssize_t written = write(output_fd, state.bytes + offset, state.length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) goto cleanup;
        offset += written;
    }
    double seconds = ended.tv_sec - started.tv_sec + (ended.tv_nsec - started.tv_nsec) / 1e9;
    fprintf(streaming ? stderr : stdout, "{\"hardware_encode_completed\":true,\"camera_opened\":false,\"width\":%u,\"height\":%u,\"frames\":%u,\"bytes\":%zu,\"seconds\":%.6f,\"fps\":%.2f}\n", width, height, fed, state.length, seconds, fed / seconds);
    result = 0;
cleanup:
    if (allocated) { fprintf(stderr, "VENC fatal: process-exit resource cleanup\n"); fflush(NULL); _exit(1); }
    if (component && component->ComponentDeInit(component) != OMX_ErrorNone) result = 1;
    /* Keep vendor DSOs resident until process exit after explicit OMX teardown. */
    if (output_fd >= 0) close(output_fd);
    free(state.bytes);
    free(raw);
    free(scratch);
    alarm(0);
    fflush(NULL);
    _exit(result);
}

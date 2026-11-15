#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <media/openmax/OMX_Component.h>
#include <media/openmax/OMX_Core.h>
#include <media/openmax/OMX_Video.h>

#ifdef TATER_CRONOS_ABI
#include "cronos_linear_rgba.h"
#endif

#ifdef TATER_NATIVE_BUFFER_SUPPORT
#include "omx_native_buffer_android.h"
#endif

#define AVC_COMPONENT_NAME "OMX.MTK.VIDEO.DECODER.AVC"
#define OMX_CALL_TIMEOUT_SECONDS 10U
#define MAX_VIDEO_PORTS 8U
#define MAX_PORT_BUFFERS 32U
#define MAX_DECODE_FRAMES 120U
#define MAX_FIXTURE_BYTES (64U * 1024U * 1024U)
#define MAX_DECODE_BYTES (64U * 1024U * 1024U)
#define MAX_DECODE_WIDTH 1920U
#define MAX_DECODE_HEIGHT 1088U
#define MAX_STREAM_PACKET_BYTES (4U * 1024U * 1024U)
#define TATER_STREAM_V2_HEADER_BYTES 32U
#define TATER_STREAM_V2_RECORD_BYTES 16U
#define TATER_STREAM_V2_FLAG_EOS 1U
#define VAL_MEM_CODEC_FOR_VDEC 1U
#define VAL_MEM_TYPE_FOR_HW_CACHEABLE 1U
#define VAL_MEM_ALIGN_64 (1U << 6)
#ifdef TATER_NATIVE_BUFFER_SUPPORT
#define TATER_HAL_PIXEL_FORMAT_YV12 0x32315659
#define TATER_GRALLOC_USAGE_SW_READ_OFTEN 3U
#define TATER_MTK_COLOR_FORMAT_NATIVE ((OMX_COLOR_FORMATTYPE)0x7f000001U)

struct enable_android_native_buffers {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_BOOL enable;
};

struct get_android_native_buffer_usage {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_U32 nUsage;
};
#endif

union val_word64 {
    unsigned long word;
    uint64_t extended;
};

union val_pointer64 {
    void *pointer;
    uint64_t extended;
};

struct val_memory {
    uint32_t memory_type;
    union val_word64 memory_size;
    union val_pointer64 memory_va;
    union val_pointer64 memory_pa;
    uint32_t alignment;
    union val_pointer64 aligned_memory_va;
    union val_pointer64 aligned_memory_pa;
    uint32_t memory_codec;
    uint32_t ion_share_fd;
    union val_pointer64 ion_buffer_handle;
    union val_pointer64 reserved;
    union val_word64 reserved_size;
};

typedef int32_t (*invalidate_cache_fn)(struct val_memory *, uint32_t,
                                       uint32_t);

_Static_assert(sizeof(struct val_memory) == 88,
               "unexpected public VAL_MEMORY_T ABI size");

struct callback_state {
    uint32_t events;
    uint32_t empty_done;
    uint32_t fill_done;
    volatile uint32_t state_completions;
    volatile OMX_STATETYPE completed_state;
    volatile uint32_t command_completions;
    volatile OMX_COMMANDTYPE completed_command;
    volatile OMX_U32 completed_command_data;
    volatile OMX_ERRORTYPE error;
    volatile uint32_t port_settings_changed;
    volatile uint32_t eos_events;
    OMX_BUFFERHEADERTYPE *returned_inputs[MAX_DECODE_FRAMES];
    uint8_t *decoded_frames;
    size_t decoded_frame_bytes;
    uint32_t decoded_width;
    uint32_t decoded_height;
    uint32_t expected_frames;
    uint32_t copied_frames;
    uint32_t output_flags[MAX_DECODE_FRAMES];
    int64_t output_timestamps[MAX_DECODE_FRAMES];
    volatile uint32_t recycle_outputs;
    volatile uint32_t recycled_outputs;
    invalidate_cache_fn invalidate_cache;
    volatile uint32_t cache_invalidations;
    volatile uint32_t cache_invalidation_failures;
    volatile uint32_t msync_failures;
    volatile int first_msync_errno;
    volatile uint32_t max_black_mismatches_at_offset0;
    volatile uint32_t max_black_mismatches_at_offset32;
    uint32_t first_output_alloc_len;
    uint32_t first_output_filled_len;
    uint32_t first_output_offset;
    int stream_output;
    uint32_t stream_version;
    uint32_t stream_fps;
    volatile uint32_t stream_header_written;
    int stream_output_fd;
    volatile uint32_t stream_write_failures;
#ifdef TATER_CRONOS_ABI
    int linear_rgba_output;
#endif
#ifdef TATER_NATIVE_BUFFER_SUPPORT
    int native_output;
    int native_rgba_output;
    struct tater_native_buffer *native_buffers[MAX_PORT_BUFFERS];
    OMX_BUFFERHEADERTYPE *native_headers[MAX_PORT_BUFFERS];
    uint32_t native_buffer_count;
#endif
};

typedef OMX_COMPONENTTYPE *(*create_component_fn)(OMX_STRING);

#if defined(TATER_NATIVE_BUFFER_SUPPORT) || defined(TATER_CRONOS_ABI)
static int write_exact_fd(int fd, const uint8_t *contents, size_t size) {
    size_t written = 0;

    while (written < size) {
        ssize_t result = write(fd, contents + written, size - written);

        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd ready;
            int poll_result;

            memset(&ready, 0, sizeof(ready));
            ready.fd = fd;
            ready.events = POLLOUT;
            do {
                poll_result = poll(&ready, 1, 10000);
            } while (poll_result < 0 && errno == EINTR);
            if (poll_result > 0 && (ready.revents & POLLOUT) != 0)
                continue;
        }
        return -1;
    }
    return 0;
}

static void store_le32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value & 0xffU);
    destination[1] = (uint8_t)((value >> 8) & 0xffU);
    destination[2] = (uint8_t)((value >> 16) & 0xffU);
    destination[3] = (uint8_t)((value >> 24) & 0xffU);
}

static void store_le64(uint8_t *destination, uint64_t value) {
    uint32_t byte_index;

    for (byte_index = 0; byte_index < 8U; ++byte_index)
        destination[byte_index] =
            (uint8_t)((value >> (byte_index * 8U)) & 0xffU);
}

static int write_stream_v2_header(struct callback_state *state) {
    uint8_t header[TATER_STREAM_V2_HEADER_BYTES] = {0};

    if (state == NULL || state->stream_version != 2U ||
        state->decoded_width == 0 || state->decoded_height == 0 ||
        state->decoded_frame_bytes > UINT32_MAX || state->stream_fps == 0)
        return -1;
    memcpy(header, "TVO2", 4U);
    store_le32(header + 4U, TATER_STREAM_V2_HEADER_BYTES);
    store_le32(header + 8U, state->decoded_width);
    store_le32(header + 12U, state->decoded_height);
    store_le32(header + 16U, (uint32_t)state->decoded_frame_bytes);
    memcpy(header + 20U, "RGBA", 4U);
    store_le32(header + 24U, state->stream_fps);
    store_le32(header + 28U, 1U);
    if (write_exact_fd(state->stream_output_fd, header, sizeof(header)) != 0)
        return -1;
    state->stream_header_written = 1U;
    return 0;
}

static int write_stream_v2_record(struct callback_state *state,
                                  const uint8_t *frame, uint32_t frame_bytes,
                                  uint32_t flags, int64_t pts_us) {
    uint8_t record[TATER_STREAM_V2_RECORD_BYTES] = {0};

    if (state == NULL || state->stream_header_written == 0)
        return -1;
    store_le32(record, frame_bytes);
    store_le32(record + 4U, flags);
    store_le64(record + 8U, (uint64_t)pts_us);
    if (write_exact_fd(state->stream_output_fd, record, sizeof(record)) != 0)
        return -1;
    if (frame_bytes != 0 &&
        (frame == NULL || write_exact_fd(state->stream_output_fd, frame,
                                         frame_bytes) != 0))
        return -1;
    return 0;
}
#endif

static int read_exact_or_eof(int fd, uint8_t *contents, size_t size,
                             int *clean_eof) {
    size_t received = 0;

    *clean_eof = 0;
    while (received < size) {
        ssize_t result = read(fd, contents + received, size - received);

        if (result > 0) {
            received += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        if (result == 0 && received == 0) {
            *clean_eof = 1;
            return 0;
        }
        return -1;
    }
    return 0;
}

/* Each streaming input record is a little-endian uint32 length followed by
 * one complete Annex-B access unit. A zero-length record or clean EOF ends the
 * stream. The one-packet lookahead marks the actual final access unit EOS. */
static int read_stream_packet(int fd, uint32_t stream_version,
                              uint8_t **packet, size_t *packet_size,
                              int64_t *pts_us) {
    uint8_t encoded_size[4];
    uint8_t encoded_pts[8];
    uint32_t size;
    uint64_t pts = 0;
    uint32_t byte_index;
    int clean_eof;

    *packet = NULL;
    *packet_size = 0;
    *pts_us = 0;
    if (read_exact_or_eof(fd, encoded_size, sizeof(encoded_size),
                          &clean_eof) != 0)
        return -1;
    if (clean_eof)
        return 0;
    size = (uint32_t)encoded_size[0] |
        (uint32_t)encoded_size[1] << 8 |
        (uint32_t)encoded_size[2] << 16 |
        (uint32_t)encoded_size[3] << 24;
    if (stream_version == 2U) {
        if (read_exact_or_eof(fd, encoded_pts, sizeof(encoded_pts),
                              &clean_eof) != 0 || clean_eof)
            return -1;
        for (byte_index = 0; byte_index < 8U; ++byte_index)
            pts |= (uint64_t)encoded_pts[byte_index] << (byte_index * 8U);
        if (pts > INT64_MAX)
            return -1;
        *pts_us = (int64_t)pts;
    }
    if (size == 0)
        return 0;
    if (size > MAX_STREAM_PACKET_BYTES)
        return -1;
    *packet = malloc(size);
    if (*packet == NULL)
        return -1;
    if (read_exact_or_eof(fd, *packet, size, &clean_eof) != 0 ||
        clean_eof) {
        free(*packet);
        *packet = NULL;
        return -1;
    }
    *packet_size = size;
    return 1;
}

static void initialize_header(void *structure, size_t size) {
#ifdef TATER_CRONOS_ABI
    OMX_U32 *words = structure;
    memset(structure, 0, size);
    words[0] = (OMX_U32)size;
    words[1] = 1; /* Independently observed Cronos parameter-version ABI. */
#else
    OMX_U32 *words = structure;
    OMX_VERSIONTYPE *version;

    memset(structure, 0, size);
    words[0] = (OMX_U32)size;
    version = (OMX_VERSIONTYPE *)&words[1];
    version->s.nVersionMajor = 1;
    version->s.nVersionMinor = 1;
    version->s.nRevision = 2;
    version->s.nStep = 0;
#endif
}

static OMX_ERRORTYPE event_handler(
    OMX_HANDLETYPE component, OMX_PTR application_data, OMX_EVENTTYPE event,
    OMX_U32 data1, OMX_U32 data2, OMX_PTR event_data) {
    struct callback_state *state = application_data;

    (void)component;
    (void)event_data;
    if (state != NULL) {
        ++state->events;
        if (event == OMX_EventCmdComplete &&
            data1 <= (OMX_U32)OMX_CommandMarkBuffer) {
            state->completed_command = (OMX_COMMANDTYPE)data1;
            state->completed_command_data = data2;
            __sync_synchronize();
            ++state->command_completions;
            if (data1 == (OMX_U32)OMX_CommandStateSet) {
                state->completed_state = (OMX_STATETYPE)data2;
                __sync_synchronize();
                ++state->state_completions;
            }
        } else if (event == OMX_EventError) {
            state->error = (OMX_ERRORTYPE)data1;
        } else if (event == OMX_EventPortSettingsChanged) {
            ++state->port_settings_changed;
        } else if (event == OMX_EventBufferFlag &&
                   (data2 & OMX_BUFFERFLAG_EOS) != 0) {
            ++state->eos_events;
        }
    }
    return OMX_ErrorNone;
}

static int wait_for_command(struct callback_state *state,
                            uint32_t completions_before,
                            OMX_COMMANDTYPE expected_command,
                            OMX_U32 expected_data) {
    unsigned int iteration;

    for (iteration = 0; iteration < 1000U; ++iteration) {
        __sync_synchronize();
        if (state->error != OMX_ErrorNone) {
            fprintf(stderr, "OMX asynchronous error: 0x%08x\n",
                    (unsigned int)state->error);
            return -1;
        }
        if (state->command_completions > completions_before &&
            state->completed_command == expected_command &&
            state->completed_command_data == expected_data)
            return 0;
        usleep(10000);
    }
    fprintf(stderr, "timed out waiting for OMX command %u data %u\n",
            (unsigned int)expected_command, (unsigned int)expected_data);
    return -1;
}

static int wait_for_state(struct callback_state *state,
                          uint32_t completions_before,
                          OMX_STATETYPE expected_state) {
    unsigned int iteration;

    for (iteration = 0; iteration < 1000U; ++iteration) {
        __sync_synchronize();
        if (state->error != OMX_ErrorNone) {
            fprintf(stderr, "OMX asynchronous error: 0x%08x\n",
                    (unsigned int)state->error);
            return -1;
        }
        if (state->state_completions > completions_before &&
            state->completed_state == expected_state)
            return 0;
        usleep(10000);
    }
    fprintf(stderr, "timed out waiting for OMX state %u\n",
            (unsigned int)expected_state);
    return -1;
}

static OMX_ERRORTYPE empty_buffer_done(
    OMX_HANDLETYPE component, OMX_PTR application_data,
    OMX_BUFFERHEADERTYPE *buffer) {
    struct callback_state *state = application_data;

    (void)component;
    if (state != NULL) {
        uint32_t slot = state->empty_done % MAX_DECODE_FRAMES;

        state->returned_inputs[slot] = buffer;
        __sync_synchronize();
        ++state->empty_done;
    }
    return OMX_ErrorNone;
}

#ifdef TATER_CRONOS_ABI
static void copy_linear_rgba(struct callback_state *state,
                             OMX_BUFFERHEADERTYPE *buffer) {
    size_t yuv_bytes = (size_t)state->decoded_width * state->decoded_height * 3U / 2U;
    uint32_t slot = state->copied_frames;
    uint8_t *destination = state->decoded_frames;
    struct val_memory memory;
    if (buffer == NULL || buffer->nFilledLen == 0 || !state->recycle_outputs)
        return;
    if (!destination || (!state->stream_output && slot >= state->expected_frames) ||
        !buffer->pBuffer || buffer->nOffset > buffer->nAllocLen ||
        yuv_bytes > buffer->nAllocLen - buffer->nOffset ||
        buffer->nFilledLen < yuv_bytes || !state->invalidate_cache) {
        state->error = OMX_ErrorBadParameter;
        return;
    }
    memset(&memory, 0, sizeof(memory));
    memory.memory_type = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
    memory.memory_size.extended = yuv_bytes;
    memory.memory_va.pointer = buffer->pBuffer + buffer->nOffset;
    memory.alignment = VAL_MEM_ALIGN_64;
    memory.aligned_memory_va.pointer = memory.memory_va.pointer;
    memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;
    memory.ion_share_fd = UINT32_MAX;
    ++state->cache_invalidations;
    if (state->invalidate_cache(&memory, sizeof(memory), 1) != 0) {
        ++state->cache_invalidation_failures;
        state->error = OMX_ErrorUndefined;
        return;
    }
    if (!state->stream_output)
        destination += (size_t)slot * state->decoded_frame_bytes;
    if (cronos_i420_rgba(buffer->pBuffer + buffer->nOffset, yuv_bytes,
                         state->decoded_width, state->decoded_height,
                         destination, state->decoded_frame_bytes) != 0) {
        state->error = OMX_ErrorBadParameter;
        return;
    }
    if (slot < MAX_DECODE_FRAMES) {
        state->output_flags[slot] = buffer->nFlags;
        state->output_timestamps[slot] = buffer->nTimeStamp;
    }
    if (slot == 0) {
        state->first_output_alloc_len = buffer->nAllocLen;
        state->first_output_filled_len = buffer->nFilledLen;
        state->first_output_offset = buffer->nOffset;
    }
    if (state->stream_output &&
        (state->stream_version == 2U ?
         write_stream_v2_record(state, destination, (uint32_t)state->decoded_frame_bytes,
                                 0, buffer->nTimeStamp) :
         write_exact_fd(state->stream_output_fd, destination,
                         state->decoded_frame_bytes)) != 0) {
        ++state->stream_write_failures;
        state->error = OMX_ErrorUndefined;
        return;
    }
    __sync_synchronize();
    ++state->copied_frames;
}
#endif

static OMX_ERRORTYPE fill_buffer_done(
    OMX_HANDLETYPE component, OMX_PTR application_data,
    OMX_BUFFERHEADERTYPE *buffer) {
    struct callback_state *state = application_data;

    (void)component;
    if (state != NULL) {
        uint32_t slot = state->copied_frames;
        int copied = 0;

#ifdef TATER_CRONOS_ABI
        if (state->linear_rgba_output) {
            copy_linear_rgba(state, buffer);
            goto output_done;
        }
#endif

#ifdef TATER_NATIVE_BUFFER_SUPPORT
        if (buffer != NULL && buffer->nFilledLen != 0 &&
            state->native_output != 0 &&
            state->recycle_outputs != 0 &&
            state->decoded_frames != NULL &&
            (state->stream_output != 0 ||
             slot < state->expected_frames)) {
            uint32_t native_index;

            for (native_index = 0;
                 native_index < state->native_buffer_count;
                 ++native_index) {
                if (state->native_headers[native_index] == buffer)
                    break;
            }
            if (native_index < state->native_buffer_count) {
                size_t luma_bytes =
                    state->decoded_frame_bytes * 2U / 3U;
                int copy_result = -1;

                if (state->decoded_width != 0 &&
                    state->decoded_height != 0) {
                    uint8_t *destination = state->decoded_frames;

                    if (state->stream_output == 0)
                        destination +=
                            (size_t)slot * state->decoded_frame_bytes;

                    if (state->native_rgba_output != 0)
                        copy_result =
                            tater_native_buffer_copy_mtk_rgba8888(
                                state->native_buffers[native_index],
                                state->decoded_width,
                                state->decoded_height, destination,
                                state->decoded_frame_bytes);
                    else
                        copy_result =
                            tater_native_buffer_copy_mtk_yuv420p(
                                state->native_buffers[native_index],
                                state->decoded_width,
                                state->decoded_height, destination,
                                state->decoded_frame_bytes);
                }
                if (copy_result == 0) {
                    size_t pixel_index;
                    uint32_t mismatch = 0;
                    uint8_t *frame = state->decoded_frames;

                    if (state->stream_output == 0)
                        frame +=
                            (size_t)slot * state->decoded_frame_bytes;

                    if (state->native_rgba_output == 0) {
                        for (pixel_index = 0;
                             pixel_index < state->decoded_frame_bytes;
                             ++pixel_index) {
                            uint8_t expected =
                                pixel_index < luma_bytes ? 16U : 128U;

                            if (frame[pixel_index] != expected)
                                ++mismatch;
                        }
                    }
                    if (mismatch > state->max_black_mismatches_at_offset0)
                        state->max_black_mismatches_at_offset0 = mismatch;
                    if (slot < MAX_DECODE_FRAMES) {
                        state->output_flags[slot] = buffer->nFlags;
                        state->output_timestamps[slot] =
                            buffer->nTimeStamp;
                    }
                    if (slot == 0) {
                        state->first_output_alloc_len = buffer->nAllocLen;
                        state->first_output_filled_len = buffer->nFilledLen;
                        state->first_output_offset = buffer->nOffset;
                    }
                    if (state->stream_output != 0 &&
                        ((state->stream_version == 2U &&
                          write_stream_v2_record(
                              state, frame,
                              (uint32_t)state->decoded_frame_bytes, 0U,
                              buffer->nTimeStamp) != 0) ||
                         (state->stream_version != 2U &&
                          write_exact_fd(state->stream_output_fd, frame,
                                         state->decoded_frame_bytes) != 0))) {
                        ++state->stream_write_failures;
                        state->error = OMX_ErrorUndefined;
                    } else {
                        __sync_synchronize();
                        ++state->copied_frames;
                        copied = 1;
                    }
                }
            }
        }
#endif
        if (!copied && buffer != NULL && buffer->nFilledLen != 0 &&
            state->decoded_frames != NULL &&
#ifdef TATER_NATIVE_BUFFER_SUPPORT
            state->native_output == 0 &&
#endif
            slot < state->expected_frames &&
            buffer->nOffset <= buffer->nAllocLen &&
            state->decoded_frame_bytes <=
                buffer->nAllocLen - buffer->nOffset &&
            buffer->nFilledLen >= state->decoded_frame_bytes) {
            if (state->invalidate_cache != NULL) {
                struct val_memory memory;
                int32_t invalidate_result;

                memset(&memory, 0, sizeof(memory));
                memory.memory_type = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
                memory.memory_size.extended =
                    state->decoded_frame_bytes;
                memory.memory_va.pointer =
                    buffer->pBuffer + buffer->nOffset;
                memory.alignment = VAL_MEM_ALIGN_64;
                memory.aligned_memory_va.pointer =
                    buffer->pBuffer + buffer->nOffset;
                memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;
                memory.ion_share_fd = UINT32_MAX;
                invalidate_result = state->invalidate_cache(
                    &memory, sizeof(memory), 1);
                ++state->cache_invalidations;
                if (invalidate_result != 0)
                    ++state->cache_invalidation_failures;
            }
            {
                long page_size = sysconf(_SC_PAGESIZE);
                uintptr_t buffer_start = (uintptr_t)buffer->pBuffer;
                uintptr_t page_start;
                uintptr_t page_end;

                if (page_size > 0) {
                    page_start = buffer_start &
                                 ~((uintptr_t)page_size - 1U);
                    page_end =
                        (buffer_start + buffer->nAllocLen +
                         (uintptr_t)page_size - 1U) &
                        ~((uintptr_t)page_size - 1U);
                    if (msync((void *)page_start,
                              page_end - page_start,
                              MS_SYNC | MS_INVALIDATE) != 0) {
                        if (state->msync_failures == 0)
                            state->first_msync_errno = errno;
                        ++state->msync_failures;
                    }
                }
            }
            {
                size_t pixel_index;
                size_t luma_bytes =
                    state->decoded_frame_bytes * 2U / 3U;
                uint32_t mismatch0 = 0;
                uint32_t mismatch32 = 0;

                for (pixel_index = 0;
                     pixel_index < state->decoded_frame_bytes;
                     ++pixel_index) {
                    uint8_t expected =
                        pixel_index < luma_bytes ? 16U : 128U;

                    if (buffer->pBuffer[buffer->nOffset + pixel_index] !=
                        expected)
                        ++mismatch0;
                    if (buffer->nOffset + 32U +
                            state->decoded_frame_bytes <=
                        buffer->nAllocLen &&
                        buffer->pBuffer[buffer->nOffset + 32U +
                                        pixel_index] != expected)
                        ++mismatch32;
                }
                if (mismatch0 >
                    state->max_black_mismatches_at_offset0)
                    state->max_black_mismatches_at_offset0 = mismatch0;
                if (mismatch32 >
                    state->max_black_mismatches_at_offset32)
                    state->max_black_mismatches_at_offset32 = mismatch32;
                if (slot == 0) {
                    state->first_output_alloc_len = buffer->nAllocLen;
                    state->first_output_filled_len = buffer->nFilledLen;
                    state->first_output_offset = buffer->nOffset;
                }
            }
            memcpy(state->decoded_frames +
                       (size_t)slot * state->decoded_frame_bytes,
                   buffer->pBuffer + buffer->nOffset,
                   state->decoded_frame_bytes);
            state->output_flags[slot] = buffer->nFlags;
            state->output_timestamps[slot] = buffer->nTimeStamp;
            __sync_synchronize();
            ++state->copied_frames;
            copied = 1;
        }
#ifdef TATER_CRONOS_ABI
output_done:
#endif
        ++state->fill_done;
        if (buffer != NULL && state->recycle_outputs != 0 &&
            (state->stream_output != 0 ||
             state->copied_frames < state->expected_frames) &&
            state->error == OMX_ErrorNone &&
            (buffer->nFlags & OMX_BUFFERFLAG_EOS) == 0) {
            OMX_COMPONENTTYPE *omx_component = component;
            OMX_ERRORTYPE recycle_result;

            buffer->nOffset = 0;
            buffer->nFilledLen = 0;
            buffer->nFlags = 0;
            recycle_result = omx_component->FillThisBuffer(
                omx_component, buffer);
            if (recycle_result == OMX_ErrorNone)
                ++state->recycled_outputs;
            else
                state->error = recycle_result;
        }
    }
    return OMX_ErrorNone;
}

static int read_file(const char *path, uint8_t **contents, size_t *size) {
    FILE *stream;
    long length;
    uint8_t *buffer;

    *contents = NULL;
    *size = 0;
    stream = fopen(path, "rb");
    if (stream == NULL || fseek(stream, 0, SEEK_END) != 0) {
        if (stream != NULL)
            fclose(stream);
        return -1;
    }
    length = ftell(stream);
    if (length <= 0 || (unsigned long)length > MAX_FIXTURE_BYTES ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return -1;
    }
    buffer = malloc((size_t)length);
    if (buffer == NULL ||
        fread(buffer, 1, (size_t)length, stream) != (size_t)length) {
        free(buffer);
        fclose(stream);
        return -1;
    }
    fclose(stream);
    *contents = buffer;
    *size = (size_t)length;
    return 0;
}

static int find_access_units(const uint8_t *stream, size_t size,
                             size_t *starts, size_t *ends,
                             uint32_t expected) {
    size_t offset = 0;
    uint32_t count = 0;

    while (offset + 4U < size) {
        size_t start_code_size = 0;
        uint8_t nal_type;

        if (stream[offset] == 0 && stream[offset + 1U] == 0 &&
            stream[offset + 2U] == 1)
            start_code_size = 3U;
        else if (offset + 5U < size && stream[offset] == 0 &&
                 stream[offset + 1U] == 0 && stream[offset + 2U] == 0 &&
                 stream[offset + 3U] == 1)
            start_code_size = 4U;
        if (start_code_size == 0) {
            ++offset;
            continue;
        }
        nal_type = stream[offset + start_code_size] & 0x1fU;
        if (nal_type == 9U) {
            if (count >= expected)
                return -1;
            if (count != 0)
                ends[count - 1U] = offset;
            starts[count++] = offset;
        }
        offset += start_code_size + 1U;
    }
    if (count != expected)
        return -1;
    ends[count - 1U] = size;
    return 0;
}

static int access_unit_has_vcl(const uint8_t *stream, size_t size) {
    size_t offset = 0;

    while (offset + 4U < size) {
        size_t start_code_size = 0;
        uint8_t nal_type;

        if (stream[offset] == 0 && stream[offset + 1U] == 0 &&
            stream[offset + 2U] == 1)
            start_code_size = 3U;
        else if (offset + 5U < size && stream[offset] == 0 &&
                 stream[offset + 1U] == 0 &&
                 stream[offset + 2U] == 0 &&
                 stream[offset + 3U] == 1)
            start_code_size = 4U;
        if (start_code_size == 0) {
            ++offset;
            continue;
        }
        nal_type = stream[offset + start_code_size] & 0x1fU;
        if (nal_type >= 1U && nal_type <= 5U)
            return 1;
        offset += start_code_size + 1U;
    }
    return 0;
}

static int write_file(const char *path, const uint8_t *contents,
                      size_t size) {
    FILE *stream = fopen(path, "wb");
    int failed = 0;

    if (stream == NULL)
        return -1;
    if (fwrite(contents, 1, size, stream) != size || fflush(stream) != 0)
        failed = 1;
    if (fclose(stream) != 0)
        failed = 1;
    return failed ? -1 : 0;
}

static int print_port_definition(OMX_COMPONENTTYPE *component,
                                 OMX_U32 port_index, unsigned int ordinal) {
    OMX_PARAM_PORTDEFINITIONTYPE definition;
    OMX_ERRORTYPE omx_result;

    initialize_header(&definition, sizeof(definition));
    definition.nPortIndex = port_index;
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->GetParameter(
        component, OMX_IndexParamPortDefinition, &definition);
    alarm(0);
    if (omx_result != OMX_ErrorNone) {
        fprintf(stderr,
                "OMX port-definition query failed: port=%u result=0x%08x\n",
                (unsigned int)port_index, (unsigned int)omx_result);
        return -1;
    }
    if (definition.eDomain != OMX_PortDomainVideo ||
        definition.nBufferCountMin == 0 || definition.nBufferSize == 0) {
        fprintf(stderr, "OMX returned an invalid video port definition\n");
        return -1;
    }

    if (ordinal != 0)
        putchar(',');
    printf("{\"index\":%u,\"direction\":%u,"
           "\"buffer_count_min\":%u,\"buffer_count_actual\":%u,"
           "\"buffer_size\":%u,\"enabled\":%u,\"populated\":%u,"
           "\"frame_width\":%u,\"frame_height\":%u,"
           "\"stride\":%d,\"slice_height\":%u,"
           "\"compression_format\":%u,\"color_format\":%u}",
           (unsigned int)definition.nPortIndex,
           (unsigned int)definition.eDir,
           (unsigned int)definition.nBufferCountMin,
           (unsigned int)definition.nBufferCountActual,
           (unsigned int)definition.nBufferSize,
           (unsigned int)definition.bEnabled,
           (unsigned int)definition.bPopulated,
           (unsigned int)definition.format.video.nFrameWidth,
           (unsigned int)definition.format.video.nFrameHeight,
           (int)definition.format.video.nStride,
           (unsigned int)definition.format.video.nSliceHeight,
           (unsigned int)definition.format.video.eCompressionFormat,
           (unsigned int)definition.format.video.eColorFormat);
    return 0;
}

#ifdef TATER_NATIVE_BUFFER_SUPPORT
static int allocate_native_output_buffers(
    OMX_COMPONENTTYPE *component, struct callback_state *state,
    const OMX_PARAM_PORTDEFINITIONTYPE *definition,
    OMX_INDEXTYPE use_native_buffer_index, OMX_U32 native_usage,
    OMX_BUFFERHEADERTYPE **buffers, OMX_U32 *buffer_count) {
    OMX_U32 buffer_index;

    state->native_buffer_count = 0;
    for (buffer_index = 0;
         buffer_index < definition->nBufferCountActual; ++buffer_index) {
        OMX_ERRORTYPE omx_result;
        struct tater_native_buffer *native_buffer = NULL;

        if (tater_native_buffer_create(
                definition->format.video.nFrameWidth,
                definition->format.video.nFrameHeight,
                TATER_HAL_PIXEL_FORMAT_YV12,
                native_usage | TATER_GRALLOC_USAGE_SW_READ_OFTEN,
                &native_buffer) != 0) {
            fprintf(stderr,
                    "native output allocation failed: index=%u\n",
                    (unsigned int)buffer_index);
            return -1;
        }
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = tater_omx_use_native_buffer(
            component, use_native_buffer_index, definition->nPortIndex,
            NULL, &buffers[buffer_index], native_buffer);
        alarm(0);
        if (omx_result != OMX_ErrorNone ||
            buffers[buffer_index] == NULL) {
            fprintf(stderr,
                    "OMX native output registration failed: index=%u "
                    "result=0x%08x\n",
                    (unsigned int)buffer_index,
                    (unsigned int)omx_result);
            tater_native_buffer_destroy(native_buffer);
            return -1;
        }
        state->native_buffers[buffer_index] = native_buffer;
        state->native_headers[buffer_index] = buffers[buffer_index];
        ++state->native_buffer_count;
        ++*buffer_count;
    }
    return 0;
}

static void forget_native_output_buffers(struct callback_state *state) {
    uint32_t buffer_index;

    for (buffer_index = 0;
         buffer_index < state->native_buffer_count; ++buffer_index) {
        tater_native_buffer_destroy(state->native_buffers[buffer_index]);
        state->native_buffers[buffer_index] = NULL;
        state->native_headers[buffer_index] = NULL;
    }
    state->native_buffer_count = 0;
}

static int rebuild_native_output_port(
    OMX_COMPONENTTYPE *component, struct callback_state *state,
    OMX_PARAM_PORTDEFINITIONTYPE *definition,
    OMX_BUFFERHEADERTYPE **buffers, OMX_U32 *buffer_count,
    OMX_U32 expected_width, OMX_U32 expected_height,
    OMX_INDEXTYPE use_native_buffer_index, OMX_U32 native_usage) {
    OMX_ERRORTYPE omx_result;
    uint32_t command_completions_before = state->command_completions;
    uint32_t fills_before = state->fill_done;
    OMX_U32 old_buffer_count = *buffer_count;
    OMX_U32 port_index = definition->nPortIndex;
    OMX_U32 buffer_index;
    unsigned int wait_iteration;

    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->SendCommand(
        component, OMX_CommandPortDisable, port_index, NULL);
    alarm(0);
    if (omx_result != OMX_ErrorNone)
        return -1;
    for (wait_iteration = 0; wait_iteration < 1000U; ++wait_iteration) {
        __sync_synchronize();
        if (state->error != OMX_ErrorNone ||
            state->fill_done >= fills_before + old_buffer_count)
            break;
        usleep(10000);
    }
    if (state->error != OMX_ErrorNone ||
        state->fill_done < fills_before + old_buffer_count)
        return -1;
    for (buffer_index = 0; buffer_index < old_buffer_count;
         ++buffer_index) {
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->FreeBuffer(
            component, port_index, buffers[buffer_index]);
        alarm(0);
        if (omx_result != OMX_ErrorNone)
            return -1;
        buffers[buffer_index] = NULL;
    }
    forget_native_output_buffers(state);
    *buffer_count = 0;
    if (wait_for_command(state, command_completions_before,
                         OMX_CommandPortDisable, port_index) != 0)
        return -1;

    initialize_header(definition, sizeof(*definition));
    definition->nPortIndex = port_index;
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->GetParameter(
        component, OMX_IndexParamPortDefinition, definition);
    alarm(0);
    if (omx_result != OMX_ErrorNone ||
        definition->nBufferCountActual == 0 ||
        definition->nBufferCountActual > MAX_PORT_BUFFERS ||
        definition->format.video.nFrameWidth != expected_width ||
        definition->format.video.nFrameHeight != expected_height ||
        definition->format.video.eColorFormat !=
            TATER_MTK_COLOR_FORMAT_NATIVE) {
        fprintf(stderr,
                "native output reconfiguration contract invalid: "
                "result=0x%08x count=%u size=%ux%u color=%u\n",
                (unsigned int)omx_result,
                (unsigned int)definition->nBufferCountActual,
                (unsigned int)definition->format.video.nFrameWidth,
                (unsigned int)definition->format.video.nFrameHeight,
                (unsigned int)definition->format.video.eColorFormat);
        return -1;
    }

    command_completions_before = state->command_completions;
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->SendCommand(
        component, OMX_CommandPortEnable, port_index, NULL);
    alarm(0);
    if (omx_result != OMX_ErrorNone ||
        allocate_native_output_buffers(
            component, state, definition, use_native_buffer_index,
            native_usage, buffers, buffer_count) != 0)
        return -1;
    return wait_for_command(state, command_completions_before,
                            OMX_CommandPortEnable, port_index);
}
#endif

static int rebuild_output_port(
    OMX_COMPONENTTYPE *component, struct callback_state *state,
    OMX_PARAM_PORTDEFINITIONTYPE *definition,
    OMX_BUFFERHEADERTYPE **buffers, OMX_U32 *buffer_count,
    OMX_U32 expected_width, OMX_U32 expected_height,
    OMX_COLOR_FORMATTYPE expected_color) {
    OMX_ERRORTYPE omx_result;
    uint32_t command_completions_before = state->command_completions;
    uint32_t fills_before = state->fill_done;
    OMX_U32 old_buffer_count = *buffer_count;
    OMX_U32 port_index = definition->nPortIndex;
    OMX_U32 buffer_index;
    unsigned int wait_iteration;

    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->SendCommand(
        component, OMX_CommandPortDisable, definition->nPortIndex, NULL);
    alarm(0);
    if (omx_result != OMX_ErrorNone) {
        fprintf(stderr, "OMX output disable failed: 0x%08x\n",
                (unsigned int)omx_result);
        return -1;
    }
    for (wait_iteration = 0; wait_iteration < 1000U; ++wait_iteration) {
        __sync_synchronize();
        if (state->error != OMX_ErrorNone ||
            state->fill_done >= fills_before + old_buffer_count)
            break;
        usleep(10000);
    }
    if (state->error != OMX_ErrorNone ||
        state->fill_done < fills_before + old_buffer_count) {
        fprintf(stderr,
                "OMX did not return every output during port disable: "
                "%u/%u\n",
                state->fill_done - fills_before,
                (unsigned int)old_buffer_count);
        return -1;
    }
    for (buffer_index = 0; buffer_index < old_buffer_count; ++buffer_index) {
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->FreeBuffer(
            component, definition->nPortIndex, buffers[buffer_index]);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr,
                    "OMX output free during reconfigure failed: index=%u "
                    "result=0x%08x\n",
                    (unsigned int)buffer_index,
                    (unsigned int)omx_result);
            return -1;
        }
        buffers[buffer_index] = NULL;
    }
    *buffer_count = 0;
    if (wait_for_command(state, command_completions_before,
                         OMX_CommandPortDisable,
                         definition->nPortIndex) != 0)
        return -1;

    initialize_header(definition, sizeof(*definition));
    definition->nPortIndex = port_index;
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->GetParameter(
        component, OMX_IndexParamPortDefinition, definition);
    alarm(0);
    if (omx_result != OMX_ErrorNone ||
        definition->nBufferCountActual == 0 ||
        definition->nBufferCountActual > MAX_PORT_BUFFERS ||
        definition->format.video.nFrameWidth != expected_width ||
        definition->format.video.nFrameHeight != expected_height ||
        definition->format.video.eColorFormat != expected_color) {
        fprintf(stderr,
                "OMX output reconfiguration returned an invalid contract: "
                "result=0x%08x count=%u size=%ux%u color=%u\n",
                (unsigned int)omx_result,
                (unsigned int)definition->nBufferCountActual,
                (unsigned int)definition->format.video.nFrameWidth,
                (unsigned int)definition->format.video.nFrameHeight,
                (unsigned int)definition->format.video.eColorFormat);
        return -1;
    }
#ifdef TATER_CRONOS_ABI
    if (definition->format.video.nStride != (OMX_S32)expected_width ||
        definition->format.video.nSliceHeight != expected_height) {
        fprintf(stderr, "Cronos linear output requires exact stride/slice geometry\n");
        return -1;
    }
#endif

    command_completions_before = state->command_completions;
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->SendCommand(
        component, OMX_CommandPortEnable, definition->nPortIndex, NULL);
    alarm(0);
    if (omx_result != OMX_ErrorNone) {
        fprintf(stderr, "OMX output enable failed: 0x%08x\n",
                (unsigned int)omx_result);
        return -1;
    }
    for (buffer_index = 0;
         buffer_index < definition->nBufferCountActual; ++buffer_index) {
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->AllocateBuffer(
            component, &buffers[buffer_index], definition->nPortIndex,
            NULL, definition->nBufferSize);
        alarm(0);
        if (omx_result != OMX_ErrorNone || buffers[buffer_index] == NULL) {
            fprintf(stderr,
                    "OMX output reallocation failed: index=%u "
                    "result=0x%08x\n",
                    (unsigned int)buffer_index,
                    (unsigned int)omx_result);
            return -1;
        }
        ++*buffer_count;
    }
    if (wait_for_command(state, command_completions_before,
                         OMX_CommandPortEnable,
                         definition->nPortIndex) != 0)
        return -1;
    return 0;
}

int tater_omx_probe_main(int argc, char **argv) {
    void *library = NULL;
    void *utility_library = NULL;
    void *symbol;
    create_component_fn create_component = NULL;
    OMX_COMPONENTTYPE *component = NULL;
    OMX_CALLBACKTYPE callbacks;
    OMX_PORT_PARAM_TYPE video_ports;
    OMX_STATETYPE state = OMX_StateInvalid;
    OMX_ERRORTYPE omx_result;
    OMX_ERRORTYPE ion_extension_result = OMX_ErrorUnsupportedIndex;
    OMX_INDEXTYPE ion_extension_index = OMX_IndexMax;
#ifdef TATER_NATIVE_BUFFER_SUPPORT
    OMX_INDEXTYPE enable_native_buffer_index = OMX_IndexMax;
    OMX_INDEXTYPE use_native_buffer_index = OMX_IndexMax;
    OMX_INDEXTYPE native_buffer_usage_index = OMX_IndexMax;
    OMX_U32 native_buffer_usage = 0;
    int decode_native_stream = 0;
    int decode_native_rgba_stream = 0;
    int serve_native_rgba_stream = 0;
#endif
    int serve_native_rgba_stream_v2 = 0;
#ifdef TATER_CRONOS_ABI
    int linear_rgba_output = 0;
#endif
    struct callback_state callback_state = {0};
    OMX_U32 port_offset;
    unsigned int queried_ports = 0;
    OMX_PARAM_PORTDEFINITIONTYPE definitions[MAX_VIDEO_PORTS];
    OMX_BUFFERHEADERTYPE *buffers[MAX_VIDEO_PORTS][MAX_PORT_BUFFERS];
    OMX_U32 buffer_counts[MAX_VIDEO_PORTS] = {0};
    uint32_t completions_before;
    int exercise_buffers;
    int decode_stream;
    int buffers_allocated = 0;
    int loaded_after_buffers = 0;
    int input_port_offset = -1;
    int output_port_offset = -1;
    int decode_succeeded = 0;
    uint32_t fed_inputs = 0;
    int component_initialized = 0;
    int result = 1;
    uint8_t *fixture = NULL;
    uint8_t *stream_current_packet = NULL;
    uint8_t *stream_next_packet = NULL;
    size_t stream_current_size = 0;
    size_t stream_next_size = 0;
    int64_t stream_current_pts_us = 0;
    int64_t stream_next_pts_us = 0;
    size_t fixture_size = 0;
    uint8_t *decoded_frames = NULL;
    size_t decoded_frame_bytes = 0;
    size_t decoded_total_bytes = 0;
    size_t maximum_access_unit_bytes = 0;
    size_t access_unit_starts[MAX_DECODE_FRAMES];
    size_t access_unit_ends[MAX_DECODE_FRAMES];
    unsigned long requested_width = 0;
    unsigned long requested_height = 0;
    unsigned long requested_frames = 0;
    unsigned long requested_fps = 0;
    unsigned long stream_input_fd = 0;
    unsigned long stream_output_fd = 0;
    int streaming = 0;
    char *number_end = NULL;
    const char *output_path = NULL;

    decode_stream = argc > 1 &&
                    strcmp(argv[1], "--decode-avc-stream") == 0;
#ifdef TATER_CRONOS_ABI
    serve_native_rgba_stream_v2 = argc > 1 &&
        strcmp(argv[1], "--serve-avc-linear-rgba-stream-v2") == 0;
    streaming = serve_native_rgba_stream_v2 || (argc > 1 &&
        strcmp(argv[1], "--serve-avc-linear-rgba-stream") == 0);
    linear_rgba_output = streaming || (argc > 1 &&
        strcmp(argv[1], "--decode-avc-linear-rgba-stream") == 0);
    decode_stream = decode_stream || linear_rgba_output;
#endif
#ifdef TATER_NATIVE_BUFFER_SUPPORT
    decode_native_stream = argc > 1 &&
        strcmp(argv[1], "--decode-avc-native-stream") == 0;
    decode_native_rgba_stream = argc > 1 &&
        strcmp(argv[1], "--decode-avc-native-rgba-stream") == 0;
    serve_native_rgba_stream = argc > 1 &&
        strcmp(argv[1], "--serve-avc-native-rgba-stream") == 0;
    serve_native_rgba_stream_v2 = argc > 1 &&
        strcmp(argv[1], "--serve-avc-native-rgba-stream-v2") == 0;
    streaming = serve_native_rgba_stream || serve_native_rgba_stream_v2;
    decode_native_stream =
        decode_native_stream || decode_native_rgba_stream ||
        streaming;
    decode_stream = decode_stream || decode_native_stream;
#endif
    if ((!decode_stream && argc != 3) ||
        (decode_stream && argc != 8) ||
        (strcmp(argv[1], "--inspect-avc-component") != 0 &&
         strcmp(argv[1], "--exercise-avc-buffers") != 0 &&
         !decode_stream)) {
        fprintf(stderr,
                "refusing: use --inspect-avc-component COMPONENT_LIBRARY or "
                "--exercise-avc-buffers COMPONENT_LIBRARY or "
                "--decode-avc-stream COMPONENT_LIBRARY FIXTURE WIDTH HEIGHT "
                "FRAME_COUNT OUTPUT_YUV420P"
#ifdef TATER_NATIVE_BUFFER_SUPPORT
                " or --decode-avc-native-stream or "
                "--decode-avc-native-rgba-stream with the same arguments, "
                "or --serve-avc-native-rgba-stream COMPONENT_LIBRARY WIDTH "
                "HEIGHT FPS INPUT_FD OUTPUT_FD, or the v2 form with the "
                "same arguments"
#endif
                "\n");
        return 2;
    }
    exercise_buffers =
        strcmp(argv[1], "--exercise-avc-buffers") == 0 || decode_stream;
    if (decode_stream) {
        const char *width_argument = streaming ? argv[3] : argv[4];
        const char *height_argument = streaming ? argv[4] : argv[5];

        errno = 0;
        requested_width = strtoul(width_argument, &number_end, 10);
        if (errno != 0 || number_end == width_argument ||
            *number_end != '\0' ||
            requested_width < 16U ||
            requested_width > MAX_DECODE_WIDTH) {
            fprintf(stderr, "width must be between 16 and %u\n",
                    MAX_DECODE_WIDTH);
            return 2;
        }
        errno = 0;
        requested_height = strtoul(height_argument, &number_end, 10);
        if (errno != 0 || number_end == height_argument ||
            *number_end != '\0' ||
            requested_height < 16U ||
            requested_height > MAX_DECODE_HEIGHT) {
            fprintf(stderr, "height must be between 16 and %u\n",
                    MAX_DECODE_HEIGHT);
            return 2;
        }
        if ((requested_width & 1U) != 0 ||
            (requested_height & 1U) != 0) {
            fprintf(stderr, "YUV420 dimensions must be even\n");
            return 2;
        }
        if (streaming) {
            errno = 0;
            requested_fps = strtoul(argv[5], &number_end, 10);
            if (errno != 0 || number_end == argv[5] ||
                *number_end != '\0' || requested_fps < 1U ||
                requested_fps > 120U) {
                fprintf(stderr, "stream FPS must be between 1 and 120\n");
                return 2;
            }
            errno = 0;
            stream_input_fd = strtoul(argv[6], &number_end, 10);
            if (errno != 0 || number_end == argv[6] ||
                *number_end != '\0' || stream_input_fd > 1024U) {
                fprintf(stderr, "invalid stream input descriptor\n");
                return 2;
            }
            errno = 0;
            stream_output_fd = strtoul(argv[7], &number_end, 10);
            if (errno != 0 || number_end == argv[7] ||
                *number_end != '\0' || stream_output_fd > 1024U ||
                stream_output_fd == stream_input_fd) {
                fprintf(stderr, "invalid stream output descriptor\n");
                return 2;
            }
            {
                int descriptor_flags =
                    fcntl((int)stream_output_fd, F_GETFL, 0);

                if (descriptor_flags < 0 ||
                    fcntl((int)stream_output_fd, F_SETFL,
                          descriptor_flags | O_NONBLOCK) != 0 ||
                    signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
                    fprintf(stderr,
                            "could not bound the stream output descriptor\n");
                    return 1;
                }
            }
        } else {
            errno = 0;
            requested_frames = strtoul(argv[6], &number_end, 10);
            if (errno != 0 || number_end == argv[6] ||
                *number_end != '\0' ||
                requested_frames < 2U ||
                requested_frames > MAX_DECODE_FRAMES) {
                fprintf(stderr,
                        "frame count must be between 2 and %u\n",
                        MAX_DECODE_FRAMES);
                return 2;
            }
        }
#ifdef TATER_NATIVE_BUFFER_SUPPORT
        if (decode_native_rgba_stream || streaming)
            decoded_frame_bytes =
                (size_t)requested_width * requested_height * 4U;
        else
#endif
#ifdef TATER_CRONOS_ABI
        if (linear_rgba_output)
            decoded_frame_bytes =
                (size_t)requested_width * requested_height * 4U;
        else
#endif
            decoded_frame_bytes =
                (size_t)requested_width * requested_height * 3U / 2U;
        if (decoded_frame_bytes == 0 ||
            (!streaming && requested_frames >
                MAX_DECODE_BYTES / decoded_frame_bytes)) {
            fprintf(stderr, "decoded stream exceeds bounded output\n");
            return 2;
        }
        decoded_total_bytes = decoded_frame_bytes *
            (streaming ? 1U : requested_frames);
        if (streaming) {
            maximum_access_unit_bytes = MAX_STREAM_PACKET_BYTES;
        } else {
            if (read_file(argv[3], &fixture, &fixture_size) != 0 ||
                find_access_units(fixture, fixture_size,
                                  access_unit_starts,
                                  access_unit_ends,
                                  (uint32_t)requested_frames) != 0) {
                fprintf(stderr,
                        "fixture must contain exactly the requested "
                        "AUD-framed access units\n");
                free(fixture);
                return 2;
            }
            {
                uint32_t access_unit_index;

                for (access_unit_index = 0;
                     access_unit_index < requested_frames;
                     ++access_unit_index) {
                    size_t access_unit_bytes =
                        access_unit_ends[access_unit_index] -
                        access_unit_starts[access_unit_index];

                    if (access_unit_bytes > maximum_access_unit_bytes)
                        maximum_access_unit_bytes = access_unit_bytes;
                }
            }
        }
        decoded_frames = malloc(decoded_total_bytes);
        if (decoded_frames == NULL) {
            fprintf(stderr, "could not allocate bounded decoded output\n");
            free(fixture);
            free(decoded_frames);
            return 1;
        }
        memset(decoded_frames, 0xa5, decoded_total_bytes);
        callback_state.decoded_frames = decoded_frames;
        callback_state.decoded_frame_bytes = decoded_frame_bytes;
        callback_state.decoded_width = (uint32_t)requested_width;
        callback_state.decoded_height = (uint32_t)requested_height;
        callback_state.expected_frames =
            streaming ? UINT32_MAX : (uint32_t)requested_frames;
        callback_state.stream_output = streaming;
        callback_state.stream_version =
            serve_native_rgba_stream_v2 ? 2U : 1U;
        callback_state.stream_fps = (uint32_t)requested_fps;
        callback_state.stream_output_fd = (int)stream_output_fd;
#ifdef TATER_CRONOS_ABI
        callback_state.linear_rgba_output = linear_rgba_output;
#endif
#ifdef TATER_NATIVE_BUFFER_SUPPORT
        callback_state.native_rgba_output =
            decode_native_rgba_stream || streaming;
#endif
        if (!streaming)
            output_path = argv[7];
    }
    memset(buffers, 0, sizeof(buffers));

    library = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "component dlopen failed: %s\n", dlerror());
        goto cleanup;
    }
    dlerror();
    symbol = dlsym(library, "MtkOmxComponentCreate");
    if (symbol == NULL) {
        fprintf(stderr, "component factory lookup failed: %s\n", dlerror());
        goto cleanup;
    }
    if (sizeof(create_component) != sizeof(symbol)) {
        fprintf(stderr, "refusing incompatible component factory pointer\n");
        goto cleanup;
    }
    memcpy(&create_component, &symbol, sizeof(create_component));

    if (decode_stream) {
        utility_library =
            dlopen("libvcodec_utility.so", RTLD_NOW | RTLD_LOCAL);
        if (utility_library == NULL) {
            fprintf(stderr, "cache utility dlopen failed: %s\n", dlerror());
            goto cleanup;
        }
        dlerror();
        symbol = dlsym(utility_library, "eVideoInvalidateCache");
        if (symbol == NULL ||
            sizeof(callback_state.invalidate_cache) != sizeof(symbol)) {
            fprintf(stderr, "cache invalidation lookup failed: %s\n",
                    dlerror());
            goto cleanup;
        }
        memcpy(&callback_state.invalidate_cache, &symbol,
               sizeof(callback_state.invalidate_cache));
    }

    alarm(OMX_CALL_TIMEOUT_SECONDS);
    component = create_component((OMX_STRING)AVC_COMPONENT_NAME);
    alarm(0);
    if (component == NULL || component->nSize < sizeof(*component) ||
        component->SetCallbacks == NULL || component->GetState == NULL ||
        component->GetParameter == NULL ||
        component->GetExtensionIndex == NULL ||
        component->ComponentDeInit == NULL) {
        fprintf(stderr, "component factory returned an invalid interface\n");
        goto cleanup;
    }
    component_initialized = 1;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.EventHandler = event_handler;
    callbacks.EmptyBufferDone = empty_buffer_done;
    callbacks.FillBufferDone = fill_buffer_done;
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->SetCallbacks(
        component, &callbacks, &callback_state);
    alarm(0);
    if (omx_result != OMX_ErrorNone) {
        fprintf(stderr, "OMX callback registration failed: 0x%08x\n",
                (unsigned int)omx_result);
        goto cleanup;
    }

    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->GetState(component, &state);
    alarm(0);
    if (omx_result != OMX_ErrorNone || state != OMX_StateLoaded) {
        fprintf(stderr, "unexpected initial OMX state: result=0x%08x state=%u\n",
                (unsigned int)omx_result, (unsigned int)state);
        goto cleanup;
    }
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    ion_extension_result = component->GetExtensionIndex(
        component, (OMX_STRING)"OMX.MTK.VIDEO.index.useIonBuffer",
        &ion_extension_index);
    alarm(0);
#ifdef TATER_NATIVE_BUFFER_SUPPORT
    if (decode_native_stream) {
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->GetExtensionIndex(
            component,
            (OMX_STRING)
                "OMX.google.android.index.enableAndroidNativeBuffers",
            &enable_native_buffer_index);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr,
                    "native-buffer enable extension unavailable: 0x%08x\n",
                    (unsigned int)omx_result);
            goto cleanup;
        }
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->GetExtensionIndex(
            component,
            (OMX_STRING)
                "OMX.google.android.index.useAndroidNativeBuffer",
            &use_native_buffer_index);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr,
                    "native-buffer registration extension unavailable: "
                    "0x%08x\n", (unsigned int)omx_result);
            goto cleanup;
        }
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->GetExtensionIndex(
            component,
            (OMX_STRING)
                "OMX.google.android.index.getAndroidNativeBufferUsage",
            &native_buffer_usage_index);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr,
                    "native-buffer usage extension unavailable: 0x%08x\n",
                    (unsigned int)omx_result);
            goto cleanup;
        }
    }
#endif

    initialize_header(&video_ports, sizeof(video_ports));
    alarm(OMX_CALL_TIMEOUT_SECONDS);
    omx_result = component->GetParameter(
        component, OMX_IndexParamVideoInit, &video_ports);
    alarm(0);
    if (omx_result != OMX_ErrorNone || video_ports.nPorts == 0 ||
        video_ports.nPorts > MAX_VIDEO_PORTS) {
        fprintf(stderr,
                "invalid OMX video-port inventory: result=0x%08x ports=%u\n",
                (unsigned int)omx_result,
                (unsigned int)video_ports.nPorts);
        goto cleanup;
    }

    printf("{\"component\":\"%s\",\"initial_state\":%u,"
           "\"video_port_start\":%u,\"video_port_count\":%u,"
           "\"ion_extension_result\":%u,"
           "\"ion_extension_index\":%u,"
           "\"ports\":[",
           AVC_COMPONENT_NAME, (unsigned int)state,
           (unsigned int)video_ports.nStartPortNumber,
           (unsigned int)video_ports.nPorts,
           (unsigned int)ion_extension_result,
           (unsigned int)ion_extension_index);
    for (port_offset = 0; port_offset < video_ports.nPorts; ++port_offset) {
        initialize_header(&definitions[port_offset],
                          sizeof(definitions[port_offset]));
        definitions[port_offset].nPortIndex =
            video_ports.nStartPortNumber + port_offset;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->GetParameter(
            component, OMX_IndexParamPortDefinition,
            &definitions[port_offset]);
        alarm(0);
        if (omx_result == OMX_ErrorNone && decode_stream) {
            definitions[port_offset].format.video.nFrameWidth =
                (OMX_U32)requested_width;
            definitions[port_offset].format.video.nFrameHeight =
                (OMX_U32)requested_height;
            definitions[port_offset].format.video.nStride =
                (OMX_S32)requested_width;
            definitions[port_offset].format.video.nSliceHeight =
                (OMX_U32)requested_height;
            if (definitions[port_offset].eDir == OMX_DirInput) {
                definitions[port_offset].format.video.eCompressionFormat =
                    OMX_VIDEO_CodingAVC;
                definitions[port_offset].format.video.eColorFormat =
                    OMX_COLOR_FormatUnused;
                if (definitions[port_offset].nBufferSize <
                    maximum_access_unit_bytes)
                    definitions[port_offset].nBufferSize =
                        (OMX_U32)maximum_access_unit_bytes;
            } else {
                definitions[port_offset].format.video.eCompressionFormat =
                    OMX_VIDEO_CodingUnused;
                definitions[port_offset].format.video.eColorFormat =
                    OMX_COLOR_FormatYUV420Planar;
#ifdef TATER_CRONOS_ABI
                definitions[port_offset].nBufferSize =
                    (OMX_U32)(requested_width * requested_height * 3U / 2U);
#else
                if (definitions[port_offset].nBufferSize <
                    decoded_frame_bytes)
                    definitions[port_offset].nBufferSize =
                        (OMX_U32)decoded_frame_bytes;
#endif
            }
            if (omx_result == OMX_ErrorNone) {
                alarm(OMX_CALL_TIMEOUT_SECONDS);
                omx_result = component->SetParameter(
                    component, OMX_IndexParamPortDefinition,
                    &definitions[port_offset]);
                alarm(0);
            }
            if (omx_result == OMX_ErrorNone) {
                initialize_header(&definitions[port_offset],
                                  sizeof(definitions[port_offset]));
                definitions[port_offset].nPortIndex =
                    video_ports.nStartPortNumber + port_offset;
                alarm(OMX_CALL_TIMEOUT_SECONDS);
                omx_result = component->GetParameter(
                    component, OMX_IndexParamPortDefinition,
                    &definitions[port_offset]);
                alarm(0);
            }
        }
        if (omx_result != OMX_ErrorNone ||
            definitions[port_offset].nBufferCountActual == 0 ||
            definitions[port_offset].nBufferCountActual > MAX_PORT_BUFFERS) {
            fprintf(stderr,
                    "refusing invalid OMX allocation contract: port=%u "
                    "result=0x%08x count=%u\n",
                    (unsigned int)definitions[port_offset].nPortIndex,
                    (unsigned int)omx_result,
                    (unsigned int)definitions[port_offset].nBufferCountActual);
            fputs("]}\n", stdout);
            goto cleanup;
        }
        if (definitions[port_offset].eDir == OMX_DirInput)
            input_port_offset = (int)port_offset;
        else if (definitions[port_offset].eDir == OMX_DirOutput)
            output_port_offset = (int)port_offset;
        if (print_port_definition(
                component, video_ports.nStartPortNumber + port_offset,
                queried_ports) != 0) {
            fputs("]}\n", stdout);
            goto cleanup;
        }
        ++queried_ports;
    }
    if (decode_stream &&
        (input_port_offset < 0 || output_port_offset < 0 ||
         definitions[output_port_offset].format.video.eColorFormat !=
             OMX_COLOR_FormatYUV420Planar ||
         definitions[output_port_offset].format.video.nFrameWidth !=
             requested_width ||
         definitions[output_port_offset].format.video.nFrameHeight !=
             requested_height)) {
        fprintf(stderr, "OMX rejected the bounded planar decode contract\n");
        fputs("]}\n", stdout);
        goto cleanup;
    }
#ifdef TATER_NATIVE_BUFFER_SUPPORT
    if (decode_native_stream) {
        struct enable_android_native_buffers enable_native;
        struct get_android_native_buffer_usage usage;

        initialize_header(&enable_native, sizeof(enable_native));
        enable_native.nPortIndex =
            definitions[output_port_offset].nPortIndex;
        enable_native.enable = OMX_TRUE;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->SetParameter(
            component, enable_native_buffer_index, &enable_native);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr,
                    "enabling Android native output failed: 0x%08x\n",
                    (unsigned int)omx_result);
            goto cleanup;
        }
        initialize_header(&usage, sizeof(usage));
        usage.nPortIndex = definitions[output_port_offset].nPortIndex;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->GetParameter(
            component, native_buffer_usage_index, &usage);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr,
                    "native output usage query failed: 0x%08x\n",
                    (unsigned int)omx_result);
            goto cleanup;
        }
        native_buffer_usage = usage.nUsage;
        callback_state.native_output = 1;
        fprintf(stderr,
                "OMX_NATIVE_OUTPUT enable_index=%u use_index=%u "
                "usage_index=%u usage=0x%08x\n",
                (unsigned int)enable_native_buffer_index,
                (unsigned int)use_native_buffer_index,
                (unsigned int)native_buffer_usage_index,
                (unsigned int)native_buffer_usage);
    }
#endif
    if (output_port_offset >= 0) {
        OMX_U32 format_index;

        for (format_index = 0; format_index < 32U; ++format_index) {
            OMX_VIDEO_PARAM_PORTFORMATTYPE format;

            initialize_header(&format, sizeof(format));
            format.nPortIndex =
                definitions[output_port_offset].nPortIndex;
            format.nIndex = format_index;
            alarm(OMX_CALL_TIMEOUT_SECONDS);
            omx_result = component->GetParameter(
                component, OMX_IndexParamVideoPortFormat, &format);
            alarm(0);
            if (omx_result == OMX_ErrorNoMore)
                break;
            if (omx_result != OMX_ErrorNone) {
                fprintf(stderr,
                        "OMX output-format query failed: index=%u "
                        "result=0x%08x\n",
                        (unsigned int)format_index,
                        (unsigned int)omx_result);
                break;
            }
            fprintf(stderr,
                    "OMX_OUTPUT_FORMAT index=%u compression=%u color=%u\n",
                    (unsigned int)format_index,
                    (unsigned int)format.eCompressionFormat,
                    (unsigned int)format.eColorFormat);
        }
    }
    if (exercise_buffers) {
        completions_before = callback_state.state_completions;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->SendCommand(
            component, OMX_CommandStateSet, OMX_StateIdle, NULL);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr, "OMX idle command failed: 0x%08x\n",
                    (unsigned int)omx_result);
            fputs("]}\n", stdout);
            goto cleanup;
        }
        for (port_offset = 0; port_offset < video_ports.nPorts;
             ++port_offset) {
            OMX_U32 buffer_index;

#ifdef TATER_NATIVE_BUFFER_SUPPORT
            if (decode_native_stream &&
                (int)port_offset == output_port_offset) {
                if (allocate_native_output_buffers(
                        component, &callback_state,
                        &definitions[port_offset],
                        use_native_buffer_index, native_buffer_usage,
                        buffers[port_offset],
                        &buffer_counts[port_offset]) != 0) {
                    fputs("]}\n", stdout);
                    goto cleanup;
                }
                continue;
            }
#endif
            for (buffer_index = 0;
                 buffer_index <
                     definitions[port_offset].nBufferCountActual;
                 ++buffer_index) {
                alarm(OMX_CALL_TIMEOUT_SECONDS);
                omx_result = component->AllocateBuffer(
                    component, &buffers[port_offset][buffer_index],
                    definitions[port_offset].nPortIndex, NULL,
                    definitions[port_offset].nBufferSize);
                alarm(0);
                if (omx_result != OMX_ErrorNone ||
                    buffers[port_offset][buffer_index] == NULL) {
                    fprintf(stderr,
                            "OMX allocation failed: port=%u index=%u "
                            "result=0x%08x\n",
                            (unsigned int)definitions[port_offset].nPortIndex,
                            (unsigned int)buffer_index,
                            (unsigned int)omx_result);
                    fputs("]}\n", stdout);
                    goto cleanup;
                }
                ++buffer_counts[port_offset];
            }
        }
        buffers_allocated = 1;
        if (wait_for_state(&callback_state, completions_before,
                           OMX_StateIdle) != 0) {
            fputs("]}\n", stdout);
            goto cleanup;
        }

        completions_before = callback_state.state_completions;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->SendCommand(
            component, OMX_CommandStateSet, OMX_StateExecuting, NULL);
        alarm(0);
        if (omx_result != OMX_ErrorNone ||
            wait_for_state(&callback_state, completions_before,
                           OMX_StateExecuting) != 0) {
            fprintf(stderr, "OMX executing transition failed: 0x%08x\n",
                    (unsigned int)omx_result);
            fputs("]}\n", stdout);
            goto cleanup;
        }

        if (decode_stream) {
            uint32_t buffer_index;
            uint32_t returned_inputs_consumed = 0;
            int decode_failed = 0;
            int output_reconfigured = 0;

            if (streaming) {
                int packet_result = read_stream_packet(
                    (int)stream_input_fd, callback_state.stream_version,
                    &stream_current_packet, &stream_current_size,
                    &stream_current_pts_us);

                if (packet_result <= 0) {
                    fprintf(stderr,
                            "stream ended before its first access unit\n");
                    decode_failed = 1;
                } else {
                    packet_result = read_stream_packet(
                        (int)stream_input_fd,
                        callback_state.stream_version,
                        &stream_next_packet, &stream_next_size,
                        &stream_next_pts_us);
                    if (packet_result < 0) {
                        fprintf(stderr,
                                "invalid framed H.264 stream input\n");
                        decode_failed = 1;
                    }
                }
            }

            for (buffer_index = 0;
                 buffer_index < buffer_counts[output_port_offset];
                 ++buffer_index) {
                OMX_BUFFERHEADERTYPE *output_buffer =
                    buffers[output_port_offset][buffer_index];

                output_buffer->nOffset = 0;
                output_buffer->nFilledLen = 0;
                output_buffer->nFlags = 0;
                alarm(OMX_CALL_TIMEOUT_SECONDS);
                omx_result = component->FillThisBuffer(component,
                                                       output_buffer);
                alarm(0);
                if (omx_result != OMX_ErrorNone) {
                    fprintf(stderr,
                            "OMX output queue failed: index=%u "
                            "result=0x%08x\n",
                            (unsigned int)buffer_index,
                            (unsigned int)omx_result);
                    decode_failed = 1;
                    break;
                }
            }

            while (!decode_failed &&
                   (streaming ? stream_current_size != 0 :
                    fed_inputs < requested_frames)) {
                OMX_BUFFERHEADERTYPE *input_buffer = NULL;
                const uint8_t *access_unit;
                size_t access_unit_size;
                int final_access_unit;

                __sync_synchronize();
                if (callback_state.error != OMX_ErrorNone) {
                    decode_failed = 1;
                    break;
                }

                if (streaming) {
                    access_unit = stream_current_packet;
                    access_unit_size = stream_current_size;
                    final_access_unit = stream_next_size == 0;
                } else {
                    access_unit = fixture +
                        access_unit_starts[fed_inputs];
                    access_unit_size =
                        access_unit_ends[fed_inputs] -
                        access_unit_starts[fed_inputs];
                    final_access_unit =
                        fed_inputs + 1U == requested_frames;
                }

                if (fed_inputs == 1U && !output_reconfigured) {
                    unsigned int wait_iteration;

                    for (wait_iteration = 0; wait_iteration < 1000U;
                         ++wait_iteration) {
                        __sync_synchronize();
                        if (callback_state.error != OMX_ErrorNone ||
                            callback_state.port_settings_changed != 0)
                            break;
                        usleep(10000);
                    }
                    if (callback_state.error != OMX_ErrorNone ||
                        callback_state.port_settings_changed == 0
#ifdef TATER_NATIVE_BUFFER_SUPPORT
                        || (decode_native_stream ?
                            rebuild_native_output_port(
                                component, &callback_state,
                                &definitions[output_port_offset],
                                buffers[output_port_offset],
                                &buffer_counts[output_port_offset],
                                (OMX_U32)requested_width,
                                (OMX_U32)requested_height,
                                use_native_buffer_index,
                                native_buffer_usage) :
                            rebuild_output_port(
                                component, &callback_state,
                                &definitions[output_port_offset],
                                buffers[output_port_offset],
                                &buffer_counts[output_port_offset],
                                (OMX_U32)requested_width,
                                (OMX_U32)requested_height,
                                OMX_COLOR_FormatYUV420Planar)) != 0
#else
                        || rebuild_output_port(
                            component, &callback_state,
                            &definitions[output_port_offset],
                            buffers[output_port_offset],
                            &buffer_counts[output_port_offset],
                            (OMX_U32)requested_width,
                            (OMX_U32)requested_height,
                            OMX_COLOR_FormatYUV420Planar) != 0
#endif
                        ) {
                        fprintf(stderr,
                                "OMX output-port negotiation failed\n");
                        decode_failed = 1;
                        break;
                    }
                    if (callback_state.stream_version == 2U &&
#if defined(TATER_NATIVE_BUFFER_SUPPORT) || defined(TATER_CRONOS_ABI)
                        write_stream_v2_header(&callback_state) != 0) {
#else
                        1) {
#endif
                        fprintf(stderr,
                                "could not write v2 stream header\n");
                        decode_failed = 1;
                        break;
                    }
                    for (buffer_index = 0;
                         buffer_index <
                             buffer_counts[output_port_offset];
                         ++buffer_index) {
                        OMX_BUFFERHEADERTYPE *output_buffer =
                            buffers[output_port_offset][buffer_index];

                        callback_state.recycle_outputs = 1;
                        output_buffer->nOffset = 0;
                        output_buffer->nFilledLen = 0;
                        output_buffer->nFlags = 0;
                        alarm(OMX_CALL_TIMEOUT_SECONDS);
                        omx_result = component->FillThisBuffer(
                            component, output_buffer);
                        alarm(0);
                        if (omx_result != OMX_ErrorNone) {
                            fprintf(stderr,
                                    "OMX output requeue failed: index=%u "
                                    "result=0x%08x\n",
                                    (unsigned int)buffer_index,
                                    (unsigned int)omx_result);
                            decode_failed = 1;
                            break;
                        }
                    }
                    output_reconfigured = !decode_failed;
                    if (decode_failed)
                        break;
                }

                if (fed_inputs < buffer_counts[input_port_offset]) {
                    input_buffer =
                        buffers[input_port_offset][fed_inputs];
                } else {
                    unsigned int wait_iteration;

                    for (wait_iteration = 0; wait_iteration < 1000U;
                         ++wait_iteration) {
                        __sync_synchronize();
                        if (callback_state.error != OMX_ErrorNone)
                            break;
                        if (callback_state.empty_done >
                            returned_inputs_consumed) {
                            input_buffer =
                                callback_state.returned_inputs[
                                    returned_inputs_consumed %
                                        MAX_DECODE_FRAMES];
                            ++returned_inputs_consumed;
                            break;
                        }
                        usleep(10000);
                    }
                }
                if (input_buffer == NULL || access_unit_size == 0 ||
                    !access_unit_has_vcl(access_unit, access_unit_size) ||
                    access_unit_size > input_buffer->nAllocLen) {
                    fprintf(stderr,
                            "OMX input buffer unavailable, empty, or too small: "
                            "frame=%u bytes=%zu\n",
                            fed_inputs, access_unit_size);
                    decode_failed = 1;
                    break;
                }
                memcpy(input_buffer->pBuffer, access_unit,
                       access_unit_size);
                input_buffer->nOffset = 0;
                input_buffer->nFilledLen = (OMX_U32)access_unit_size;
                input_buffer->nTimeStamp =
                    (OMX_TICKS)(streaming ?
                        (callback_state.stream_version == 2U ?
                            stream_current_pts_us :
                            ((int64_t)fed_inputs * 1000000) /
                                (int64_t)requested_fps) :
                        (int64_t)fed_inputs * 33333);
                input_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;
                if (final_access_unit)
                    input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                alarm(OMX_CALL_TIMEOUT_SECONDS);
                omx_result = component->EmptyThisBuffer(component,
                                                        input_buffer);
                alarm(0);
                if (omx_result != OMX_ErrorNone) {
                    fprintf(stderr,
                            "OMX input queue failed: frame=%u "
                            "result=0x%08x\n",
                            fed_inputs, (unsigned int)omx_result);
                    decode_failed = 1;
                    break;
                }
                ++fed_inputs;
                if (streaming) {
                    int packet_result;

                    free(stream_current_packet);
                    stream_current_packet = stream_next_packet;
                    stream_current_size = stream_next_size;
                    stream_current_pts_us = stream_next_pts_us;
                    stream_next_packet = NULL;
                    stream_next_size = 0;
                    stream_next_pts_us = 0;
                    if (stream_current_size == 0)
                        continue;
                    packet_result = read_stream_packet(
                        (int)stream_input_fd,
                        callback_state.stream_version,
                        &stream_next_packet, &stream_next_size,
                        &stream_next_pts_us);
                    if (packet_result < 0) {
                        fprintf(stderr,
                                "invalid framed H.264 stream input\n");
                        decode_failed = 1;
                    }
                }
            }

            if (!decode_failed) {
                unsigned int wait_iteration;

                for (wait_iteration = 0; wait_iteration < 1500U;
                     ++wait_iteration) {
                    __sync_synchronize();
                    if (callback_state.error != OMX_ErrorNone)
                        break;
                    if (callback_state.copied_frames ==
                        (streaming ? fed_inputs : requested_frames))
                        break;
                    usleep(10000);
                }
                __sync_synchronize();
                if (callback_state.error != OMX_ErrorNone ||
                    callback_state.copied_frames !=
                        (streaming ? fed_inputs : requested_frames)) {
                    fprintf(stderr,
                            "OMX decode incomplete: fed=%u copied=%u "
                            "fill_done=%u empty_done=%u port_changes=%u "
                            "eos_events=%u error=0x%08x\n",
                            fed_inputs, callback_state.copied_frames,
                            callback_state.fill_done,
                            callback_state.empty_done,
                            callback_state.port_settings_changed,
                            callback_state.eos_events,
                            (unsigned int)callback_state.error);
                    decode_failed = 1;
                }
            }
            if (!decode_failed && !streaming &&
                write_file(output_path, decoded_frames,
                           decoded_total_bytes) != 0) {
                fprintf(stderr, "could not write bounded YUV stream\n");
                decode_failed = 1;
            }
            if (!decode_failed && streaming &&
                callback_state.stream_version == 2U &&
#if defined(TATER_NATIVE_BUFFER_SUPPORT) || defined(TATER_CRONOS_ABI)
                write_stream_v2_record(
                    &callback_state, NULL, 0U,
                    TATER_STREAM_V2_FLAG_EOS, 0) != 0) {
#else
                1) {
#endif
                fprintf(stderr, "could not write v2 stream terminator\n");
                decode_failed = 1;
            }
            decode_succeeded = !decode_failed;
        }

        completions_before = callback_state.state_completions;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->SendCommand(
            component, OMX_CommandStateSet, OMX_StateIdle, NULL);
        alarm(0);
        if (omx_result != OMX_ErrorNone ||
            wait_for_state(&callback_state, completions_before,
                           OMX_StateIdle) != 0) {
            fprintf(stderr, "OMX return-to-idle failed: 0x%08x\n",
                    (unsigned int)omx_result);
            fputs("]}\n", stdout);
            goto cleanup;
        }

        completions_before = callback_state.state_completions;
        alarm(OMX_CALL_TIMEOUT_SECONDS);
        omx_result = component->SendCommand(
            component, OMX_CommandStateSet, OMX_StateLoaded, NULL);
        alarm(0);
        if (omx_result != OMX_ErrorNone) {
            fprintf(stderr, "OMX loaded command failed: 0x%08x\n",
                    (unsigned int)omx_result);
            fputs("]}\n", stdout);
            goto cleanup;
        }
        for (port_offset = 0; port_offset < video_ports.nPorts;
             ++port_offset) {
            OMX_U32 buffer_index;

            for (buffer_index = 0;
                 buffer_index < buffer_counts[port_offset]; ++buffer_index) {
                alarm(OMX_CALL_TIMEOUT_SECONDS);
                omx_result = component->FreeBuffer(
                    component, definitions[port_offset].nPortIndex,
                    buffers[port_offset][buffer_index]);
                alarm(0);
                if (omx_result != OMX_ErrorNone) {
                    fprintf(stderr,
                            "OMX free failed: port=%u index=%u "
                            "result=0x%08x\n",
                            (unsigned int)definitions[port_offset].nPortIndex,
                            (unsigned int)buffer_index,
                            (unsigned int)omx_result);
                    fputs("]}\n", stdout);
                    goto cleanup;
                }
                buffers[port_offset][buffer_index] = NULL;
            }
#ifdef TATER_NATIVE_BUFFER_SUPPORT
            if (decode_native_stream &&
                (int)port_offset == output_port_offset)
                forget_native_output_buffers(&callback_state);
#endif
            buffer_counts[port_offset] = 0;
        }
        buffers_allocated = 0;
        if (wait_for_state(&callback_state, completions_before,
                           OMX_StateLoaded) != 0) {
            fputs("]}\n", stdout);
            goto cleanup;
        }
        loaded_after_buffers = 1;
    }
    printf("],\"buffer_lifecycle_exercised\":%s,"
           "\"loaded_after_buffers\":%s,"
           "\"decode_requested\":%s,\"streaming\":%s,"
           "\"decode_succeeded\":%s,"
           "\"fed_inputs\":%u,\"copied_frames\":%u,"
           "\"port_settings_changed\":%u,\"eos_events\":%u,"
           "\"cache_invalidations\":%u,"
           "\"cache_invalidation_failures\":%u,"
           "\"stream_write_failures\":%u,"
           "\"msync_failures\":%u,"
           "\"first_msync_errno\":%d,"
           "\"first_output_alloc_len\":%u,"
           "\"first_output_filled_len\":%u,"
           "\"first_output_offset\":%u,"
           "\"max_black_mismatches_at_offset0\":%u,"
           "\"max_black_mismatches_at_offset32\":%u,"
           "\"callbacks_before_deinit\":{\"events\":%u,"
           "\"empty_done\":%u,\"fill_done\":%u}}\n",
           exercise_buffers ? "true" : "false",
           loaded_after_buffers ? "true" : "false",
           decode_stream ? "true" : "false",
           streaming ? "true" : "false",
           decode_succeeded ? "true" : "false", fed_inputs,
           callback_state.copied_frames,
           callback_state.port_settings_changed,
           callback_state.eos_events,
           callback_state.cache_invalidations,
           callback_state.cache_invalidation_failures,
           callback_state.stream_write_failures,
           callback_state.msync_failures,
           callback_state.first_msync_errno,
           callback_state.first_output_alloc_len,
           callback_state.first_output_filled_len,
           callback_state.first_output_offset,
           callback_state.max_black_mismatches_at_offset0,
           callback_state.max_black_mismatches_at_offset32,
           callback_state.events, callback_state.empty_done,
           callback_state.fill_done);
    result = decode_stream && !decode_succeeded ? 1 : 0;

cleanup:
    alarm(0);
    if (buffers_allocated) {
        fprintf(stderr,
                "OMX fatal path is using process-exit resource cleanup\n");
        fflush(stderr);
        _exit(1);
    }
    if (component_initialized) {
        OMX_ERRORTYPE deinit_result;

        alarm(OMX_CALL_TIMEOUT_SECONDS);
        deinit_result = component->ComponentDeInit(component);
        alarm(0);
        if (deinit_result != OMX_ErrorNone) {
            fprintf(stderr, "OMX component deinit failed: 0x%08x\n",
                    (unsigned int)deinit_result);
            result = 1;
        }
    }
#ifndef TATER_CRONOS_ABI
    if (library != NULL)
        dlclose(library);
    if (utility_library != NULL)
        dlclose(utility_library);
#endif
    free(decoded_frames);
    free(fixture);
    free(stream_current_packet);
    free(stream_next_packet);
    return result;
}

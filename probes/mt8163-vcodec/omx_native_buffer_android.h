#ifndef TATER_OMX_NATIVE_BUFFER_ANDROID_H
#define TATER_OMX_NATIVE_BUFFER_ANDROID_H

#include <stddef.h>
#include <stdint.h>

#include <media/openmax/OMX_Component.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tater_native_buffer;

int tater_native_buffer_create(uint32_t width, uint32_t height,
                               int32_t format, uint32_t usage,
                               struct tater_native_buffer **output);
void tater_native_buffer_destroy(struct tater_native_buffer *buffer);
uint32_t tater_native_buffer_stride(
    const struct tater_native_buffer *buffer);
int tater_native_buffer_copy_mtk_yuv420p(
    struct tater_native_buffer *buffer, uint32_t width, uint32_t height,
    uint8_t *destination, size_t destination_size);
int tater_native_buffer_copy_mtk_rgba8888(
    struct tater_native_buffer *buffer, uint32_t width, uint32_t height,
    uint8_t *destination, size_t destination_size);
OMX_ERRORTYPE tater_omx_use_native_buffer(
    OMX_COMPONENTTYPE *component, OMX_INDEXTYPE extension_index,
    OMX_U32 port_index, OMX_PTR application_private,
    OMX_BUFFERHEADERTYPE **header,
    struct tater_native_buffer *buffer);

#ifdef __cplusplus
}
#endif

#endif

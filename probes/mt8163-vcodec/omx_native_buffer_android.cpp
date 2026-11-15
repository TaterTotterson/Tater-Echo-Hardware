#include "omx_native_buffer_android.h"

#include "mtk_yuv420_block.h"

#include <dlfcn.h>
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <media/hardware/HardwareAPI.h>
#include <system/graphics.h>
#include <ui/GraphicBuffer.h>

struct tater_native_buffer {
    android::sp<android::GraphicBuffer> graphic;
};

typedef int (*hw_get_module_fn)(const char *, const hw_module_t **);
typedef int32_t (*invalidate_cache_fn)(void *, uint32_t, uint32_t);

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

static_assert(sizeof(struct val_memory) == 88,
              "unexpected public VAL_MEMORY_T ABI size");

/*
 * The stock decoder obtains this singleton even for a headless decode, then
 * uses the optional GUI-extension pool only as an optimization. Tater Linux
 * intentionally does not run Android's service manager. Returning a bounded
 * local placeholder and reporting the optional allocation as unavailable
 * keeps the decoder on its ordinary registered-output-buffer path.
 */
static uint64_t headless_gui_ext_producer_storage[32];

extern "C" __attribute__((visibility("default"))) void *
tater_headless_gui_ext_instance(void)
    __asm__("_ZN7android9SingletonINS_20GuiExtClientProducerEE11getInstanceEv");
extern "C" __attribute__((visibility("default"))) int
tater_headless_gui_ext_alloc(
    void *, uint32_t, uint32_t, uint32_t, uint32_t *)
    __asm__("_ZN7android20GuiExtClientProducer5allocEjjjPj");
extern "C" __attribute__((visibility("default"))) int
tater_headless_gui_ext_free(void *, uint32_t)
    __asm__("_ZN7android20GuiExtClientProducer4freeEj");

extern "C" void *tater_headless_gui_ext_instance(void) {
    return headless_gui_ext_producer_storage;
}

extern "C" int tater_headless_gui_ext_alloc(
    void *instance, uint32_t gralloc_usage, uint32_t width,
    uint32_t height, uint32_t *pool_id) {
    (void)instance;
    (void)gralloc_usage;
    (void)width;
    (void)height;
    if (pool_id != NULL)
        *pool_id = 0;
    return -1;
}

extern "C" int tater_headless_gui_ext_free(
    void *instance, uint32_t pool_id) {
    (void)instance;
    (void)pool_id;
    return 0;
}

extern "C" __attribute__((visibility("default"))) int hw_get_module(
    const char *id, const hw_module_t **module) {
    static void *gralloc_library;
    static const hw_module_t *gralloc_module;
    hw_get_module_fn next_get_module;
    void *symbol;

    if (id == NULL || module == NULL)
        return -1;
    if (strcmp(id, GRALLOC_HARDWARE_MODULE_ID) != 0) {
        symbol = dlsym(RTLD_NEXT, "hw_get_module");
        if (symbol == NULL || sizeof(next_get_module) != sizeof(symbol))
            return -1;
        memcpy(&next_get_module, &symbol, sizeof(next_get_module));
        return next_get_module(id, module);
    }
    if (gralloc_module == NULL) {
        const char *gralloc_path = getenv("TATER_VDEC_GRALLOC");

        /*
         * The owner-local runtime directory is supplied through the process'
         * reviewed environment. A bare SONAME is the relocatable default;
         * Tater supplies an absolute allowlisted path on older Android
         * linkers that do not honor LD_LIBRARY_PATH for a HAL dlopen.
         */
        if (gralloc_path == NULL || gralloc_path[0] == '\0')
            gralloc_path = "gralloc.mt8163.mali.so";
        gralloc_library = dlopen(
            gralloc_path, RTLD_NOW | RTLD_LOCAL);
        if (gralloc_library == NULL)
            return -1;
        symbol = dlsym(gralloc_library, HAL_MODULE_INFO_SYM_AS_STR);
        if (symbol == NULL)
            return -1;
        gralloc_module = static_cast<const hw_module_t *>(symbol);
        if (gralloc_module->id == NULL ||
            strcmp(gralloc_module->id,
                   GRALLOC_HARDWARE_MODULE_ID) != 0) {
            gralloc_module = NULL;
            return -1;
        }
    }
    *module = gralloc_module;
    return 0;
}

extern "C" int tater_native_buffer_create(
    uint32_t width, uint32_t height, int32_t format, uint32_t usage,
    struct tater_native_buffer **output) {
    struct tater_native_buffer *result;

    if (output == NULL || width == 0 || height == 0)
        return -1;
    *output = NULL;
    result = new (std::nothrow) tater_native_buffer;
    if (result == NULL)
        return -1;
    result->graphic = new (std::nothrow) android::GraphicBuffer(
        width, height, static_cast<android::PixelFormat>(format), usage);
    if (result->graphic.get() == NULL || result->graphic->initCheck() != 0) {
        delete result;
        return -1;
    }
    *output = result;
    return 0;
}

extern "C" void tater_native_buffer_destroy(
    struct tater_native_buffer *buffer) {
    delete buffer;
}

extern "C" uint32_t tater_native_buffer_stride(
    const struct tater_native_buffer *buffer) {
    if (buffer == NULL || buffer->graphic.get() == NULL)
        return 0;
    return buffer->graphic->getStride();
}

extern "C" int tater_native_buffer_copy_mtk_yuv420p(
    struct tater_native_buffer *buffer, uint32_t width, uint32_t height,
    uint8_t *destination, size_t destination_size) {
    uint8_t *source = NULL;
    uint32_t buffer_width;
    uint32_t buffer_height;
    size_t source_size;
    size_t expected_destination;
    int conversion_result;
    int lock_result;
    int unlock_result;
    static void *cache_utility;
    static invalidate_cache_fn invalidate_cache;

    if (buffer == NULL || buffer->graphic.get() == NULL ||
        destination == NULL || width == 0 || height == 0 ||
        (width & 1U) != 0 || (height & 1U) != 0)
        return -1;
    buffer_width = buffer->graphic->getStride();
    buffer_height = (height + 31U) & ~31U;
    if (buffer_width < width || buffer_width % 16U != 0 ||
        buffer_height < height || buffer_height % 32U != 0)
        return -1;
    source_size = (size_t)buffer_width * buffer_height * 3U / 2U;
    expected_destination = (size_t)width * height * 3U / 2U;
    if (destination_size < expected_destination)
        return -1;

    lock_result = buffer->graphic->lock(
        GRALLOC_USAGE_SW_READ_OFTEN,
        reinterpret_cast<void **>(&source));
    if (lock_result != 0 || source == NULL)
        return -1;
    if (invalidate_cache == NULL) {
        void *symbol;

        if (cache_utility == NULL)
            cache_utility = dlopen(
                "libvcodec_utility.so", RTLD_NOW | RTLD_LOCAL);
        symbol = cache_utility == NULL ? NULL :
            dlsym(cache_utility, "eVideoInvalidateCache");
        if (symbol == NULL || sizeof(invalidate_cache) != sizeof(symbol)) {
            buffer->graphic->unlock();
            return -1;
        }
        memcpy(&invalidate_cache, &symbol, sizeof(invalidate_cache));
    }
    {
        struct val_memory memory;

        memset(&memory, 0, sizeof(memory));
        memory.memory_type = 1U;
        memory.memory_size.extended = source_size;
        memory.memory_va.pointer = source;
        memory.alignment = 1U << 6;
        memory.aligned_memory_va.pointer = source;
        memory.memory_codec = 1U;
        memory.ion_share_fd = UINT32_MAX;
        if (invalidate_cache(&memory, sizeof(memory), 1U) != 0) {
            buffer->graphic->unlock();
            return -1;
        }
    }
    conversion_result = tater_mtk_yuv420_block_to_yuv420p(
        source, source_size, buffer_width, buffer_height, width, height,
        destination, destination_size);
    if (conversion_result == 0) {
        size_t index;
        int any_nonzero = 0;

        for (index = 0; index < expected_destination; ++index) {
            if (destination[index] != 0) {
                any_nonzero = 1;
                break;
            }
        }
        if (!any_nonzero)
            conversion_result = -1;
    }
    unlock_result = buffer->graphic->unlock();
    return conversion_result == 0 && unlock_result == 0 ? 0 : -1;
}

extern "C" int tater_native_buffer_copy_mtk_rgba8888(
    struct tater_native_buffer *buffer, uint32_t width, uint32_t height,
    uint8_t *destination, size_t destination_size) {
    uint8_t *source = NULL;
    uint32_t buffer_width;
    uint32_t buffer_height;
    size_t source_size;
    size_t expected_destination;
    int conversion_result;
    int lock_result;
    int unlock_result;
    static void *cache_utility;
    static invalidate_cache_fn invalidate_cache;

    if (buffer == NULL || buffer->graphic.get() == NULL ||
        destination == NULL || width == 0 || height == 0)
        return -1;
    buffer_width = buffer->graphic->getStride();
    buffer_height = (height + 31U) & ~31U;
    if (buffer_width < width || buffer_width % 16U != 0 ||
        buffer_height < height || buffer_height % 32U != 0)
        return -1;
    source_size = (size_t)buffer_width * buffer_height * 3U / 2U;
    expected_destination = (size_t)width * height * 4U;
    if (destination_size < expected_destination)
        return -1;

    lock_result = buffer->graphic->lock(
        GRALLOC_USAGE_SW_READ_OFTEN,
        reinterpret_cast<void **>(&source));
    if (lock_result != 0 || source == NULL)
        return -1;
    if (invalidate_cache == NULL) {
        void *symbol;

        if (cache_utility == NULL)
            cache_utility = dlopen(
                "libvcodec_utility.so", RTLD_NOW | RTLD_LOCAL);
        symbol = cache_utility == NULL ? NULL :
            dlsym(cache_utility, "eVideoInvalidateCache");
        if (symbol == NULL || sizeof(invalidate_cache) != sizeof(symbol)) {
            buffer->graphic->unlock();
            return -1;
        }
        memcpy(&invalidate_cache, &symbol, sizeof(invalidate_cache));
    }
    {
        struct val_memory memory;

        memset(&memory, 0, sizeof(memory));
        memory.memory_type = 1U;
        memory.memory_size.extended = source_size;
        memory.memory_va.pointer = source;
        memory.alignment = 1U << 6;
        memory.aligned_memory_va.pointer = source;
        memory.memory_codec = 1U;
        memory.ion_share_fd = UINT32_MAX;
        if (invalidate_cache(&memory, sizeof(memory), 1U) != 0) {
            buffer->graphic->unlock();
            return -1;
        }
    }
    conversion_result = tater_mtk_yuv420_block_to_rgba8888(
        source, source_size, buffer_width, buffer_height, width, height,
        destination, (size_t)width * 4U, destination_size);
    unlock_result = buffer->graphic->unlock();
    return conversion_result == 0 && unlock_result == 0 ? 0 : -1;
}

extern "C" OMX_ERRORTYPE tater_omx_use_native_buffer(
    OMX_COMPONENTTYPE *component, OMX_INDEXTYPE extension_index,
    OMX_U32 port_index, OMX_PTR application_private,
    OMX_BUFFERHEADERTYPE **header,
    struct tater_native_buffer *buffer) {
    android::sp<ANativeWindowBuffer> native_buffer;
    android::UseAndroidNativeBufferParams parameters = {
        sizeof(parameters), {{1, 1, 2, 0}}, port_index,
        application_private, header, native_buffer};

    if (component == NULL || component->SetParameter == NULL ||
        header == NULL || buffer == NULL || buffer->graphic.get() == NULL)
        return OMX_ErrorBadParameter;
    native_buffer = buffer->graphic->getNativeBuffer();
    return component->SetParameter(
        component, extension_index, &parameters);
}

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define VAL_RESULT_NO_ERROR 0
#define VAL_MEM_TYPE_FOR_HW_NONCACHEABLE 3U
#define VAL_MEM_ALIGN_4K (1U << 12)
#define VAL_MEM_CODEC_FOR_VDEC 1U
#define TEST_ALLOCATION_SIZE (1024U * 1024U)

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

typedef int32_t (*video_mem_alloc_fn)(struct val_memory *, uint32_t);
typedef int32_t (*video_mem_free_fn)(struct val_memory *, uint32_t);

_Static_assert(sizeof(struct val_memory) == 88,
               "unexpected ARM32 public VAL_MEMORY_T ABI size");
_Static_assert(sizeof(video_mem_alloc_fn) == sizeof(void *),
               "function pointer cannot be loaded safely");
_Static_assert(sizeof(video_mem_free_fn) == sizeof(void *),
               "function pointer cannot be loaded safely");

static int load_symbol(void *library, const char *name, void *function,
                       size_t function_size) {
    void *symbol = dlsym(library, name);

    if (symbol == NULL) {
        fprintf(stderr, "dlsym failed for %s: %s\n", name, dlerror());
        return -1;
    }
    if (function_size != sizeof(symbol)) {
        fprintf(stderr, "refusing incompatible function pointer size\n");
        return -1;
    }
    memcpy(function, &symbol, function_size);
    return 0;
}

static int mapping_contains(const void *address, size_t size) {
    FILE *maps;
    char line[256];
    uintptr_t first = (uintptr_t)address;
    uintptr_t last;

    if (address == NULL || size == 0 || first > UINTPTR_MAX - (size - 1))
        return 0;
    last = first + size - 1;
    maps = fopen("/proc/self/maps", "r");
    if (maps == NULL)
        return 0;
    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long start;
        unsigned long end;

        if (sscanf(line, "%lx-%lx", &start, &end) == 2 &&
            first >= (uintptr_t)start && last < (uintptr_t)end) {
            fclose(maps);
            return 1;
        }
    }
    fclose(maps);
    return 0;
}

static const char *address_state(const void *address, int mapping_valid) {
    if (address == NULL)
        return "null";
    if (address == MAP_FAILED)
        return "map-failed";
    return mapping_valid ? "mapped" : "unmapped";
}

int main(int argc, char **argv) {
    video_mem_alloc_fn video_mem_alloc;
    video_mem_free_fn video_mem_free;
    struct val_memory memory;
    void *library;
    void *write_address;
    uint64_t hardware_address;
    int base_mapping_valid;
    int aligned_mapping_valid;
    int32_t alloc_result;
    int32_t free_result;

    if (argc != 2 || strcmp(argv[1], "--allocate-free-vdec-buffer") != 0) {
        fprintf(stderr, "refusing: use --allocate-free-vdec-buffer\n");
        return 2;
    }

    library = dlopen("libvcodec_utility.so", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    if (load_symbol(library, "eVideoMemAlloc", &video_mem_alloc,
                    sizeof(video_mem_alloc)) != 0 ||
        load_symbol(library, "eVideoMemFree", &video_mem_free,
                    sizeof(video_mem_free)) != 0) {
        dlclose(library);
        return 2;
    }

    memset(&memory, 0, sizeof(memory));
    memory.memory_type = VAL_MEM_TYPE_FOR_HW_NONCACHEABLE;
    memory.memory_size.extended = TEST_ALLOCATION_SIZE;
    memory.alignment = VAL_MEM_ALIGN_4K;
    memory.memory_codec = VAL_MEM_CODEC_FOR_VDEC;

    alloc_result = video_mem_alloc(&memory, sizeof(memory));
    write_address = memory.aligned_memory_va.pointer != NULL
                        ? memory.aligned_memory_va.pointer
                        : memory.memory_va.pointer;
    hardware_address = memory.aligned_memory_pa.extended != 0
                           ? memory.aligned_memory_pa.extended
                           : memory.memory_pa.extended;
    base_mapping_valid = mapping_contains(memory.memory_va.pointer,
                                          TEST_ALLOCATION_SIZE);
    aligned_mapping_valid = mapping_contains(memory.aligned_memory_va.pointer,
                                             TEST_ALLOCATION_SIZE);
    if (alloc_result != VAL_RESULT_NO_ERROR || write_address == NULL ||
        hardware_address == 0 ||
        (!base_mapping_valid && !aligned_mapping_valid)) {
        fprintf(stderr,
                "allocation unusable: result=%d base=%s aligned=%s pa=%s "
                "size=%llu\n",
                alloc_result,
                address_state(memory.memory_va.pointer, base_mapping_valid),
                address_state(memory.aligned_memory_va.pointer,
                              aligned_mapping_valid),
                hardware_address == 0 ? "zero" : "non-zero",
                (unsigned long long)memory.memory_size.extended);
        if (alloc_result == VAL_RESULT_NO_ERROR)
            video_mem_free(&memory, sizeof(memory));
        dlclose(library);
        return 1;
    }

    write_address = aligned_mapping_valid ? memory.aligned_memory_va.pointer
                                          : memory.memory_va.pointer;
    ((volatile uint8_t *)write_address)[0] = 0xa5;
    ((volatile uint8_t *)write_address)[TEST_ALLOCATION_SIZE - 1] = 0x5a;
    if (((const volatile uint8_t *)write_address)[0] != 0xa5 ||
        ((const volatile uint8_t *)write_address)[TEST_ALLOCATION_SIZE - 1] !=
            0x5a) {
        fprintf(stderr, "allocated mapping failed host read/write check\n");
        video_mem_free(&memory, sizeof(memory));
        dlclose(library);
        return 1;
    }

    free_result = video_mem_free(&memory, sizeof(memory));
    if (free_result != VAL_RESULT_NO_ERROR) {
        fprintf(stderr, "free failed: result=%d\n", free_result);
        dlclose(library);
        return 1;
    }

    printf("{\"abi_size\":%zu,\"codec\":\"vdec\",\"requested_bytes\":%u,"
           "\"allocated_bytes\":%llu,"
           "\"allocation\":\"ok\",\"mapping\":\"read-write\","
           "\"base_mapping\":\"%s\",\"aligned_mapping\":\"%s\","
           "\"physical_address\":\"available\",\"free\":\"ok\"}\n",
           sizeof(memory), TEST_ALLOCATION_SIZE,
           (unsigned long long)memory.memory_size.extended,
           base_mapping_valid ? "valid" : "not-valid",
           aligned_mapping_valid ? "valid" : "not-valid");
    dlclose(library);
    return 0;
}

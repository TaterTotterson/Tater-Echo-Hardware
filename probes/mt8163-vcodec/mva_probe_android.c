#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TEST_MAPPING_SIZE (1024U * 1024U)
#define VAL_MEM_CODEC_FOR_VDEC 1U

struct m4u_buffer_config {
    uint32_t memory_codec;
    uint32_t cache_coherent;
    uint32_t security;
};

typedef uint32_t (*init_mva_fn)(void **);
typedef uint32_t (*alloc_mva_fn)(void *, uint32_t, uint32_t *, uint32_t,
                                 struct m4u_buffer_config *);
typedef uint32_t (*free_mva_fn)(void *, uint32_t, uint32_t, uint32_t,
                                struct m4u_buffer_config *);
typedef uint32_t (*deinit_mva_fn)(void *);

_Static_assert(sizeof(struct m4u_buffer_config) == 12,
               "unexpected public M4U config ABI size");
_Static_assert(sizeof(init_mva_fn) == sizeof(void *),
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

int main(int argc, char **argv) {
    init_mva_fn init_mva;
    alloc_mva_fn alloc_mva;
    free_mva_fn free_mva;
    deinit_mva_fn deinit_mva;
    struct m4u_buffer_config config = {
        .memory_codec = VAL_MEM_CODEC_FOR_VDEC,
        .cache_coherent = 0,
        .security = 0,
    };
    void *library;
    void *handle = NULL;
    void *mapping = MAP_FAILED;
    uint32_t mva = 0;
    uint32_t init_result;
    uint32_t alloc_result;
    uint32_t free_result;
    uint32_t deinit_result;
    int result = 1;

    if (argc != 2 || strcmp(argv[1], "--map-unmap-vdec-buffer") != 0) {
        fprintf(stderr, "refusing: use --map-unmap-vdec-buffer\n");
        return 2;
    }

    library = dlopen("libvcodec_utility.so", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    if (load_symbol(library, "eVideoInitMVA", &init_mva,
                    sizeof(init_mva)) != 0 ||
        load_symbol(library, "eVideoAllocMVA", &alloc_mva,
                    sizeof(alloc_mva)) != 0 ||
        load_symbol(library, "eVideoFreeMVA", &free_mva,
                    sizeof(free_mva)) != 0 ||
        load_symbol(library, "eVideoDeInitMVA", &deinit_mva,
                    sizeof(deinit_mva)) != 0) {
        dlclose(library);
        return 2;
    }

    mapping = mmap(NULL, TEST_MAPPING_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }
    memset(mapping, 0x3c, TEST_MAPPING_SIZE);

    init_result = init_mva(&handle);
    if (handle == NULL) {
        fprintf(stderr, "MVA init failed: result=%u handle=null\n",
                init_result);
        goto cleanup;
    }
    alloc_result = alloc_mva(handle, (uint32_t)(uintptr_t)mapping, &mva,
                             TEST_MAPPING_SIZE, &config);
    if (mva == 0) {
        fprintf(stderr, "MVA allocation failed: result=%u mva=zero\n",
                alloc_result);
        goto cleanup_mva;
    }

    free_result = free_mva(handle, (uint32_t)(uintptr_t)mapping, mva,
                           TEST_MAPPING_SIZE, &config);
    mva = 0;
    deinit_result = deinit_mva(handle);
    handle = NULL;
    if (free_result != 0 || deinit_result != 0) {
        fprintf(stderr, "MVA cleanup failed: free=%u deinit=%u\n",
                free_result, deinit_result);
        goto cleanup;
    }

    printf("{\"abi_size\":%zu,\"codec\":\"vdec\",\"bytes\":%u,"
           "\"cpu_mapping\":\"read-write\",\"mva\":\"available\","
           "\"unmap\":\"ok\"}\n",
           sizeof(config), TEST_MAPPING_SIZE);
    result = 0;
    goto cleanup;

cleanup_mva:
    if (mva != 0)
        (void)free_mva(handle, (uint32_t)(uintptr_t)mapping, mva,
                       TEST_MAPPING_SIZE, &config);
    if (handle != NULL)
        (void)deinit_mva(handle);
cleanup:
    if (mapping != MAP_FAILED)
        (void)munmap(mapping, TEST_MAPPING_SIZE);
    dlclose(library);
    return result;
}

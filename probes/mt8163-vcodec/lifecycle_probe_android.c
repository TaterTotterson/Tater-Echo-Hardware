#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VDEC_FORMAT_H264 (1U << 8)
#define VDEC_RESULT_OK 0

typedef int32_t (*create_decoder_fn)(void **, uint32_t);
typedef int32_t (*release_decoder_fn)(void *);

_Static_assert(sizeof(create_decoder_fn) == sizeof(void *),
               "function pointer cannot be loaded safely");
_Static_assert(sizeof(release_decoder_fn) == sizeof(void *),
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
    create_decoder_fn create_decoder;
    release_decoder_fn release_decoder;
    void *decoder = NULL;
    void *library;
    int32_t create_result;
    int32_t release_result;

    if (argc != 3 || strcmp(argv[1], "--create-release") != 0 ||
        strcmp(argv[2], "h264") != 0) {
        fprintf(stderr, "refusing: use --create-release h264\n");
        return 2;
    }

    library = dlopen("libvcodecdrv.so", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    if (load_symbol(library, "eVDecDrvCreate", &create_decoder,
                    sizeof(create_decoder)) != 0 ||
        load_symbol(library, "eVDecDrvRelease", &release_decoder,
                    sizeof(release_decoder)) != 0) {
        dlclose(library);
        return 2;
    }

    create_result = create_decoder(&decoder, VDEC_FORMAT_H264);
    if (create_result != VDEC_RESULT_OK || decoder == NULL) {
        fprintf(stderr, "create failed: result=%d handle=%s\n", create_result,
                decoder == NULL ? "null" : "non-null");
        dlclose(library);
        return 1;
    }

    release_result = release_decoder(decoder);
    decoder = NULL;
    if (release_result != VDEC_RESULT_OK) {
        fprintf(stderr, "release failed: result=%d\n", release_result);
        dlclose(library);
        return 1;
    }

    puts("{\"format\":\"h264\",\"create\":\"ok\",\"release\":\"ok\"}");
    dlclose(library);
    return 0;
}

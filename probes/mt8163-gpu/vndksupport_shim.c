#include <dlfcn.h>

__attribute__((visibility("default")))
void *android_load_sphal_library(const char *name, int flags) {
    return dlopen(name, flags);
}

__attribute__((visibility("default")))
int android_unload_sphal_library(void *handle) {
    return dlclose(handle);
}

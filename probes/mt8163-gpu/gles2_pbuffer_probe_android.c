#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef TATER_GPU_CRONOS
#include "cronos_target.h"
#define GPU_TARGET "cronos"
#else
#define GPU_TARGET "checkers"
#endif

#define EGL_FALSE 0U
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_OPENGL_ES_API 0x30a0
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_SUCCESS 0x3000

#define GL_NO_ERROR 0U
#define GL_COLOR_BUFFER_BIT 0x00004000U
#define GL_RGBA 0x1908U
#define GL_UNSIGNED_BYTE 0x1401U
#define GL_VENDOR 0x1f00U
#define GL_RENDERER 0x1f01U
#define GL_VERSION 0x1f02U

#define WIDTH 32
#define HEIGHT 32
#define PIXEL_BYTES 4
#define MALI_NODE "/dev/mali0"

typedef void *egl_display;
typedef void *egl_config;
typedef void *egl_surface;
typedef void *egl_context;
typedef void *egl_native_display;
typedef int32_t egl_int;
typedef uint32_t egl_boolean;
typedef uint32_t gl_enum;
typedef int32_t gl_size;
typedef float gl_float;

typedef egl_display (*egl_get_display_fn)(egl_native_display);
typedef egl_boolean (*egl_initialize_fn)(egl_display, egl_int *, egl_int *);
typedef egl_boolean (*egl_choose_config_fn)(egl_display, const egl_int *,
                                             egl_config *, egl_int, egl_int *);
typedef egl_boolean (*egl_bind_api_fn)(gl_enum);
typedef egl_surface (*egl_create_pbuffer_surface_fn)(egl_display, egl_config,
                                                      const egl_int *);
typedef egl_context (*egl_create_context_fn)(egl_display, egl_config,
                                              egl_context, const egl_int *);
typedef egl_boolean (*egl_make_current_fn)(egl_display, egl_surface,
                                            egl_surface, egl_context);
typedef egl_boolean (*egl_destroy_surface_fn)(egl_display, egl_surface);
typedef egl_boolean (*egl_destroy_context_fn)(egl_display, egl_context);
typedef egl_boolean (*egl_terminate_fn)(egl_display);
typedef egl_int (*egl_get_error_fn)(void);
typedef const char *(*egl_query_string_fn)(egl_display, egl_int);
typedef void (*gl_viewport_fn)(int32_t, int32_t, gl_size, gl_size);
typedef void (*gl_clear_color_fn)(gl_float, gl_float, gl_float, gl_float);
typedef void (*gl_clear_fn)(uint32_t);
typedef void (*gl_finish_fn)(void);
typedef void (*gl_read_pixels_fn)(int32_t, int32_t, gl_size, gl_size, gl_enum,
                                   gl_enum, void *);
typedef gl_enum (*gl_get_error_fn)(void);
typedef const uint8_t *(*gl_get_string_fn)(gl_enum);

struct api {
    egl_get_display_fn egl_get_display;
    egl_initialize_fn egl_initialize;
    egl_choose_config_fn egl_choose_config;
    egl_bind_api_fn egl_bind_api;
    egl_create_pbuffer_surface_fn egl_create_pbuffer_surface;
    egl_create_context_fn egl_create_context;
    egl_make_current_fn egl_make_current;
    egl_destroy_surface_fn egl_destroy_surface;
    egl_destroy_context_fn egl_destroy_context;
    egl_terminate_fn egl_terminate;
    egl_get_error_fn egl_get_error;
    egl_query_string_fn egl_query_string;
    gl_viewport_fn gl_viewport;
    gl_clear_color_fn gl_clear_color;
    gl_clear_fn gl_clear;
    gl_finish_fn gl_finish;
    gl_read_pixels_fn gl_read_pixels;
    gl_get_error_fn gl_get_error;
    gl_get_string_fn gl_get_string;
};

static int load_function(void *library, const char *name, void *function,
                         size_t function_size) {
    void *symbol = dlsym(library, name);

    if (symbol == NULL || function_size != sizeof(symbol)) {
        fprintf(stderr, "could not load %s: %s\n", name,
                symbol == NULL ? dlerror() : "function pointer size mismatch");
        return -1;
    }
    memcpy(function, &symbol, function_size);
    return 0;
}

#define LOAD(API, LIBRARY, MEMBER, NAME)                                      \
    load_function((LIBRARY), (NAME), &(API).MEMBER, sizeof((API).MEMBER))

static int load_api(void *library, struct api *api) {
    memset(api, 0, sizeof(*api));
    return LOAD(*api, library, egl_get_display, "eglGetDisplay") ||
           LOAD(*api, library, egl_initialize, "eglInitialize") ||
           LOAD(*api, library, egl_choose_config, "eglChooseConfig") ||
           LOAD(*api, library, egl_bind_api, "eglBindAPI") ||
           LOAD(*api, library, egl_create_pbuffer_surface,
                "eglCreatePbufferSurface") ||
           LOAD(*api, library, egl_create_context, "eglCreateContext") ||
           LOAD(*api, library, egl_make_current, "eglMakeCurrent") ||
           LOAD(*api, library, egl_destroy_surface, "eglDestroySurface") ||
           LOAD(*api, library, egl_destroy_context, "eglDestroyContext") ||
           LOAD(*api, library, egl_terminate, "eglTerminate") ||
           LOAD(*api, library, egl_get_error, "eglGetError") ||
           LOAD(*api, library, egl_query_string, "eglQueryString") ||
           LOAD(*api, library, gl_viewport, "glViewport") ||
           LOAD(*api, library, gl_clear_color, "glClearColor") ||
           LOAD(*api, library, gl_clear, "glClear") ||
           LOAD(*api, library, gl_finish, "glFinish") ||
           LOAD(*api, library, gl_read_pixels, "glReadPixels") ||
           LOAD(*api, library, gl_get_error, "glGetError") ||
           LOAD(*api, library, gl_get_string, "glGetString");
}

static uint32_t fnv1a32(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261U;
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

static int near_channel(uint8_t actual, uint8_t expected) {
    int difference = (int)actual - (int)expected;

    return difference >= -1 && difference <= 1;
}

static const char *safe_string(const uint8_t *value) {
    return value == NULL ? "unavailable" : (const char *)value;
}

int main(int argc, char **argv) {
    static const egl_int config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE,
    };
    static const egl_int pbuffer_attributes[] = {
        EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE,
    };
    static const egl_int context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE,
    };
    struct stat node_status;
    struct api api;
    void *library = NULL;
    egl_display display = NULL;
    egl_config config = NULL;
    egl_surface surface = NULL;
    egl_context context = NULL;
    egl_int config_count = 0;
    egl_int major = 0;
    egl_int minor = 0;
    uint8_t pixels[WIDTH * HEIGHT * PIXEL_BYTES];
    size_t index;
    uint32_t matching_pixels = 0;
    int result = 1;

    if (argc != 7 || strcmp(argv[1], "--offscreen-gles2-pbuffer") != 0 ||
        strcmp(argv[2], "--target") != 0 ||
        strcmp(argv[3], GPU_TARGET) != 0 ||
        strcmp(argv[4], "--acknowledge-gpu-job") != 0 ||
        strcmp(argv[5], "private-pbuffer-no-display") != 0 ||
        strcmp(argv[6], "run") != 0) {
        fprintf(stderr,
                "refusing: use --offscreen-gles2-pbuffer --target " GPU_TARGET " "
                "--acknowledge-gpu-job private-pbuffer-no-display run\n");
        return 2;
    }
#ifdef TATER_GPU_CRONOS
    if (!cronos_gpu_target_allowed()) {
        fprintf(stderr, "refusing: Cronos kernel/product/panel preflight failed\n");
        return 2;
    }
#endif
    if (stat(MALI_NODE, &node_status) != 0 ||
        !S_ISCHR(node_status.st_mode)) {
        fprintf(stderr, "target check failed: %s is not a character device\n",
                MALI_NODE);
        return 2;
    }

    alarm(20);
    library = dlopen("libGLES_mali.so", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        goto cleanup;
    }
    if (load_api(library, &api) != 0)
        goto cleanup;

    display = api.egl_get_display(NULL);
    if (display == NULL ||
        api.egl_initialize(display, &major, &minor) == EGL_FALSE ||
        api.egl_choose_config(display, config_attributes, &config, 1,
                              &config_count) == EGL_FALSE ||
        config_count != 1 ||
        api.egl_bind_api(EGL_OPENGL_ES_API) == EGL_FALSE) {
        fprintf(stderr, "EGL initialization failed: error=0x%x\n",
                (unsigned)api.egl_get_error());
        goto cleanup;
    }
    surface = api.egl_create_pbuffer_surface(display, config,
                                              pbuffer_attributes);
    context = api.egl_create_context(display, config, NULL,
                                     context_attributes);
    if (surface == NULL || context == NULL ||
        api.egl_make_current(display, surface, surface, context) == EGL_FALSE) {
        fprintf(stderr, "EGL context failed: error=0x%x\n",
                (unsigned)api.egl_get_error());
        goto cleanup;
    }

    memset(pixels, 0, sizeof(pixels));
    api.gl_viewport(0, 0, WIDTH, HEIGHT);
    api.gl_clear_color(0.1f, 0.4f, 0.7f, 1.0f);
    api.gl_clear(GL_COLOR_BUFFER_BIT);
    api.gl_finish();
    api.gl_read_pixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                       pixels);
    if (api.gl_get_error() != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL ES readback failed\n");
        goto cleanup;
    }
    for (index = 0; index < WIDTH * HEIGHT; ++index) {
        const uint8_t *pixel = &pixels[index * PIXEL_BYTES];

        if (near_channel(pixel[0], 26) && near_channel(pixel[1], 102) &&
            near_channel(pixel[2], 179) && near_channel(pixel[3], 255))
            ++matching_pixels;
    }
    if (matching_pixels != WIDTH * HEIGHT) {
        fprintf(stderr, "GPU readback mismatch: %u of %u pixels matched\n",
                matching_pixels, WIDTH * HEIGHT);
        goto cleanup;
    }

    printf("{\"egl\":\"%d.%d\",\"egl_vendor\":\"%s\","
           "\"gl_vendor\":\"%s\",\"gl_renderer\":\"%s\","
           "\"gl_version\":\"%s\",\"surface\":\"pbuffer\","
           "\"size\":\"%dx%d\",\"matching_pixels\":%u,"
           "\"pixel_hash\":%u,\"display_touched\":false}\n",
           major, minor,
           safe_string((const uint8_t *)api.egl_query_string(display,
                                                              EGL_VENDOR)),
           safe_string(api.gl_get_string(GL_VENDOR)),
           safe_string(api.gl_get_string(GL_RENDERER)),
           safe_string(api.gl_get_string(GL_VERSION)), WIDTH, HEIGHT,
           matching_pixels, fnv1a32(pixels, sizeof(pixels)));
    result = 0;

cleanup:
    if (display != NULL && context != NULL)
        (void)api.egl_make_current(display, NULL, NULL, NULL);
    if (display != NULL && context != NULL)
        (void)api.egl_destroy_context(display, context);
    if (display != NULL && surface != NULL)
        (void)api.egl_destroy_surface(display, surface);
    if (display != NULL)
        (void)api.egl_terminate(display);
    if (library != NULL)
        dlclose(library);
    alarm(0);
    return result;
}

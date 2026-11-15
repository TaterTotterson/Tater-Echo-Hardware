#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(TATER_GPU_ROOK)
#include "rook_target.h"
#define GPU_TARGET "rook"
#elif defined(TATER_GPU_ATHENA)
#include "athena_target.h"
#define GPU_TARGET "athena"
#elif defined(TATER_GPU_CROWN)
#include "crown_target.h"
#define GPU_TARGET "crown"
#elif defined(TATER_GPU_CRONOS)
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

#define GL_NO_ERROR 0U
#define GL_COLOR_BUFFER_BIT 0x00004000U
#define GL_FLOAT 0x1406U
#define GL_FRAGMENT_SHADER 0x8b30U
#define GL_VERTEX_SHADER 0x8b31U
#define GL_COMPILE_STATUS 0x8b81U
#define GL_LINK_STATUS 0x8b82U
#define GL_TEXTURE_2D 0x0de1U
#define GL_TEXTURE_MIN_FILTER 0x2801U
#define GL_TEXTURE_MAG_FILTER 0x2800U
#define GL_TEXTURE_WRAP_S 0x2802U
#define GL_TEXTURE_WRAP_T 0x2803U
#define GL_NEAREST 0x2600U
#define GL_CLAMP_TO_EDGE 0x812fU
#define GL_RGBA 0x1908U
#define GL_UNSIGNED_BYTE 0x1401U
#define GL_UNPACK_ALIGNMENT 0x0cf5U
#define GL_TRIANGLE_STRIP 0x0005U
#define GL_TEXTURE0 0x84c0U
#define GL_TEXTURE1 0x84c1U
#define GL_TEXTURE2 0x84c2U
#define GL_LINEAR 0x2601U
#define GL_TRIANGLES 0x0004U
#define GL_BLEND 0x0be2U
#define GL_ONE 1U
#define GL_ONE_MINUS_SRC_ALPHA 0x0303U
#define MAX_TEXT_QUADS 96

#if defined(TATER_GPU_ROOK)
#define SOURCE_WIDTH 480
#define SOURCE_HEIGHT 480
#define PANEL_WIDTH 480
#define PANEL_HEIGHT 480
#define GLSL_PANEL_WIDTH "480.0"
#define GLSL_PANEL_SIZE "vec2(480.0,480.0)"
#define GLSL_SOURCE_SIZE "vec2(480.0,480.0)"
#define GLSL_DOT_X "240.0"
#define GLSL_DOT_Y "360.0"
#define MAX_ATLAS_WIDTH 1024U
#elif defined(TATER_GPU_CROWN) || defined(TATER_GPU_ATHENA)
#define SOURCE_WIDTH 1280
#define SOURCE_HEIGHT 800
#define PANEL_WIDTH 800
#define PANEL_HEIGHT 1280
#define GLSL_PANEL_WIDTH "800.0"
#define GLSL_PANEL_SIZE "vec2(800.0,1280.0)"
#define GLSL_SOURCE_SIZE "vec2(1280.0,800.0)"
#define GLSL_DOT_X "640.0"
#define GLSL_DOT_Y "576.0"
#define MAX_ATLAS_WIDTH 2048U
#else
#define SOURCE_WIDTH 960
#define SOURCE_HEIGHT 480
#define PANEL_WIDTH 480
#define PANEL_HEIGHT 960
#define GLSL_PANEL_WIDTH "480.0"
#define GLSL_PANEL_SIZE "vec2(480.0,960.0)"
#define GLSL_SOURCE_SIZE "vec2(960.0,480.0)"
#define GLSL_DOT_X "480.0"
#define GLSL_DOT_Y "345.6"
#define MAX_ATLAS_WIDTH 1024U
#endif
#define PIXEL_BYTES 4
#define SOURCE_BYTES ((uint32_t)(SOURCE_WIDTH * SOURCE_HEIGHT * PIXEL_BYTES))
#define PANEL_BYTES ((uint32_t)(PANEL_WIDTH * PANEL_HEIGHT * PIXEL_BYTES))
#define MAX_VIDEO_WIDTH 1920U
#define MAX_VIDEO_HEIGHT 1088U
#define MAX_VIDEO_BYTES (MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT * PIXEL_BYTES)
#ifdef TATER_GPU_ATHENA
#define MALI_NODE "/dev/dri/renderD128"
#else
#define MALI_NODE "/dev/mali0"
#endif

typedef void *egl_display;
typedef void *egl_config;
typedef void *egl_surface;
typedef void *egl_context;
typedef int32_t egl_int;
typedef uint32_t egl_boolean;
typedef uint32_t gl_enum;
typedef uint32_t gl_uint;
typedef int32_t gl_int;
typedef int32_t gl_size;
typedef char gl_char;

struct api {
    egl_display (*egl_get_display)(void *);
    egl_boolean (*egl_initialize)(egl_display, egl_int *, egl_int *);
    egl_boolean (*egl_choose_config)(egl_display, const egl_int *, egl_config *,
                                     egl_int, egl_int *);
    egl_boolean (*egl_bind_api)(gl_enum);
    egl_surface (*egl_create_pbuffer_surface)(egl_display, egl_config,
                                               const egl_int *);
    egl_context (*egl_create_context)(egl_display, egl_config, egl_context,
                                       const egl_int *);
    egl_boolean (*egl_make_current)(egl_display, egl_surface, egl_surface,
                                     egl_context);
    egl_boolean (*egl_destroy_surface)(egl_display, egl_surface);
    egl_boolean (*egl_destroy_context)(egl_display, egl_context);
    egl_boolean (*egl_terminate)(egl_display);
    egl_int (*egl_get_error)(void);
    gl_uint (*gl_create_shader)(gl_enum);
    void (*gl_shader_source)(gl_uint, gl_size, const gl_char *const *,
                             const gl_int *);
    void (*gl_compile_shader)(gl_uint);
    void (*gl_get_shader_iv)(gl_uint, gl_enum, gl_int *);
    void (*gl_get_shader_info_log)(gl_uint, gl_size, gl_size *, gl_char *);
    void (*gl_delete_shader)(gl_uint);
    gl_uint (*gl_create_program)(void);
    void (*gl_attach_shader)(gl_uint, gl_uint);
    void (*gl_bind_attrib_location)(gl_uint, gl_uint, const gl_char *);
    void (*gl_link_program)(gl_uint);
    void (*gl_get_program_iv)(gl_uint, gl_enum, gl_int *);
    void (*gl_get_program_info_log)(gl_uint, gl_size, gl_size *, gl_char *);
    void (*gl_delete_program)(gl_uint);
    void (*gl_use_program)(gl_uint);
    void (*gl_gen_textures)(gl_size, gl_uint *);
    void (*gl_delete_textures)(gl_size, const gl_uint *);
    void (*gl_active_texture)(gl_enum);
    void (*gl_bind_texture)(gl_enum, gl_uint);
    void (*gl_tex_parameter_i)(gl_enum, gl_enum, gl_int);
    void (*gl_pixel_store_i)(gl_enum, gl_int);
    void (*gl_tex_image_2d)(gl_enum, gl_int, gl_int, gl_size, gl_size, gl_int,
                            gl_enum, gl_enum, const void *);
    void (*gl_tex_sub_image_2d)(gl_enum, gl_int, gl_int, gl_int, gl_size,
                                gl_size, gl_enum, gl_enum, const void *);
    gl_int (*gl_get_uniform_location)(gl_uint, const gl_char *);
    void (*gl_uniform_1i)(gl_int, gl_int);
    void (*gl_uniform_1f)(gl_int, float);
    void (*gl_uniform_4f)(gl_int, float, float, float, float);
    void (*gl_copy_tex_sub_image_2d)(gl_enum, gl_int, gl_int, gl_int,
                                    gl_int, gl_int, gl_size, gl_size);
    void (*gl_viewport)(gl_int, gl_int, gl_size, gl_size);
    void (*gl_vertex_attrib_pointer)(gl_uint, gl_int, gl_enum, uint8_t,
                                     gl_size, const void *);
    void (*gl_enable_vertex_attrib_array)(gl_uint);
    void (*gl_disable_vertex_attrib_array)(gl_uint);
    void (*gl_enable)(gl_enum);
    void (*gl_disable)(gl_enum);
    void (*gl_blend_func)(gl_enum, gl_enum);
    void (*gl_draw_arrays)(gl_enum, gl_int, gl_size);
    void (*gl_finish)(void);
    void (*gl_read_pixels)(gl_int, gl_int, gl_size, gl_size, gl_enum, gl_enum,
                           void *);
    gl_enum (*gl_get_error)(void);
};

struct renderer {
    void *library;
#ifdef TATER_GPU_ATHENA
    void *gles_library;
#endif
    struct api api;
    egl_display display;
    egl_surface surface;
    egl_context context;
    gl_uint program;
    gl_uint texture;
    gl_uint previous;
    gl_uint text_program, text_texture;
    int text_width, text_height;
    gl_int blend_uniform, clock_uniform, glow_uniform, features_uniform;
    gl_int video_rect_uniform, video_crop_uniform, video_radius_uniform;
    gl_int video_mode_uniform;
    int texture_width, texture_height;
    int has_frame;
};

static int load_function(void *library, const char *name, void *function,
                         size_t size) {
    void *symbol = dlsym(library, name);
    if (symbol == NULL || size != sizeof(symbol)) {
        fprintf(stderr, "could not load %s: %s\n", name,
                symbol == NULL ? dlerror() : "function pointer mismatch");
        return -1;
    }
    memcpy(function, &symbol, size);
    return 0;
}

#ifdef TATER_GPU_ATHENA
#define LOAD(R, MEMBER, NAME)                                                \
    load_function(strncmp(NAME, "egl", 3) == 0 ? (R)->library : (R)->gles_library, \
                  (NAME), &(R)->api.MEMBER, sizeof((R)->api.MEMBER))
#else
#define LOAD(R, MEMBER, NAME)                                                \
    load_function((R)->library, (NAME), &(R)->api.MEMBER,                   \
                  sizeof((R)->api.MEMBER))
#endif

static int load_api(struct renderer *renderer) {
    return LOAD(renderer, egl_get_display, "eglGetDisplay") ||
           LOAD(renderer, egl_initialize, "eglInitialize") ||
           LOAD(renderer, egl_choose_config, "eglChooseConfig") ||
           LOAD(renderer, egl_bind_api, "eglBindAPI") ||
           LOAD(renderer, egl_create_pbuffer_surface,
                "eglCreatePbufferSurface") ||
           LOAD(renderer, egl_create_context, "eglCreateContext") ||
           LOAD(renderer, egl_make_current, "eglMakeCurrent") ||
           LOAD(renderer, egl_destroy_surface, "eglDestroySurface") ||
           LOAD(renderer, egl_destroy_context, "eglDestroyContext") ||
           LOAD(renderer, egl_terminate, "eglTerminate") ||
           LOAD(renderer, egl_get_error, "eglGetError") ||
           LOAD(renderer, gl_create_shader, "glCreateShader") ||
           LOAD(renderer, gl_shader_source, "glShaderSource") ||
           LOAD(renderer, gl_compile_shader, "glCompileShader") ||
           LOAD(renderer, gl_get_shader_iv, "glGetShaderiv") ||
           LOAD(renderer, gl_get_shader_info_log, "glGetShaderInfoLog") ||
           LOAD(renderer, gl_delete_shader, "glDeleteShader") ||
           LOAD(renderer, gl_create_program, "glCreateProgram") ||
           LOAD(renderer, gl_attach_shader, "glAttachShader") ||
           LOAD(renderer, gl_bind_attrib_location, "glBindAttribLocation") ||
           LOAD(renderer, gl_link_program, "glLinkProgram") ||
           LOAD(renderer, gl_get_program_iv, "glGetProgramiv") ||
           LOAD(renderer, gl_get_program_info_log, "glGetProgramInfoLog") ||
           LOAD(renderer, gl_delete_program, "glDeleteProgram") ||
           LOAD(renderer, gl_use_program, "glUseProgram") ||
           LOAD(renderer, gl_gen_textures, "glGenTextures") ||
           LOAD(renderer, gl_delete_textures, "glDeleteTextures") ||
           LOAD(renderer, gl_active_texture, "glActiveTexture") ||
           LOAD(renderer, gl_bind_texture, "glBindTexture") ||
           LOAD(renderer, gl_tex_parameter_i, "glTexParameteri") ||
           LOAD(renderer, gl_pixel_store_i, "glPixelStorei") ||
           LOAD(renderer, gl_tex_image_2d, "glTexImage2D") ||
           LOAD(renderer, gl_tex_sub_image_2d, "glTexSubImage2D") ||
           LOAD(renderer, gl_get_uniform_location, "glGetUniformLocation") ||
           LOAD(renderer, gl_uniform_1i, "glUniform1i") ||
           LOAD(renderer, gl_uniform_1f, "glUniform1f") ||
           LOAD(renderer, gl_uniform_4f, "glUniform4f") ||
           LOAD(renderer, gl_copy_tex_sub_image_2d, "glCopyTexSubImage2D") ||
           LOAD(renderer, gl_viewport, "glViewport") ||
           LOAD(renderer, gl_vertex_attrib_pointer, "glVertexAttribPointer") ||
           LOAD(renderer, gl_enable_vertex_attrib_array,
                "glEnableVertexAttribArray") ||
           LOAD(renderer, gl_disable_vertex_attrib_array, "glDisableVertexAttribArray") ||
           LOAD(renderer, gl_enable, "glEnable") ||
           LOAD(renderer, gl_disable, "glDisable") ||
           LOAD(renderer, gl_blend_func, "glBlendFunc") ||
           LOAD(renderer, gl_draw_arrays, "glDrawArrays") ||
           LOAD(renderer, gl_finish, "glFinish") ||
           LOAD(renderer, gl_read_pixels, "glReadPixels") ||
           LOAD(renderer, gl_get_error, "glGetError");
}

static gl_uint compile_shader(struct renderer *renderer, gl_enum kind,
                              const char *source) {
    gl_uint shader = renderer->api.gl_create_shader(kind);
    gl_int ok = 0;
    renderer->api.gl_shader_source(shader, 1, &source, NULL);
    renderer->api.gl_compile_shader(shader);
    renderer->api.gl_get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char detail[512] = {0};
        renderer->api.gl_get_shader_info_log(shader, sizeof(detail), NULL,
                                             detail);
        fprintf(stderr, "shader compile failed: %s\n", detail);
        renderer->api.gl_delete_shader(shader);
        return 0;
    }
    return shader;
}

static void restore_base(struct renderer *renderer) {
    static const float vertices[] = {
#ifdef TATER_GPU_ROOK
        -1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1,
#else
        -1, -1, 0, 1, 1, -1, 0, 0, -1, 1, 1, 1, 1, 1, 1, 0,
#endif
    };
    renderer->api.gl_use_program(renderer->program);
    renderer->api.gl_vertex_attrib_pointer(0, 2, GL_FLOAT, 0, 4*sizeof(float), vertices);
    renderer->api.gl_vertex_attrib_pointer(1, 2, GL_FLOAT, 0, 4*sizeof(float), vertices+2);
    renderer->api.gl_disable_vertex_attrib_array(2);
    renderer->api.gl_disable(GL_BLEND);
}

static int initialize_text(struct renderer *renderer) {
    static const char *vertex_source =
        "attribute vec2 position;attribute vec2 texture_coordinate;attribute float opacity;"
        "varying vec2 uv;varying float alpha;void main(){"
#ifdef TATER_GPU_ROOK
        "vec2 panel=position;"
#else
        "vec2 panel=vec2(" GLSL_PANEL_WIDTH "-position.y,position.x);"
#endif
        "gl_Position=vec4(panel/" GLSL_PANEL_SIZE "*2.0-1.0,0.0,1.0);"
        "uv=texture_coordinate;alpha=opacity;}";
    static const char *fragment_source =
        "precision mediump float;varying vec2 uv;varying float alpha;"
        "uniform sampler2D glyphs;void main(){gl_FragColor=texture2D(glyphs,uv).bgra*alpha;}";
    gl_uint vertex = compile_shader(renderer, GL_VERTEX_SHADER, vertex_source);
    gl_uint fragment = compile_shader(renderer, GL_FRAGMENT_SHADER, fragment_source);
    gl_int linked = 0;
    if (!vertex || !fragment)
        return -1;
    renderer->text_program = renderer->api.gl_create_program();
    renderer->api.gl_attach_shader(renderer->text_program, vertex);
    renderer->api.gl_attach_shader(renderer->text_program, fragment);
    renderer->api.gl_bind_attrib_location(renderer->text_program, 0, "position");
    renderer->api.gl_bind_attrib_location(renderer->text_program, 1, "texture_coordinate");
    renderer->api.gl_bind_attrib_location(renderer->text_program, 2, "opacity");
    renderer->api.gl_link_program(renderer->text_program);
    renderer->api.gl_get_program_iv(renderer->text_program, GL_LINK_STATUS, &linked);
    renderer->api.gl_delete_shader(vertex);
    renderer->api.gl_delete_shader(fragment);
    if (!linked)
        return -1;
    renderer->api.gl_use_program(renderer->text_program);
    renderer->api.gl_uniform_1i(renderer->api.gl_get_uniform_location(renderer->text_program, "glyphs"), 2);
    renderer->api.gl_active_texture(GL_TEXTURE2);
    renderer->api.gl_gen_textures(1, &renderer->text_texture);
    renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->text_texture);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    restore_base(renderer);
    return 0;
}

static int initialize_renderer(struct renderer *renderer) {
    static const egl_int config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE,
    };
    static const egl_int surface_attributes[] = {
        EGL_WIDTH, PANEL_WIDTH, EGL_HEIGHT, PANEL_HEIGHT, EGL_NONE,
    };
    static const egl_int context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE,
    };
    static const char *vertex_source =
        "attribute vec2 position; attribute vec2 texture_coordinate;"
        "varying vec2 uv; void main(){gl_Position=vec4(position,0.0,1.0);"
        "uv=texture_coordinate;}";
    static const char *fragment_source =
#ifdef TATER_GPU_ROOK
        "precision highp float; varying vec2 uv; uniform sampler2D frame;"
#else
        "precision mediump float; varying vec2 uv; uniform sampler2D frame;"
#endif
        "uniform sampler2D previous; uniform float blend; uniform float clock;"
        "uniform vec4 glow; uniform vec4 features;"
        "uniform vec4 video_rect;uniform vec4 video_crop;"
        "uniform float video_radius;uniform float video_mode;"
        "float bell(float p,float c,float s){float d=(p-c)/s;return exp(-d*d);}"
        "void main(){vec2 p=uv*" GLSL_SOURCE_SIZE ";vec4 ink;"
        "if(video_mode>0.5){vec2 local=(p-video_rect.xy)/video_rect.zw;"
        "vec2 q=abs(p-(video_rect.xy+video_rect.zw*0.5))-(video_rect.zw*0.5-vec2(video_radius));"
        "float d=length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-video_radius;"
        "if(local.x>=0.0&&local.x<=1.0&&local.y>=0.0&&local.y<=1.0&&d<=0.0){"
        "ink=texture2D(frame,video_crop.xy+local*video_crop.zw);"
        "if(d>-1.5)ink=mix(ink,vec4(0.204,0.208,0.212,1.0),0.9);"
        "}else{ink=vec4(0.067,0.071,0.075,1.0);}}else{ink=texture2D(frame,uv);}"
#ifdef TATER_GPU_ROOK
        "float edge=236.0-length(floor(p)-vec2(240.0));"
#else
        "vec2 e=min(p," GLSL_SOURCE_SIZE "-p);float edge=min(e.x,e.y);"
#endif
        "if(features.x+features.y>0.0&&edge<64.0){"
        "float wave=0.5+0.5*sin(clock*1.8+uv.x*5.0+uv.y*3.0);"
        "float fall=max(0.0,1.0-edge/24.0);"
        "float rim=features.x*(0.047+0.863*glow.a)*fall*fall*(0.9+0.1*wave);"
        "float broad=max(0.0,1.0-edge/64.0);"
        "float pool=0.36+0.40*bell(uv.x,0.53+0.025*sin(clock*0.19),0.19);"
        "float ambient=features.y*(0.055+0.275*pool)*pow(broad,2.2);"
        "vec3 light=mix(glow.rgb,vec3(1.0),0.04+0.10*glow.a);"
        "ink.rgb=mix(ink.rgb,glow.rgb,ambient);ink.rgb=mix(ink.rgb,light,rim);}"
        "if(features.z>0.0&&abs(p.x-" GLSL_DOT_X ")<60.0&&abs(p.y-" GLSL_DOT_Y ")<28.0){"
        "for(int i=0;i<3;i++){float n=float(i);"
        "float pulse=0.5+0.5*sin(clock*3.0-n*0.72);"
        "float radius=5.0+2.0*pulse;"
        "float dist=length(p-vec2(" GLSL_DOT_X "+(n-1.0)*27.0," GLSL_DOT_Y "));"
        "float dot=features.z*(1.0-smoothstep(radius-0.75,radius+0.75,dist));"
        "float halo=features.z*(0.06+0.06*pulse)*exp(-dist*dist/180.0);"
        "ink.rgb=mix(ink.rgb,glow.rgb,max(dot*(0.57+0.41*pulse),halo));}}"
#ifdef TATER_GPU_ROOK
        "if(edge<0.0)ink=vec4(0.0,0.0,0.0,1.0);"
#endif
        "vec4 target=ink.bgra;gl_FragColor=target;if(blend<1.0){"
        "gl_FragColor=mix(texture2D(previous,gl_FragCoord.xy/" GLSL_PANEL_SIZE "),target,blend);}}";
    static const float vertices[] = {
#ifdef TATER_GPU_ROOK
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
#else
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 0.0f,
        -1.0f,  1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
#endif
    };
    egl_config config = NULL;
    egl_int count = 0, major = 0, minor = 0;
    gl_uint vertex = 0, fragment = 0;
    gl_int linked = 0;

    memset(renderer, 0, sizeof(*renderer));
#ifdef TATER_GPU_ATHENA
    renderer->library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    renderer->gles_library = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (renderer->gles_library == NULL) return -1;
#else
    renderer->library = dlopen("libGLES_mali.so", RTLD_NOW | RTLD_LOCAL);
#endif
    if (renderer->library == NULL || load_api(renderer) != 0) {
        fprintf(stderr, "Mali EGL load failed: %s\n", dlerror());
        return -1;
    }
    renderer->display = renderer->api.egl_get_display(NULL);
    if (renderer->display == NULL ||
        renderer->api.egl_initialize(renderer->display, &major, &minor) == EGL_FALSE ||
        renderer->api.egl_choose_config(renderer->display, config_attributes,
                                         &config, 1, &count) == EGL_FALSE ||
        count != 1 ||
        renderer->api.egl_bind_api(EGL_OPENGL_ES_API) == EGL_FALSE) {
        fprintf(stderr, "EGL initialization failed: 0x%x\n",
                (unsigned)renderer->api.egl_get_error());
        return -1;
    }
    renderer->surface = renderer->api.egl_create_pbuffer_surface(
        renderer->display, config, surface_attributes);
    renderer->context = renderer->api.egl_create_context(
        renderer->display, config, NULL, context_attributes);
    if (renderer->surface == NULL || renderer->context == NULL ||
        renderer->api.egl_make_current(renderer->display, renderer->surface,
                                        renderer->surface,
                                        renderer->context) == EGL_FALSE) {
        fprintf(stderr, "EGL context failed: 0x%x\n",
                (unsigned)renderer->api.egl_get_error());
        return -1;
    }
#ifdef TATER_GPU_ATHENA
    const unsigned char *(*get_string)(gl_enum) = NULL;
    if (load_function(renderer->gles_library, "glGetString", &get_string, sizeof(get_string))) return -1;
    const char *gpu = (const char *)get_string(0x1f01U); /* GL_RENDERER */
    if (!gpu || !strstr(gpu, "Mali-G72") || !strstr(gpu, "Panfrost") ||
        strstr(gpu, "llvmpipe") || strstr(gpu, "softpipe")) {
        fprintf(stderr, "refusing non-qualified renderer: %s\n", gpu ? gpu : "none");
        return -1;
    }
    fprintf(stderr, "Athena hardware renderer: %s\n", gpu);
#endif
    vertex = compile_shader(renderer, GL_VERTEX_SHADER, vertex_source);
    fragment = compile_shader(renderer, GL_FRAGMENT_SHADER, fragment_source);
    if (vertex == 0 || fragment == 0)
        return -1;
    renderer->program = renderer->api.gl_create_program();
    renderer->api.gl_attach_shader(renderer->program, vertex);
    renderer->api.gl_attach_shader(renderer->program, fragment);
    renderer->api.gl_bind_attrib_location(renderer->program, 0, "position");
    renderer->api.gl_bind_attrib_location(renderer->program, 1,
                                           "texture_coordinate");
    renderer->api.gl_link_program(renderer->program);
    renderer->api.gl_get_program_iv(renderer->program, GL_LINK_STATUS, &linked);
    renderer->api.gl_delete_shader(vertex);
    renderer->api.gl_delete_shader(fragment);
    if (!linked) {
        char detail[512] = {0};
        renderer->api.gl_get_program_info_log(renderer->program,
                                               sizeof(detail), NULL, detail);
        fprintf(stderr, "program link failed: %s\n", detail);
        return -1;
    }
    renderer->api.gl_use_program(renderer->program);
    renderer->api.gl_active_texture(GL_TEXTURE0);
    renderer->api.gl_gen_textures(1, &renderer->texture);
    renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->texture);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                      GL_NEAREST);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                      GL_NEAREST);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                      GL_CLAMP_TO_EDGE);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                      GL_CLAMP_TO_EDGE);
    renderer->api.gl_pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    renderer->api.gl_tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA, SOURCE_WIDTH,
                                   SOURCE_HEIGHT, 0, GL_RGBA,
                                   GL_UNSIGNED_BYTE, NULL);
    renderer->texture_width = SOURCE_WIDTH;
    renderer->texture_height = SOURCE_HEIGHT;
    renderer->api.gl_uniform_1i(
        renderer->api.gl_get_uniform_location(renderer->program, "frame"), 0);
    renderer->blend_uniform = renderer->api.gl_get_uniform_location(renderer->program, "blend");
    renderer->clock_uniform = renderer->api.gl_get_uniform_location(renderer->program, "clock");
    renderer->glow_uniform = renderer->api.gl_get_uniform_location(renderer->program, "glow");
    renderer->features_uniform = renderer->api.gl_get_uniform_location(renderer->program, "features");
    renderer->video_rect_uniform = renderer->api.gl_get_uniform_location(renderer->program, "video_rect");
    renderer->video_crop_uniform = renderer->api.gl_get_uniform_location(renderer->program, "video_crop");
    renderer->video_radius_uniform = renderer->api.gl_get_uniform_location(renderer->program, "video_radius");
    renderer->video_mode_uniform = renderer->api.gl_get_uniform_location(renderer->program, "video_mode");
    renderer->api.gl_uniform_1i(
        renderer->api.gl_get_uniform_location(renderer->program, "previous"), 1);
    renderer->api.gl_active_texture(GL_TEXTURE1);
    renderer->api.gl_gen_textures(1, &renderer->previous);
    renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->previous);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    renderer->api.gl_tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    renderer->api.gl_tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA, PANEL_WIDTH,
                                 PANEL_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    renderer->api.gl_viewport(0, 0, PANEL_WIDTH, PANEL_HEIGHT);
    renderer->api.gl_vertex_attrib_pointer(0, 2, GL_FLOAT, 0,
                                            4 * (gl_size)sizeof(float),
                                            vertices);
    renderer->api.gl_vertex_attrib_pointer(1, 2, GL_FLOAT, 0,
                                            4 * (gl_size)sizeof(float),
                                            vertices + 2);
    renderer->api.gl_enable_vertex_attrib_array(0);
    renderer->api.gl_enable_vertex_attrib_array(1);
    return initialize_text(renderer) == 0 && renderer->api.gl_get_error() == GL_NO_ERROR ? 0 : -1;
}

static void destroy_renderer(struct renderer *renderer) {
    if (renderer->texture != 0)
        renderer->api.gl_delete_textures(1, &renderer->texture);
    if (renderer->previous != 0)
        renderer->api.gl_delete_textures(1, &renderer->previous);
    if (renderer->text_texture != 0)
        renderer->api.gl_delete_textures(1, &renderer->text_texture);
    if (renderer->text_program != 0)
        renderer->api.gl_delete_program(renderer->text_program);
    if (renderer->program != 0)
        renderer->api.gl_delete_program(renderer->program);
    if (renderer->display != NULL && renderer->context != NULL)
        renderer->api.egl_destroy_context(renderer->display,
                                           renderer->context);
    if (renderer->display != NULL && renderer->surface != NULL)
        renderer->api.egl_destroy_surface(renderer->display,
                                           renderer->surface);
    if (renderer->display != NULL)
        renderer->api.egl_terminate(renderer->display);
    if (renderer->library != NULL)
        dlclose(renderer->library);
#ifdef TATER_GPU_ATHENA
    if (renderer->gles_library != NULL)
        dlclose(renderer->gles_library);
#endif
    memset(renderer, 0, sizeof(*renderer));
}

static void upload_frame(struct renderer *renderer, const uint8_t *source,
                         int width, int height) {
    renderer->api.gl_active_texture(GL_TEXTURE0);
    renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->texture);
    if (renderer->texture_width == width && renderer->texture_height == height) {
        renderer->api.gl_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, width,
                                           height, GL_RGBA, GL_UNSIGNED_BYTE,
                                           source);
    } else {
        renderer->api.gl_tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA, width,
                                      height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                      source);
        renderer->texture_width = width;
        renderer->texture_height = height;
    }
}

static int render_frame(struct renderer *renderer, const uint8_t *source,
                        uint8_t *panel) {
    renderer->api.gl_uniform_1f(renderer->blend_uniform, 1.0f);
    renderer->api.gl_uniform_1f(renderer->video_mode_uniform, 0.0f);
    renderer->api.gl_uniform_4f(renderer->features_uniform, 0, 0, 0, 0);
    upload_frame(renderer, source, SOURCE_WIDTH, SOURCE_HEIGHT);
    renderer->api.gl_draw_arrays(GL_TRIANGLE_STRIP, 0, 4);
    renderer->api.gl_finish();
    renderer->api.gl_read_pixels(0, 0, PANEL_WIDTH, PANEL_HEIGHT, GL_RGBA,
                                  GL_UNSIGNED_BYTE, panel);
    renderer->has_frame = 1;
    return renderer->api.gl_get_error() == GL_NO_ERROR ? 0 : -1;
}

static int read_all(int fd, void *buffer, size_t bytes) {
    uint8_t *next = buffer;
    while (bytes > 0) {
        ssize_t count = read(fd, next, bytes);
        if (count == 0)
            return 1;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        next += count;
        bytes -= (size_t)count;
    }
    return 0;
}

static int write_all(int fd, const void *buffer, size_t bytes) {
    const uint8_t *next = buffer;
    while (bytes > 0) {
        ssize_t count = write(fd, next, bytes);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        next += count;
        bytes -= (size_t)count;
    }
    return 0;
}

static uint32_t le32(const uint8_t *value) {
    return (uint32_t)value[0] | (uint32_t)value[1] << 8 |
           (uint32_t)value[2] << 16 | (uint32_t)value[3] << 24;
}

static void store_le32(uint8_t *value, uint32_t number) {
    value[0] = (uint8_t)number;
    value[1] = (uint8_t)(number >> 8);
    value[2] = (uint8_t)(number >> 16);
    value[3] = (uint8_t)(number >> 24);
}

static float le_float(const uint8_t *value) {
    uint32_t bits = le32(value);
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static unsigned le16(const uint8_t *value) {
    return value[0] | (unsigned)value[1] << 8;
}

static int draw_text(struct renderer *renderer, const uint8_t *quads,
                      unsigned count, float blend) {
    float vertices[MAX_TEXT_QUADS * 6 * 5];
    static const unsigned corners[] = {0, 1, 2, 2, 1, 3};
    unsigned index, vertex;
    if (count == 0)
        return 0;
    if (count > MAX_TEXT_QUADS || renderer->text_width < 1 || renderer->text_height < 1)
        return -1;
    for (index = 0; index < count; ++index) {
        const uint8_t *quad = quads + index*20;
        unsigned sx = le16(quad), sy = le16(quad+2);
        unsigned width = le16(quad+4), height = le16(quad+6);
        float x = le_float(quad+8), y = le_float(quad+12), alpha = le_float(quad+16);
        if (!width || !height || sx+width > (unsigned)renderer->text_width ||
            sy+height > (unsigned)renderer->text_height ||
            !(x >= -1024 && x <= 2048) || !(y >= -1024 && y <= 2048) ||
            !(alpha >= 0 && alpha <= 1))
            return -1;
        for (vertex = 0; vertex < 6; ++vertex) {
            unsigned right = corners[vertex] & 1, bottom = corners[vertex] >> 1;
            float *out = vertices + (index*6+vertex)*5;
            out[0] = x + right*width;
            out[1] = y + bottom*height;
            out[2] = (float)(sx+right*width) / renderer->text_width;
            out[3] = (float)(sy+bottom*height) / renderer->text_height;
            out[4] = alpha * blend;
        }
    }
    renderer->api.gl_use_program(renderer->text_program);
    renderer->api.gl_vertex_attrib_pointer(0, 2, GL_FLOAT, 0, 5*sizeof(float), vertices);
    renderer->api.gl_vertex_attrib_pointer(1, 2, GL_FLOAT, 0, 5*sizeof(float), vertices+2);
    renderer->api.gl_vertex_attrib_pointer(2, 1, GL_FLOAT, 0, 5*sizeof(float), vertices+4);
    renderer->api.gl_enable_vertex_attrib_array(2);
    renderer->api.gl_enable(GL_BLEND);
    renderer->api.gl_blend_func(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    renderer->api.gl_draw_arrays(GL_TRIANGLES, 0, (gl_size)count*6);
    restore_base(renderer);
    return 0;
}

/* Retain text/layout on the GPU. A replacement snapshots the last completed
 * result, so even interrupted fades start at the currently displayed image. */
static int render_scene(struct renderer *renderer, const uint8_t *source,
                         uint8_t *panel, const uint8_t *command,
                         const uint8_t *quads, unsigned quad_count) {
    float blend = le_float(command + 8), clock = le_float(command + 12);
    float level = le_float(command + 16);
    float red = le_float(command + 20), green = le_float(command + 24);
    float blue = le_float(command + 28);
    uint32_t features = le32(command + 32);
    if (!(blend >= 0 && blend <= 1) || !(clock >= 0 && clock <= 1024) ||
        !(level >= 0 && level <= 1) || !(red >= 0 && red <= 1) ||
        !(green >= 0 && green <= 1) || !(blue >= 0 && blue <= 1) ||
        features > 7)
        return -1;
    if (source != NULL) {
        if (renderer->has_frame) {
            renderer->api.gl_active_texture(GL_TEXTURE1);
            renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->previous);
            renderer->api.gl_copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0,
                                                  0, 0, PANEL_WIDTH, PANEL_HEIGHT);
        } else {
            blend = 1.0f;
        }
        upload_frame(renderer, source, SOURCE_WIDTH, SOURCE_HEIGHT);
    } else if (!renderer->has_frame) {
        return -1;
    }
    renderer->api.gl_uniform_1f(renderer->blend_uniform, blend);
    renderer->api.gl_uniform_1f(renderer->video_mode_uniform, 0.0f);
    renderer->api.gl_uniform_1f(renderer->clock_uniform, clock);
    renderer->api.gl_uniform_4f(renderer->glow_uniform, red, green, blue, level);
    renderer->api.gl_uniform_4f(renderer->features_uniform,
                              (features & 1) != 0, (features & 2) != 0,
                              (features & 4) != 0, 0);
    renderer->api.gl_draw_arrays(GL_TRIANGLE_STRIP, 0, 4);
    if (draw_text(renderer, quads, quad_count, blend) != 0)
        return -1;
    renderer->api.gl_finish();
    renderer->api.gl_read_pixels(0, 0, PANEL_WIDTH, PANEL_HEIGHT,
                                 GL_RGBA, GL_UNSIGNED_BYTE, panel);
    renderer->has_frame = 1;
    return renderer->api.gl_get_error() == GL_NO_ERROR ? 0 : -1;
}

static int stream_text_scene(struct renderer *renderer, uint8_t *source,
                               uint8_t *panel, uint8_t *header) {
    uint8_t quads[MAX_TEXT_QUADS*20];
    uint8_t *atlas = NULL;
    uint32_t source_bytes, atlas_bytes, width, height, count;
    if (read_all(STDIN_FILENO, header+8, 48) != 0)
        return -1;
    source_bytes = le32(header+4);
    atlas_bytes = le32(header+36);
    width = le32(header+40);
    height = le32(header+44);
    count = le32(header+48);
    if ((source_bytes != 0 && source_bytes != SOURCE_BYTES) ||
        width < 1 || width > MAX_ATLAS_WIDTH || height < 1 || height > 1024 ||
        (atlas_bytes != 0 && atlas_bytes != width*height*4) ||
        count > MAX_TEXT_QUADS || le32(header+52) != 0 ||
        (source_bytes && read_all(STDIN_FILENO, source, source_bytes) != 0))
        return -1;
    if (atlas_bytes) {
        atlas = malloc(atlas_bytes);
        if (!atlas)
            return -1;
        if (read_all(STDIN_FILENO, atlas, atlas_bytes) != 0) {
            free(atlas);
            return -1;
        }
        renderer->api.gl_active_texture(GL_TEXTURE2);
        renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->text_texture);
        if (renderer->text_width == (int)width && renderer->text_height == (int)height) {
            renderer->api.gl_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, width, height,
                                             GL_RGBA, GL_UNSIGNED_BYTE, atlas);
        } else {
            renderer->api.gl_tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
                                          0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
        }
        free(atlas);
        renderer->text_width = (int)width;
        renderer->text_height = (int)height;
    } else if (renderer->text_width != (int)width || renderer->text_height != (int)height) {
        return -1;
    }
    if (read_all(STDIN_FILENO, quads, count*20) != 0)
        return -1;
    return render_scene(renderer, source_bytes ? source : NULL, panel, header, quads, count);
}

static int render_video_scene(struct renderer *renderer, const uint8_t *source,
                              uint8_t *panel, const uint8_t *header,
                              const uint8_t *quads, unsigned quad_count) {
    uint32_t width = le32(header+8), height = le32(header+12);
    uint32_t source_x = le32(header+16), source_y = le32(header+20);
    uint32_t source_width = le32(header+24), source_height = le32(header+28);
    uint32_t x = le32(header+32), y = le32(header+36);
    uint32_t draw_width = le32(header+40), draw_height = le32(header+44);
    uint32_t radius = le32(header+48);
    if (source != NULL)
        upload_frame(renderer, source, (int)width, (int)height);
    else if (renderer->texture_width != (int)width ||
             renderer->texture_height != (int)height)
        return -1;
    renderer->api.gl_use_program(renderer->program);
    renderer->api.gl_uniform_1f(renderer->blend_uniform, 1.0f);
    renderer->api.gl_uniform_1f(renderer->clock_uniform, 0.0f);
    renderer->api.gl_uniform_4f(renderer->features_uniform, 0, 0, 0, 0);
    renderer->api.gl_uniform_1f(renderer->video_mode_uniform, 1.0f);
    renderer->api.gl_uniform_4f(renderer->video_rect_uniform,
                               (float)x, (float)y,
                               (float)draw_width, (float)draw_height);
    renderer->api.gl_uniform_4f(renderer->video_crop_uniform,
                               (float)source_x / width,
                               (float)source_y / height,
                               (float)source_width / width,
                               (float)source_height / height);
    renderer->api.gl_uniform_1f(renderer->video_radius_uniform, (float)radius);
    renderer->api.gl_draw_arrays(GL_TRIANGLE_STRIP, 0, 4);
    if (draw_text(renderer, quads, quad_count, 1.0f) != 0)
        return -1;
    renderer->api.gl_finish();
    renderer->api.gl_read_pixels(0, 0, PANEL_WIDTH, PANEL_HEIGHT,
                                 GL_RGBA, GL_UNSIGNED_BYTE, panel);
    renderer->has_frame = 1;
    return renderer->api.gl_get_error() == GL_NO_ERROR ? 0 : -1;
}

static int stream_video_scene(struct renderer *renderer, uint8_t **video,
                              size_t *video_capacity, uint8_t *panel,
                              uint8_t *header) {
    uint8_t quads[MAX_TEXT_QUADS*20];
    uint8_t *atlas = NULL;
    uint32_t source_bytes, width, height, source_x, source_y;
    uint32_t source_width, source_height, x, y, draw_width, draw_height;
    uint32_t radius, atlas_bytes, atlas_width, atlas_height, count;
    if (read_all(STDIN_FILENO, header+8, 64) != 0)
        return -1;
    source_bytes = le32(header+4);
    width = le32(header+8);
    height = le32(header+12);
    source_x = le32(header+16);
    source_y = le32(header+20);
    source_width = le32(header+24);
    source_height = le32(header+28);
    x = le32(header+32);
    y = le32(header+36);
    draw_width = le32(header+40);
    draw_height = le32(header+44);
    radius = le32(header+48);
    atlas_bytes = le32(header+52);
    atlas_width = le32(header+56);
    atlas_height = le32(header+60);
    count = le32(header+64);
    if (width < 16 || width > MAX_VIDEO_WIDTH || height < 16 ||
        height > MAX_VIDEO_HEIGHT || (width & 1) || (height & 1) ||
        (source_bytes != 0 && source_bytes != width*height*4) ||
        source_bytes > MAX_VIDEO_BYTES || !source_width || !source_height ||
        source_x > width || source_width > width-source_x ||
        source_y > height || source_height > height-source_y ||
        !draw_width || !draw_height || x > SOURCE_WIDTH ||
        draw_width > SOURCE_WIDTH-x || y > SOURCE_HEIGHT ||
        draw_height > SOURCE_HEIGHT-y ||
        radius > draw_width/2 || radius > draw_height/2 ||
        atlas_width < 1 || atlas_width > MAX_ATLAS_WIDTH ||
        atlas_height < 1 || atlas_height > 1024 ||
        (atlas_bytes != 0 && atlas_bytes != atlas_width*atlas_height*4) ||
        count > MAX_TEXT_QUADS || le32(header+68) != 0)
        return -1;
    if (source_bytes) {
        if (*video_capacity < source_bytes) {
            uint8_t *replacement = realloc(*video, source_bytes);
            if (!replacement)
                return -1;
            *video = replacement;
            *video_capacity = source_bytes;
        }
        if (read_all(STDIN_FILENO, *video, source_bytes) != 0)
            return -1;
    }
    if (atlas_bytes) {
        atlas = malloc(atlas_bytes);
        if (!atlas)
            return -1;
        if (read_all(STDIN_FILENO, atlas, atlas_bytes) != 0) {
            free(atlas);
            return -1;
        }
        renderer->api.gl_active_texture(GL_TEXTURE2);
        renderer->api.gl_bind_texture(GL_TEXTURE_2D, renderer->text_texture);
        if (renderer->text_width == (int)atlas_width &&
            renderer->text_height == (int)atlas_height) {
            renderer->api.gl_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0,
                                              atlas_width, atlas_height,
                                              GL_RGBA, GL_UNSIGNED_BYTE, atlas);
        } else {
            renderer->api.gl_tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA,
                                          atlas_width, atlas_height, 0,
                                          GL_RGBA, GL_UNSIGNED_BYTE, atlas);
        }
        free(atlas);
        renderer->text_width = (int)atlas_width;
        renderer->text_height = (int)atlas_height;
    } else if (renderer->text_width != (int)atlas_width ||
               renderer->text_height != (int)atlas_height) {
        return -1;
    }
    if (read_all(STDIN_FILENO, quads, count*20) != 0)
        return -1;
    return render_video_scene(renderer, source_bytes ? *video : NULL,
                              panel, header, quads, count);
}

static int self_test(struct renderer *renderer, uint8_t *source,
                     uint8_t *panel) {
    uint32_t matches = 0;
    int x, y;
    for (y = 0; y < SOURCE_HEIGHT; ++y) {
        for (x = 0; x < SOURCE_WIDTH; ++x) {
            uint8_t *pixel = source + ((size_t)y * SOURCE_WIDTH + x) * 4;
            pixel[0] = (uint8_t)x;
            pixel[1] = (uint8_t)y;
            pixel[2] = (uint8_t)(x ^ y);
            pixel[3] = 255;
        }
    }
    if (render_frame(renderer, source, panel) != 0)
        return -1;
    for (y = 0; y < PANEL_HEIGHT; ++y) {
        for (x = 0; x < PANEL_WIDTH; ++x) {
            int source_x = y;
            int source_y = SOURCE_HEIGHT - 1 - x;
#ifdef TATER_GPU_ROOK
            source_x = x;
            source_y = y;
            if ((x-240)*(x-240)+(y-240)*(y-240)>236*236) {
                const uint8_t *pixel = panel + ((size_t)y * PANEL_WIDTH + x) * 4;
                if (!pixel[0] && !pixel[1] && !pixel[2] && pixel[3] == 255) ++matches;
                continue;
            }
#endif
            const uint8_t *pixel = panel + ((size_t)y * PANEL_WIDTH + x) * 4;
            if (pixel[0] == (uint8_t)(source_x ^ source_y) &&
                pixel[1] == (uint8_t)source_y &&
                pixel[2] == (uint8_t)source_x && pixel[3] == 255)
                ++matches;
        }
    }
    printf("{\"adapter\":\"tater-gpu-rotate\",\"version\":1,"
#ifdef TATER_GPU_ROOK
           "\"source\":\"480x480-rgba\",\"output\":\"480x480-bgra\","
#elif defined(TATER_GPU_ATHENA)
           "\"source\":\"1280x800-rgba\",\"output\":\"800x1280-bgra\","
#else
           "\"source\":\"960x480-rgba\",\"output\":\"480x960-bgra\","
#endif
           "\"matching_pixels\":%u,\"total_pixels\":%u}\n",
           matches, (unsigned)(PANEL_WIDTH * PANEL_HEIGHT));
    return matches == PANEL_WIDTH * PANEL_HEIGHT ? 0 : -1;
}

static uint64_t monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0;
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
}

static int benchmark(struct renderer *renderer, uint8_t *source,
                     uint8_t *panel) {
    const unsigned frames = 60;
    uint64_t total_ns = 0, maximum_ns = 0;
    unsigned frame;
    size_t index;

    for (index = 0; index < SOURCE_BYTES; index += 4) {
        source[index + 0] = (uint8_t)index;
        source[index + 1] = (uint8_t)(index >> 8);
        source[index + 2] = (uint8_t)(index >> 16);
        source[index + 3] = 255;
    }
    /* Warm the context and allocations before reporting steady-state work. */
    for (frame = 0; frame < 3; ++frame) {
        source[frame * 4] ^= (uint8_t)(frame + 1);
        if (render_frame(renderer, source, panel) != 0)
            return -1;
    }
    for (frame = 0; frame < frames; ++frame) {
        uint64_t started, elapsed;
        source[(frame * 7919U % (SOURCE_WIDTH * SOURCE_HEIGHT)) * 4] ^=
            (uint8_t)(frame + 1);
        started = monotonic_ns();
        if (started == 0 || render_frame(renderer, source, panel) != 0)
            return -1;
        elapsed = monotonic_ns() - started;
        total_ns += elapsed;
        if (elapsed > maximum_ns)
            maximum_ns = elapsed;
    }
    printf("{\"adapter\":\"tater-gpu-rotate\",\"version\":1,"
           "\"benchmark\":\"upload-draw-readback\",\"frames\":%u,"
           "\"average_us\":%llu,\"maximum_us\":%llu}\n",
           frames, (unsigned long long)(total_ns / frames / 1000ULL),
           (unsigned long long)(maximum_ns / 1000ULL));
    return 0;
}

static int stream_frames(struct renderer *renderer, uint8_t *source,
                         uint8_t *panel) {
    uint8_t input_header[72];
    uint8_t output_header[20];
    uint8_t *video = NULL;
    size_t video_capacity = 0;
    memcpy(output_header, "TGO1", 4);
    store_le32(output_header + 4, PANEL_WIDTH);
    store_le32(output_header + 8, PANEL_HEIGHT);
    store_le32(output_header + 12, PANEL_WIDTH * PIXEL_BYTES);
    store_le32(output_header + 16, PANEL_BYTES);
    if (write_all(STDOUT_FILENO, output_header, sizeof(output_header)) != 0)
        return -1;
    for (;;) {
        int status = read_all(STDIN_FILENO, input_header, 8);
        if (status == 1) {
            free(video);
            return 0;
        }
        if (status != 0)
            return -1;
        if (memcmp(input_header, "TGI1", 4) == 0) {
            if (le32(input_header + 4) != SOURCE_BYTES ||
                read_all(STDIN_FILENO, source, SOURCE_BYTES) != 0 ||
                render_frame(renderer, source, panel) != 0)
                return -1;
        } else if (memcmp(input_header, "TGS2", 4) == 0) {
            uint32_t bytes = le32(input_header + 4);
            if ((bytes != 0 && bytes != SOURCE_BYTES) ||
                read_all(STDIN_FILENO, input_header + 8, 32) != 0 ||
                le32(input_header + 36) != 0 ||
                (bytes && read_all(STDIN_FILENO, source, bytes) != 0) ||
                render_scene(renderer, bytes ? source : NULL, panel, input_header, NULL, 0) != 0)
                return -1;
        } else if (memcmp(input_header, "TGS3", 4) == 0) {
            if (stream_text_scene(renderer, source, panel, input_header) != 0)
                return -1;
        } else if (memcmp(input_header, "TGV4", 4) == 0) {
            if (stream_video_scene(renderer, &video, &video_capacity,
                                   panel, input_header) != 0)
                return -1;
        } else {
            return -1;
        }
        memcpy(output_header, "TGF1", 4);
        store_le32(output_header + 4, PANEL_BYTES);
        if (write_all(STDOUT_FILENO, output_header, 8) != 0 ||
            write_all(STDOUT_FILENO, panel, PANEL_BYTES) != 0)
            return -1;
    }
}

int main(int argc, char **argv) {
    struct stat node;
    struct renderer renderer;
    uint8_t *source = NULL;
    uint8_t *panel = NULL;
    int stream = 0, run_benchmark = 0;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--capabilities") == 0) {
#if defined(TATER_GPU_ROOK)
        puts("{\"adapter\":\"tater-gpu-rotate\",\"version\":1,"
             "\"source_width\":480,\"source_height\":480,"
             "\"panel_width\":480,\"panel_height\":480,"
             "\"output\":\"bgra\",\"scene_version\":4}");
#elif defined(TATER_GPU_CROWN) || defined(TATER_GPU_ATHENA)
        puts("{\"adapter\":\"tater-gpu-rotate\",\"version\":1,"
             "\"source_width\":1280,\"source_height\":800,"
             "\"panel_width\":800,\"panel_height\":1280,"
             "\"output\":\"bgra\",\"scene_version\":4}");
#else
        puts("{\"adapter\":\"tater-gpu-rotate\",\"version\":1,"
             "\"source_width\":960,\"source_height\":480,"
             "\"panel_width\":480,\"panel_height\":960,"
             "\"output\":\"bgra\",\"scene_version\":4}");
#endif
        return 0;
    }
    if (argc != 7 || strcmp(argv[2], "--target") != 0 ||
        strcmp(argv[3], GPU_TARGET) != 0 ||
        strcmp(argv[4], "--acknowledge-gpu-job") != 0 ||
        strcmp(argv[5], "pbuffer-rotate-readback") != 0 ||
        strcmp(argv[6], "run") != 0 ||
        ((strcmp(argv[1], "--self-test-rotate") != 0) &&
         (strcmp(argv[1], "--stream-rgba-rotate-v1") != 0) &&
         (strcmp(argv[1], "--benchmark-rotate") != 0))) {
#if defined(TATER_GPU_ATHENA)
        fprintf(stderr, "refusing: guarded Athena GPU invocation required\n");
#elif defined(TATER_GPU_CROWN)
        fprintf(stderr, "refusing: guarded Crown GPU invocation required\n");
#elif defined(TATER_GPU_CRONOS)
        fprintf(stderr, "refusing: guarded Cronos GPU invocation required\n");
#else
        fprintf(stderr, "refusing: guarded Checkers GPU invocation required\n");
#endif
        return 2;
    }
#if defined(TATER_GPU_ROOK)
    if (!rook_gpu_target_allowed()) {
        fprintf(stderr, "refusing: Rook kernel/hardware/LCM/panel preflight failed\n");
        return 2;
    }
#elif defined(TATER_GPU_ATHENA)
    if (!athena_gpu_target_allowed()) {
        fprintf(stderr, "refusing: Athena kernel/DT/panel/Panfrost preflight failed\n");
        return 2;
    }
#elif defined(TATER_GPU_CROWN)
    if (!crown_gpu_target_allowed()) {
        fprintf(stderr, "refusing: Crown kernel/product/panel preflight failed\n");
        return 2;
    }
#elif defined(TATER_GPU_CRONOS)
    if (!cronos_gpu_target_allowed()) {
        fprintf(stderr, "refusing: Cronos kernel/product/panel preflight failed\n");
        return 2;
    }
#endif
    stream = strcmp(argv[1], "--stream-rgba-rotate-v1") == 0;
    run_benchmark = strcmp(argv[1], "--benchmark-rotate") == 0;
    if (stat(MALI_NODE, &node) != 0 || !S_ISCHR(node.st_mode)) {
        fprintf(stderr, "target check failed: %s is unavailable\n", MALI_NODE);
        return 2;
    }
    source = malloc(SOURCE_BYTES);
    panel = malloc(PANEL_BYTES);
    if (source == NULL || panel == NULL)
        goto cleanup;
    alarm(30);
    if (initialize_renderer(&renderer) != 0)
        goto cleanup;
    if (stream)
        alarm(0);
    result = stream ? stream_frames(&renderer, source, panel)
                    : run_benchmark ? benchmark(&renderer, source, panel)
                                    : self_test(&renderer, source, panel);
    alarm(0);
    destroy_renderer(&renderer);
cleanup:
    free(panel);
    free(source);
    return result == 0 ? 0 : 1;
}

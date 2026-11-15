#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define DISPLAY_NODE "/dev/mtk_disp_mgr"
#define FRAMEBUFFER_NODE "/dev/fb0"
#define DISP_SESSION_PRIMARY 1U
#define MAKE_DISP_SESSION(type, device) (((type) << 16) | (device))
#define RSZ_RES_LIST_NUM 4

enum disp_if_type {
    DISP_IF_TYPE_DBI = 0,
    DISP_IF_TYPE_DPI,
    DISP_IF_TYPE_DSI0,
    DISP_IF_TYPE_DSI1,
    DISP_IF_TYPE_DSIDUAL,
};

enum disp_if_mode {
    DISP_IF_MODE_VIDEO = 0,
    DISP_IF_MODE_COMMAND,
};

enum disp_cap_output_mode {
    DISP_OUTPUT_CAP_DIRECT_LINK = 0,
    DISP_OUTPUT_CAP_DECOUPLE,
    DISP_OUTPUT_CAP_SWITCHABLE,
};

enum disp_cap_output_pass {
    DISP_OUTPUT_CAP_SINGLE_PASS = 0,
    DISP_OUTPUT_CAP_MULTI_PASS,
};

struct disp_session_info {
    uint32_t session_id;
    uint32_t max_layer_num;
    uint32_t hardware_vsync;
    enum disp_if_type display_type;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    enum disp_if_mode mode;
    uint32_t vsync_fps;
    uint32_t physical_width;
    uint32_t physical_height;
    uint32_t physical_width_um;
    uint32_t physical_height_um;
    uint32_t density;
    uint32_t connected;
    uint32_t hdcp_supported;
    uint32_t overlay_disabled;
    uint32_t support_3d;
    uint32_t constant_layer_num;
};

struct disp_caps_info {
    enum disp_cap_output_mode output_mode;
    enum disp_cap_output_pass output_pass;
    uint32_t max_layer_num;
    uint32_t features;
    int32_t frame_config_ioctl;
    int32_t output_rotated;
    int32_t lcm_degree;
    uint32_t resize_input[RSZ_RES_LIST_NUM][2];
    uint32_t resize_count;
    uint32_t resize_max[2];
};

#define DISP_IOW(number, type) _IOW('O', (number), type)
#define DISP_IOCTL_GET_SESSION_INFO DISP_IOW(208, struct disp_session_info)
#define DISP_IOCTL_GET_IS_DRIVER_SUSPEND DISP_IOW(218, uint32_t)
#define DISP_IOCTL_GET_DISPLAY_CAPS DISP_IOW(219, struct disp_caps_info)
#define DISP_IOCTL_GET_LCM_INDEX _IOR('x', 56, int)

_Static_assert(sizeof(struct disp_session_info) == 76,
               "unexpected display session ABI size");
_Static_assert(sizeof(struct disp_caps_info) == 72,
               "unexpected display capability ABI size");

static int check_character_node(const char *path) {
    struct stat status;

    return stat(path, &status) == 0 && S_ISCHR(status.st_mode);
}

static const char *mode_name(uint32_t mode) {
    if (mode == DISP_IF_MODE_VIDEO)
        return "video";
    if (mode == DISP_IF_MODE_COMMAND)
        return "command";
    return "unknown";
}

static const char *type_name(uint32_t type) {
    switch (type) {
    case DISP_IF_TYPE_DBI:
        return "dbi";
    case DISP_IF_TYPE_DPI:
        return "dpi";
    case DISP_IF_TYPE_DSI0:
        return "dsi0";
    case DISP_IF_TYPE_DSI1:
        return "dsi1";
    case DISP_IF_TYPE_DSIDUAL:
        return "dsi-dual";
    default:
        return "unknown";
    }
}

int main(int argc, char **argv) {
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    struct disp_session_info session;
    struct disp_caps_info caps;
    uint32_t suspended = 0;
    int suspend_query_supported = 1;
    int framebuffer = -1;
    int display = -1;
    int lcm_index;
    int result = 1;

    if (argc != 4 || strcmp(argv[1], "--read-only-query") != 0 ||
        strcmp(argv[2], "--target") != 0 ||
        strcmp(argv[3], "checkers") != 0) {
        fprintf(stderr,
                "refusing: use --read-only-query --target checkers\n");
        return 2;
    }
    if (!check_character_node(FRAMEBUFFER_NODE) ||
        !check_character_node(DISPLAY_NODE)) {
        fprintf(stderr, "target check failed: display nodes are unavailable\n");
        return 2;
    }

    framebuffer = open(FRAMEBUFFER_NODE, O_RDONLY | O_CLOEXEC);
    display = open(DISPLAY_NODE, O_RDONLY | O_CLOEXEC);
    if (framebuffer < 0 || display < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        goto cleanup;
    }
    memset(&fixed, 0, sizeof(fixed));
    memset(&variable, 0, sizeof(variable));
    memset(&session, 0, sizeof(session));
    memset(&caps, 0, sizeof(caps));
    session.session_id = MAKE_DISP_SESSION(DISP_SESSION_PRIMARY, 0U);

    if (ioctl(framebuffer, FBIOGET_FSCREENINFO, &fixed) != 0) {
        fprintf(stderr, "FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (ioctl(framebuffer, FBIOGET_VSCREENINFO, &variable) != 0) {
        fprintf(stderr, "FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (ioctl(display, DISP_IOCTL_GET_SESSION_INFO, &session) != 0) {
        fprintf(stderr, "GET_SESSION_INFO failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (ioctl(display, DISP_IOCTL_GET_DISPLAY_CAPS, &caps) != 0) {
        fprintf(stderr, "GET_DISPLAY_CAPS failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (ioctl(display, DISP_IOCTL_GET_IS_DRIVER_SUSPEND, &suspended) != 0) {
        if (errno == EFAULT) {
            suspend_query_supported = 0;
        } else {
            fprintf(stderr, "GET_IS_DRIVER_SUSPEND failed: %s\n",
                    strerror(errno));
            goto cleanup;
        }
    }
    lcm_index = ioctl(display, DISP_IOCTL_GET_LCM_INDEX, 0UL);
    if (lcm_index < 0) {
        fprintf(stderr, "LCM index query failed: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("{\"framebuffer\":\"%.16s\",\"visible\":\"%ux%u\","
           "\"virtual\":\"%ux%u\",\"bits_per_pixel\":%u,"
           "\"line_length\":%u,\"buffer_bytes\":%u,"
           "\"session\":\"primary\",\"display_type\":\"%s\","
           "\"display_mode\":\"%s\",\"display_size\":\"%ux%u\","
           "\"display_format\":%u,\"hardware_vsync\":%u,"
           "\"vsync_fps\":%u,\"connected\":%u,"
           "\"session_max_layers\":%u,\"cap_max_layers\":%u,"
           "\"cap_output_mode\":%u,\"cap_output_pass\":%u,"
           "\"cap_features\":%u,\"frame_config_ioctl\":%d,"
           "\"output_rotated\":%d,\"lcm_degree\":%d,"
           "\"lcm_index\":%d,\"suspend_query_supported\":%s,"
           "\"suspended\":%s,"
           "\"display_touched\":false}\n",
           fixed.id, variable.xres, variable.yres, variable.xres_virtual,
           variable.yres_virtual, variable.bits_per_pixel, fixed.line_length,
           fixed.smem_len, type_name(session.display_type),
           mode_name(session.mode), session.width, session.height,
           session.format, session.hardware_vsync, session.vsync_fps,
           session.connected, session.max_layer_num, caps.max_layer_num,
           (uint32_t)caps.output_mode, (uint32_t)caps.output_pass,
           caps.features, caps.frame_config_ioctl, caps.output_rotated,
           caps.lcm_degree, lcm_index,
           suspend_query_supported ? "true" : "false",
           suspend_query_supported ? (suspended ? "true" : "false") :
                                     "null");
    result = 0;

cleanup:
    if (display >= 0)
        close(display);
    if (framebuffer >= 0)
        close(framebuffer);
    return result;
}

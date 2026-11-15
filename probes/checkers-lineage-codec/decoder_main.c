/* SPDX-License-Identifier: MIT
 * Checkers-only entry point for the shared retained-Lineage linear decoder.
 * Separate from the published FireOS/GraphicBuffer helper and its source lock.
 */
#include "checkers_target.h"
#include "../mt8163-vcodec/tater_vdec_android.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static int number(const char *text, uint32_t *value) {
    char *end;
    unsigned long parsed;
    if (!text || !*text || *text < '0' || *text > '9') return -1;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed > UINT32_MAX) return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int finish(int result) {
    /* The shared OMX client explicitly tears down buffers/component first.
     * Keep vendor DSOs resident: their Android unload hooks are not a Linux ABI.
     */
    fflush(NULL);
    _exit(result);
}

int main(int argc, char **argv) {
    if (!checkers_lineage_target_allowed()) {
        fputs("refusing: Checkers Lineage kernel/product/panel preflight failed\n", stderr);
        return 2;
    }
    if (argc == 2 && !strcmp(argv[1], "--capabilities")) {
        printf("{\"adapter\":\"tater-vdec\",\"version\":2,\"codec\":\"h264\","
               "\"input\":\"annex-b-aud-batch\",\"outputs\":[\"yuv420p\",\"rgba8888\"],"
               "\"batch_version\":1,\"stream_versions\":[1,2],"
               "\"stream_input\":\"le32-length-prefixed-access-units\","
               "\"stream_v2_input\":\"le32-size-i64-pts-us-access-unit\","
               "\"stream_v2_output\":\"TVO2-header-and-timestamped-rgba\","
               "\"presentation_order\":true,\"max_width\":%u,\"max_height\":%u,"
               "\"max_batch_frames\":%u}\n", TATER_VDEC_MAX_WIDTH,
               TATER_VDEC_MAX_HEIGHT, TATER_VDEC_MAX_BATCH_FRAMES);
        return 0;
    }
    if (argc == 6 && (!strcmp(argv[1], "--stream-h264-rgba") ||
                      !strcmp(argv[1], "--stream-h264-rgba-v2"))) {
        uint32_t width, height, fps;
        int output, result;
        if (number(argv[3], &width) || number(argv[4], &height) || number(argv[5], &fps))
            return 2;
        fflush(stdout);
        output = dup(STDOUT_FILENO);
        if (output < 0) return 1;
        if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
            close(output);
            return 1;
        }
        if (!strcmp(argv[1], "--stream-h264-rgba-v2"))
            result = tater_vdec_stream_rgba8888_v2(argv[2], width, height,
                                                  fps, STDIN_FILENO, output);
        else
            result = tater_vdec_stream_rgba8888(argv[2], width, height,
                                               fps, STDIN_FILENO, output);
        close(output);
        return finish(result);
    }
    if (argc == 8 && (!strcmp(argv[1], "--decode-h264-batch") ||
                      !strcmp(argv[1], "--decode-h264-batch-rgba"))) {
        struct tater_vdec_batch request = {0};
        request.component_library = argv[2];
        request.input_annex_b = argv[3];
        request.output_path = argv[7];
        request.output_rgba8888 = !strcmp(argv[1], "--decode-h264-batch-rgba");
        if (number(argv[4], &request.width) || number(argv[5], &request.height) ||
            number(argv[6], &request.frame_count)) return 2;
        return finish(tater_vdec_decode_batch(&request));
    }
    fputs("refusing: expected a bounded batch or TVO2 stream invocation\n", stderr);
    return 2;
}

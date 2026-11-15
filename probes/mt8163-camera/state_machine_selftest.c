// SPDX-License-Identifier: MIT
#include "test_pattern_state.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct mock_context {
    enum tater_camera_step events[32];
    unsigned int count;
    int fail_step;
};

static int mock_privacy_gate(void *opaque, bool operator_present,
                             bool test_pattern_only)
{
    struct mock_context *context = opaque;

    context->events[context->count++] = TATER_CAMERA_PRIVACY_GATE;
    return operator_present && test_pattern_only ? 0 : -EPERM;
}

static int mock_run_step(void *opaque, enum tater_camera_step step)
{
    struct mock_context *context = opaque;

    context->events[context->count++] = step;
    return context->fail_step == (int)step ? -EIO : 0;
}

static int expect(const struct mock_context *context,
                  const enum tater_camera_step *events,
                  unsigned int count)
{
    return context->count == count &&
           memcmp(context->events, events, count * sizeof(events[0])) == 0;
}

int main(void)
{
    static const struct tater_camera_backend backend = {
        .privacy_gate = mock_privacy_gate,
        .run_step = mock_run_step,
    };
    static const enum tater_camera_step success[] = {
        TATER_CAMERA_PRIVACY_GATE,
        TATER_CAMERA_SENSOR_OPEN,
        TATER_CAMERA_TEST_PATTERN_ENABLE,
        TATER_CAMERA_BUFFERS_MAP,
        TATER_CAMERA_PIPE_ACQUIRE,
        TATER_CAMERA_PIPE_CONFIGURE,
        TATER_CAMERA_STREAM_START,
        TATER_CAMERA_DEQUEUE_ONE,
        TATER_CAMERA_STREAM_STOP,
        TATER_CAMERA_PIPE_RELEASE,
        TATER_CAMERA_BUFFERS_UNMAP,
        TATER_CAMERA_SENSOR_CLOSE,
    };
    static const enum tater_camera_step start_failure[] = {
        TATER_CAMERA_PRIVACY_GATE,
        TATER_CAMERA_SENSOR_OPEN,
        TATER_CAMERA_TEST_PATTERN_ENABLE,
        TATER_CAMERA_BUFFERS_MAP,
        TATER_CAMERA_PIPE_ACQUIRE,
        TATER_CAMERA_PIPE_CONFIGURE,
        TATER_CAMERA_STREAM_START,
        TATER_CAMERA_PIPE_RELEASE,
        TATER_CAMERA_BUFFERS_UNMAP,
        TATER_CAMERA_SENSOR_CLOSE,
    };
    struct mock_context context = {.fail_step = -1};
    int result;

    result = tater_camera_run_one_test_pattern(&backend, &context, true, true);
    if (result != 0 || !expect(&context, success,
                               sizeof(success) / sizeof(success[0])))
        return 1;

    memset(&context, 0, sizeof(context));
    context.fail_step = TATER_CAMERA_STREAM_START;
    result = tater_camera_run_one_test_pattern(&backend, &context, true, true);
    if (result != -EIO || !expect(&context, start_failure,
                                  sizeof(start_failure) /
                                      sizeof(start_failure[0])))
        return 2;

    memset(&context, 0, sizeof(context));
    context.fail_step = -1;
    result = tater_camera_run_one_test_pattern(&backend, &context, false, true);
    if (result != -EPERM || context.count != 1)
        return 3;

    puts("camera state-machine self-test: ok");
    return 0;
}

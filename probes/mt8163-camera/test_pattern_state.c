// SPDX-License-Identifier: MIT
#include "test_pattern_state.h"

#include <errno.h>

static int first_error(int current, int candidate)
{
    return current != 0 ? current : candidate;
}

int tater_camera_run_one_test_pattern(
    const struct tater_camera_backend *backend,
    void *context,
    bool operator_present,
    bool test_pattern_only)
{
    bool sensor_open = false;
    bool buffers_mapped = false;
    bool pipe_acquired = false;
    bool stream_started = false;
    int result;

    if (backend == 0 || backend->privacy_gate == 0 || backend->run_step == 0)
        return -EINVAL;

    result = backend->privacy_gate(context, operator_present,
                                   test_pattern_only);
    if (result != 0)
        return result;

    result = backend->run_step(context, TATER_CAMERA_SENSOR_OPEN);
    if (result != 0)
        return result;
    sensor_open = true;

    result = backend->run_step(context, TATER_CAMERA_TEST_PATTERN_ENABLE);
    if (result != 0)
        goto teardown;

    result = backend->run_step(context, TATER_CAMERA_BUFFERS_MAP);
    if (result != 0)
        goto teardown;
    buffers_mapped = true;

    result = backend->run_step(context, TATER_CAMERA_PIPE_ACQUIRE);
    if (result != 0)
        goto teardown;
    pipe_acquired = true;

    result = backend->run_step(context, TATER_CAMERA_PIPE_CONFIGURE);
    if (result != 0)
        goto teardown;

    result = backend->run_step(context, TATER_CAMERA_STREAM_START);
    if (result != 0)
        goto teardown;
    stream_started = true;

    result = backend->run_step(context, TATER_CAMERA_DEQUEUE_ONE);

teardown:
    if (stream_started)
        result = first_error(
            result,
            backend->run_step(context, TATER_CAMERA_STREAM_STOP));
    if (pipe_acquired)
        result = first_error(
            result,
            backend->run_step(context, TATER_CAMERA_PIPE_RELEASE));
    if (buffers_mapped)
        result = first_error(
            result,
            backend->run_step(context, TATER_CAMERA_BUFFERS_UNMAP));
    if (sensor_open)
        result = first_error(
            result,
            backend->run_step(context, TATER_CAMERA_SENSOR_CLOSE));
    return result;
}

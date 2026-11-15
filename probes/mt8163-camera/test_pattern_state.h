// SPDX-License-Identifier: MIT
#ifndef TATER_CHECKERS_CAMERA_TEST_PATTERN_STATE_H
#define TATER_CHECKERS_CAMERA_TEST_PATTERN_STATE_H

#include <stdbool.h>

enum tater_camera_step {
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

struct tater_camera_backend {
    int (*privacy_gate)(void *context, bool operator_present,
                        bool test_pattern_only);
    int (*run_step)(void *context, enum tater_camera_step step);
};

/*
 * Run exactly one sensor-generated test-pattern frame. The backend owns all
 * hardware details. This function owns ordering and reverse teardown.
 */
int tater_camera_run_one_test_pattern(
    const struct tater_camera_backend *backend,
    void *context,
    bool operator_present,
    bool test_pattern_only);

#endif

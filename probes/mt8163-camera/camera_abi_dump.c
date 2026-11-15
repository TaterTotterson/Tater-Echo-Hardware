// SPDX-License-Identifier: GPL-2.0-only
/*
 * Emit the exact ARM32 ioctl numbers and structure sizes from the public
 * MT8163 camera headers. This program does not open a device or issue an
 * ioctl. It is an ABI build check, not a camera probe.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;

#include "kd_imgsensor_define.h"
#include "kd_imgsensor.h"
#include "camera_isp.h"
#include "camera_pipe_mgr.h"
#include "camera_sysram.h"

#define IOCTL_ENTRY(name)                                                     \
    printf("\"%s\":{\"request\":%lu,\"size\":%lu}", #name,             \
           (unsigned long)(name), (unsigned long)_IOC_SIZE(name))

int main(void)
{
    printf("{");
    printf("\"schema_version\":1,");
    printf("\"architecture\":\"arm32\",");
    printf("\"pointer_size\":%lu,", (unsigned long)sizeof(void *));
#ifdef CONFIG_CHECKERS
    printf("\"config_checkers\":true,");
#else
    printf("\"config_checkers\":false,");
#endif
    printf("\"sensor_feature_ids\":{");
    printf("\"set_register\":%u,", (unsigned int)SENSOR_FEATURE_SET_REGISTER);
    printf("\"get_register\":%u,", (unsigned int)SENSOR_FEATURE_GET_REGISTER);
    printf("\"set_test_pattern\":%u},",
           (unsigned int)SENSOR_FEATURE_SET_TEST_PATTERN);
    printf("\"device_nodes\":[\"/dev/kd_camera_hw\","
           "\"/dev/kd_camera_hw_bus2\",\"/dev/camera-isp\","
           "\"/dev/camera-pipemgr\",\"/dev/camera-sysram\"],");
    printf("\"ioctls\":{");
    IOCTL_ENTRY(ISP_RESET);
    printf(",");
    IOCTL_ENTRY(ISP_READ_REGISTER);
    printf(",");
    IOCTL_ENTRY(ISP_WRITE_REGISTER);
    printf(",");
    IOCTL_ENTRY(ISP_WAIT_IRQ);
    printf(",");
    IOCTL_ENTRY(ISP_BUFFER_CTRL);
    printf(",");
    IOCTL_ENTRY(ISP_REF_CNT_CTRL);
    printf(",");
    IOCTL_ENTRY(CAM_PIPE_MGR_LOCK);
    printf(",");
    IOCTL_ENTRY(CAM_PIPE_MGR_UNLOCK);
    printf(",");
    IOCTL_ENTRY(CAM_PIPE_MGR_SET_MODE);
    printf(",");
    IOCTL_ENTRY(CAM_PIPE_MGR_ENABLE_PIPE);
    printf(",");
    IOCTL_ENTRY(CAM_PIPE_MGR_DISABLE_PIPE);
    printf(",");
    IOCTL_ENTRY(SYSRAM_ALLOC);
    printf(",");
    IOCTL_ENTRY(SYSRAM_FREE);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_X_SET_DRIVER);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_T_OPEN);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_X_GETINFO2);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_X_GETRESOLUTION2);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_X_FEATURECONCTROL);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_X_CONTROL);
    printf(",");
    IOCTL_ENTRY(KDIMGSENSORIOC_T_CLOSE);
    printf("},");
    printf("\"structures\":{");
    printf("\"ISP_REG_IO_STRUCT\":%lu,",
           (unsigned long)sizeof(struct ISP_REG_IO_STRUCT));
    printf("\"ISP_WAIT_IRQ_STRUCT\":%lu,",
           (unsigned long)sizeof(struct ISP_WAIT_IRQ_STRUCT));
    printf("\"ISP_BUFFER_CTRL_STRUCT\":%lu,",
           (unsigned long)sizeof(struct ISP_BUFFER_CTRL_STRUCT));
    printf("\"ISP_REF_CNT_CTRL_STRUCT\":%lu,",
           (unsigned long)sizeof(struct ISP_REF_CNT_CTRL_STRUCT));
    printf("\"CAM_PIPE_MGR_LOCK_STRUCT\":%lu,",
           (unsigned long)sizeof(struct CAM_PIPE_MGR_LOCK_STRUCT));
    printf("\"CAM_PIPE_MGR_MODE_STRUCT\":%lu,",
           (unsigned long)sizeof(struct CAM_PIPE_MGR_MODE_STRUCT));
    printf("\"SYSRAM_ALLOC_STRUCT\":%lu",
           (unsigned long)sizeof(struct SYSRAM_ALLOC_STRUCT));
    printf(",\"SENSOR_DRIVER_INDEX_STRUCT\":%lu",
           (unsigned long)sizeof(struct SENSOR_DRIVER_INDEX_STRUCT));
    printf(",\"IMAGESENSOR_GETINFO_STRUCT\":%lu",
           (unsigned long)sizeof(struct IMAGESENSOR_GETINFO_STRUCT));
    printf(",\"ACDK_SENSOR_PRESOLUTION_STRUCT\":%lu",
           (unsigned long)sizeof(struct ACDK_SENSOR_PRESOLUTION_STRUCT));
    printf(",\"ACDK_SENSOR_FEATURECONTROL_STRUCT\":%lu",
           (unsigned long)sizeof(struct ACDK_SENSOR_FEATURECONTROL_STRUCT));
    printf(",\"ACDK_SENSOR_CONTROL_STRUCT\":%lu",
           (unsigned long)sizeof(struct ACDK_SENSOR_CONTROL_STRUCT));
    printf("},");
    printf("\"device_opened\":false,");
    printf("\"ioctl_issued\":false,");
    printf("\"camera_powered\":false,");
    printf("\"capture_performed\":false");
    printf("}\n");
    return 0;
}

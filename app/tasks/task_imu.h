#ifndef APP_TASK_IMU
#define APP_TASK_IMU

#include "kernel/kernel.h"

#define IMU_RDY_EVT BIT(0)

extern task_handle_t g_imu_task_h;
extern event_t g_imu_evt;

void imu_task(void *user_data);

#endif

#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/imu/imu_regs.h"
#include "kernel/kernel.h"
#include "kernel/task/task.h"
#include "task_imu.h"

// #define IMU_FOC
#define NOMO_DUR_S 30

task_handle_t g_imu_task_h;
event_t g_imu_evt;

static void imu_callback()
{
    kernel_task_notify_from_isr(g_imu_task_h, 0, NOTIFY_ACTION_NONE);
}

void imu_task(void *user_data)
{
    (void)user_data;
    uint8_t int_status[4];

    bw_status_t status = imu_init(IMU_ODR_100, IMU_STEP_MODE_NORMAL, IMU_NOMO_NORMAL, NOMO_DUR_S, imu_callback);
    if (status != STATUS_OK)
    {
        BW_LOG("failed to initialize, Exit with status: %d", status);
    }

#ifdef IMU_FOC
    imu_start_foc();
#endif

    kernel_event_set(&g_imu_evt, IMU_RDY_EVT);

    while (1)
    {
        kernel_task_notify_wait(0, 0, NULL, MAX_TIMEOUT);

        status = imu_read_int_status(int_status);
        if (status != STATUS_OK)
        {
            continue;
        }

        if (int_status[0] & IMU_INT_ST0_STEP_Msk)
        {
            BW_LOG("Step\n");
        }
        if (int_status[1] & IMU_INT_ST1_NOMO_Msk)
        {
            BW_LOG("No motion\n");
        }
        if (int_status[1] & IMU_INT_ST1_DRDY_Msk)
        {
            BW_LOG("IMU Data Ready\n");
        }

        BW_LOG("%x, %x, %x, %x\n", int_status[0], int_status[1], int_status[2], int_status[3]);
    }
}

#include "task_imu.h"
#include "task_oxim.h"
#include "tasks.h"

void test_task(void *user_data)
{
    (void)user_data;
    kernel_task_delay(5000);

    while (1)
    {
        BW_LOG("----------------------Read SPO2-----------------------\n");
        kernel_task_notify(g_oxim_task_h, OXIM_READ_SPO2_NTF, NOTIFY_ACTION_SET_BITS);
        kernel_task_delay(25000);
    }
}

void tasks_create()
{
    kernel_event_init(&g_imu_evt);
    kernel_task_create(imu_task, "IMU Task", PRIO_IMU, STACK_IMU, NULL, &g_imu_task_h);
    kernel_task_create(oxim_task, "Oxim Task", PRIO_OXIM, STACK_OXIM, NULL, &g_oxim_task_h);
    kernel_task_create(test_task, "Test Task", 0, 128, NULL, NULL);
}

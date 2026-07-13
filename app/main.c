#include "biowatch/bsp.h"
#include "biowatch/pins.h"
#include "drivers/gpio/gpio.h"
#include "drivers/gpio/gpio_types.h"
#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/oxim/oxim.h"
#include "kernel/kernel.h"
#include "kernel/task/task.h"
#include "lib/logger.h"
#include <stdint.h>

static task_handle_t g_imu_task_h;
static uint16_t g_step_count = UINT16_MAX;
static imu_data_t g_step_imu_data;
static imu_sample_t g_step_buf[1024];
static uint32_t g_oxim_red_ppg_buf[512];
static uint32_t g_oxim_ir_ppg_buf[512];
static imu_acc_sample_t g_oxim_acc_buf[512];

static void oximeter_task(void *user_data)
{
    (void)user_data;
    oxim_init(OXIM_SMP_AVG_4, OXIM_SMP_RATE_100);
    if (oxim_get_state() != OXIM_STATE_READY)
    {
        kernel_task_delete(NULL);
    }

    oxim_data_t data = {.mode = OXIM_MODE_SPO2,
                        .red_ppg_buf = g_oxim_red_ppg_buf,
                        .ir_ppg_buf = g_oxim_ir_ppg_buf,
                        .acc_buf = g_oxim_acc_buf,
                        .samples = 512};
    bw_status_t status = oxim_read_data(&data);
    if (status != STATUS_OK)
    {
        BW_LOG("Oximeter exited with status: %d and state: %d\n", status, oxim_get_state());
    }

    while (1)
    {
        kernel_task_delay(5000);
    }
}

static void imu_callback()
{
    kernel_task_notify_from_isr(g_imu_task_h, 0, NOTIFY_ACTION_NONE);
}

static void imu_task(void *user_data)
{
    (void)user_data;
    bw_status_t status;
    uint8_t int_status[4];

    imu_init(IMU_ODR_100, IMU_STEP_MODE_NORMAL, IMU_NOMO_NORMAL, 8, imu_callback);
    kernel_task_create(oximeter_task, "Oximeter Task", 7, 256, NULL, NULL);

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
            status = imu_read_step_cnt(&g_step_count);
            BW_LOG("Steps: %d\n", g_step_count);
        }
        if (int_status[1] & IMU_INT_ST1_NOMO_Msk)
        {
            BW_LOG("No motion\n");
        }
        if ((int_status[1] & IMU_INT_ST1_FFULL_Msk) || (int_status[1] & IMU_INT_ST1_FWM_Msk))
        {
            BW_LOG("Fifo interrupt\n");
            imu_drain_fifo();
        }

        BW_LOG("%x, %x, %x, %x\n", int_status[0], int_status[1], int_status[2], int_status[3]);
    }
}

static void led_task(void *user_data)
{
    (void)user_data;
    gpio_conf_t gpio_conf = gpio_conf_output(PA15, GPIO_SPEED_FAST);
    gpio_init(&gpio_conf);

    gpio_set_level(PA15, 1);
    while (1)
    {
        gpio_set_level(PA15, 0);
        kernel_task_delay(500);
        gpio_set_level(PA15, 1);
        kernel_task_delay(500);
    }
    gpio_set_level(PA15, 0);
}

static void idle_hook(void *user_data)
{
    (void)user_data;
}

int main()
{
    bsp_init();

    kernel_conf_t conf = {.pool_confs = {{.sz = MEM_BLOCK_SZ_2048, .count = 2},
                                         {.sz = MEM_BLOCK_SZ_1024, .count = 4},
                                         {.sz = MEM_BLOCK_SZ_512, .count = 4},
                                         {.sz = MEM_BLOCK_SZ_256, .count = 4},
                                         {.sz = MEM_BLOCK_SZ_128, .count = 4}},
                          .idle_hook = idle_hook,
                          .idle_data = NULL};
    kernel_init(&conf);

    task_handle_t handle0;
    kernel_task_create(led_task, "LED Task", 7, 256, NULL, &handle0);
    kernel_task_create(imu_task, "IMU Task", 7, 256, NULL, &g_imu_task_h);

    kernel_run();
}

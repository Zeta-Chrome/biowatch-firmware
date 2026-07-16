#include "app/tasks/task_imu.h"
#include "arm_math.h"
#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/oxim/oxim.h"
#include "drivers/sensor/oxim/oxim_regs.h"
#include "kernel/task/task.h"
#include "lib/logger.h"
#include "lib/status.h"
#include "task_oxim.h"
#include <stdint.h>

#define INT_NTF BIT(0)
#define NUM_HR_SAMPLES 128
#define NUM_SPO2_SAMPLES 512
#define MAX_ALC_CTR 2

typedef float dtype_t;

typedef enum
{
    OXIM_INT_PWR_RDY,
    OXIM_INT_PPG_RDY,
    OXIM_INT_ALC_OVF,
    OXIM_INT_DIE_TEMP_RDY,
    OXIM_INT_ERR,
} oxim_int_t;

task_handle_t g_oxim_task_h;
dtype_t g_red_ppg[NUM_SPO2_SAMPLES];
dtype_t g_ir_ppg[NUM_SPO2_SAMPLES];
dtype_t g_acc_mag[NUM_SPO2_SAMPLES];

static void oxim_callback(void)
{
    kernel_task_notify_from_isr(g_oxim_task_h, INT_NTF, NOTIFY_ACTION_SET_BITS);
}

static oxim_int_t handle_interrupt(void)
{
    uint8_t int_status[2];

    if (oxim_read_int_status(int_status) != STATUS_OK)
    {
        BW_LOG("oxim_read_int_status failed\n");
        return OXIM_INT_ERR;
    }

    if (int_status[0] & OXIM_INT_ST1_PWR_RDY)
    {
        BW_LOG("Brown out occured\n");
        oxim_reconfigure();
        return OXIM_INT_PWR_RDY;
    }

    if (int_status[0] & OXIM_INT_ST1_ALC_OVF)
    {
        return OXIM_INT_ALC_OVF;
    }

    if (int_status[1] & OXIM_INT_ST2_DIE_TEMP_RDY)
    {
        return OXIM_INT_DIE_TEMP_RDY;
    }

    if (!(int_status[0] & OXIM_INT_ST1_PPG_RDY))
    {
        BW_LOG("Unknown interrupt occurred, (%x, %x)\n", int_status[0], int_status[1]);
        return OXIM_INT_ERR;
    }

    return OXIM_INT_PPG_RDY;
}

static bw_status_t oxim_run_session(oxim_mode_t mode, int samples)
{
    bw_status_t status = oxim_start_mode(mode);
    if (status != STATUS_OK)
    {
        BW_LOG("oxim_start_mode exited with status: %d\n", status);
        return status;
    }

    if (mode == OXIM_MODE_SPO2)
    {
        status = oxim_start_temp_conversion();
        if (status != STATUS_OK)
        {
            BW_LOG("oxim_start_temp_conversion exited with status: %d\n", status);
            return status;
        }
    }

    int count = 0, alc_ctr = 0;
    bool failed = false;

    while (count < samples && !failed)
    {
        uint32_t ntf;
        kernel_task_notify_wait(0, INT_NTF, &ntf, MAX_TIMEOUT);
        if (!(ntf & INT_NTF))
        {
            continue;
        }

        switch (handle_interrupt())
        {
        case OXIM_INT_ERR:
        case OXIM_INT_PWR_RDY:
            failed = true;
            continue;

        case OXIM_INT_ALC_OVF:
            if (++alc_ctr > MAX_ALC_CTR)
            {
                BW_LOG("ALC overflow exceeded retry limit\n");
                failed = true;
            }
            continue;

        case OXIM_INT_DIE_TEMP_RDY:
        {
            int temp_milli_c;
            if (oxim_read_temp(&temp_milli_c) != STATUS_OK)
            {
                BW_LOG("oxim_read_temp failed\n");
                failed = true;
            }
            continue;
        }

        case OXIM_INT_PPG_RDY:
            break;
        }

        uint32_t red_sample, ir_sample;
        status = oxim_read_sample(&red_sample, &ir_sample);
        if (status != STATUS_OK)
        {
            BW_LOG("oxim_read_sample with status: %d\n", status);
            failed = true;
            continue;
        }

        imu_acc_sample_t acc_sample;
        status = imu_read_sample(&acc_sample, NULL);
        if (status != STATUS_OK)
        {
            BW_LOG("imu_read_sample with status: %d\n", status);
            failed = true;
            continue;
        }

        // Save the results
        int64_t acc_mag_sqr = (int64_t)acc_sample.ax * acc_sample.ax + (int64_t)acc_sample.ay * acc_sample.ay
                              + (int64_t)acc_sample.az * acc_sample.az;
        double acc_mag_sqr_norm = (double)acc_mag_sqr / ((double)INT16_MAX * INT16_MAX);
        arm_sqrt_f32((dtype_t)acc_mag_sqr_norm, &g_acc_mag[count]);

        g_red_ppg[count] = (dtype_t)red_sample / 0x3FFFF;

        if (mode == OXIM_MODE_SPO2)
        {
            g_ir_ppg[count] = (dtype_t)ir_sample / 0x3FFFF;
        }

        BW_LOG("Count: %d, RED:%f, IR:%f, ACC:%f\n", count, g_red_ppg[count], g_ir_ppg[count], g_acc_mag[count]);

        count++;
    }

    oxim_shutdown();
    return failed ? STATUS_ERR : STATUS_OK;
}

void oxim_task(void *user_data)
{
    (void)user_data;

    bw_status_t status = oxim_init(OXIM_SMP_AVG_4, OXIM_SMP_RATE_100, oxim_callback);
    if (status != STATUS_OK)
    {
        BW_LOG("oxim_init exited with status: %d\n", status);
        kernel_task_delete(NULL);
    }

    // Wait for IMU init
    kernel_event_wait(&g_imu_evt, IMU_RDY_EVT, NULL, false, true, MAX_TIMEOUT);

    while (1)
    {
        uint32_t ntf;
        kernel_task_notify_wait(INT_NTF, OXIM_READ_HR_NTF | OXIM_READ_SPO2_NTF, &ntf, MAX_TIMEOUT);
        if (!(ntf & (OXIM_READ_HR_NTF | OXIM_READ_SPO2_NTF)))
        {
            continue;
        }

        bool is_hr = ntf & OXIM_READ_HR_NTF;
        status = oxim_run_session(is_hr ? OXIM_MODE_HR : OXIM_MODE_SPO2, is_hr ? NUM_HR_SAMPLES : NUM_SPO2_SAMPLES);
        if (status != STATUS_OK)
        {
            BW_LOG("Session failed with status: %d\n", status);
            // TODO: Handle fault
        }
    }
}

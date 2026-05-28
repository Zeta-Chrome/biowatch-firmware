#ifndef DRIVER_IMU_H
#define DRIVER_IMU_H

#include "hal/exti/exti.h"
#include "hal/spi/spi.h"
#include "rtos/task/task.h"

typedef enum
{
    IMU_STEP_MODE_NORMAL,
    IMU_STEP_MODE_SENSITIVE,
    IMU_STEP_MODE_ROBUST
} imu_step_mode_t;

typedef enum
{
    IMU_TAP_VERY_LIGHT = 0x0,
    IMU_TAP_LIGHT = 0x1,
    IMU_TAP_NORMAL = 0x4,
    IMU_TAP_FIRM = 0x6,
    IMU_TAP_HARD = 0x8,
    IMU_TAP_VERY_HARD = 0xA,
} imu_tap_t;

typedef enum
{
    IMU_DTAP_DUR_50MS,
    IMU_DTAP_DUR_100MS,
    IMU_DTAP_DUR_150MS,
    IMU_DTAP_DUR_200MS,
    IMU_DTAP_DUR_250MS,
    IMU_DTAP_DUR_375MS,
    IMU_DTAP_DUR_500MS,
    IMU_DTAP_DUR_700MS,
} imu_dtap_dur_t;

typedef enum
{
    IMU_NOMO_VERY_LIGHT = 0x0,
    IMU_NOMO_LIGHT = 0x1,
    IMU_NOMO_NORMAL = 0x4,
    IMU_NOMO_FIRM = 0x6,
    IMU_NOMO_HARD = 0x8,
    IMU_NOMO_VERY_HARD = 0xA,
} imu_nomo_t;

void imu_init(task_handle_t task, imu_step_mode_t st_mode, imu_tap_t tap_mode, imu_dtap_dur_t dtap_dur,
              imu_nomo_t nomo_mode, uint16_t nomo_dur_s);
void imu_reconfigure(imu_step_mode_t st_mode, imu_tap_t tap_mode, imu_dtap_dur_t dtap_dur,
                     imu_nomo_t nomo_mode, uint16_t nomo_dur_s);
spi_handle_t* imu_get_spi_handle();
exti_handle_t* imu_get_exti_handle(exti_callback_t *callback);
void imu_read_error();
bw_status_t imu_read_int_status(uint8_t int_status[2]);
bw_status_t imu_read_step_cnt(uint16_t *step_cnt);
void imu_sleep();

#endif

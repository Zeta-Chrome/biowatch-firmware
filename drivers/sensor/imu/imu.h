#ifndef DRIVER_IMU_H
#define DRIVER_IMU_H

#include "drivers/exti/exti.h"
#include "drivers/spi/spi.h"
#include "imu_regs.h"

typedef enum
{
    IMU_ODR_25 = 0x6,
    IMU_ODR_50 = 0x7,
    IMU_ODR_100 = 0x8,
    IMU_ODR_200 = 0x9,
    IMU_ODR_400 = 0x10,
    IMU_ODR_800 = 0x11,
    IMU_ODR_1600 = 0x12,
} imu_odr_t;

typedef enum
{
    IMU_STEP_MODE_NORMAL,
    IMU_STEP_MODE_SENSITIVE,
    IMU_STEP_MODE_ROBUST
} imu_step_mode_t;

typedef enum
{
    IMU_NOMO_VERY_LIGHT = 0x0,
    IMU_NOMO_LIGHT = 0x1,
    IMU_NOMO_NORMAL = 0x4,
    IMU_NOMO_FIRM = 0x6,
    IMU_NOMO_HARD = 0x8,
    IMU_NOMO_VERY_HARD = 0xA,
} imu_nomo_t;

typedef struct
{
    int16_t ax;
    int16_t ay;
    int16_t az;
} imu_acc_sample_t;

typedef struct
{
    int16_t gx;
    int16_t gy;
    int16_t gz;
} imu_gyr_sample_t;

typedef struct
{
    imu_gyr_sample_t gyr;
    imu_acc_sample_t acc;
} imu_sample_t;

typedef void (*imu_callback_t)();

bw_status_t imu_init(imu_odr_t odr, imu_step_mode_t st_mode, imu_nomo_t nomo_mode, uint16_t nomo_dur_s,
                     imu_callback_t callback);
bw_status_t imu_read_error();
bw_status_t imu_read_int_status(uint8_t int_status[4]);
bw_status_t imu_read_step_cnt(uint16_t *step_cnt);
bw_status_t imu_enable_nomo_int();
bw_status_t imu_disable_nomo_int();
bw_status_t imu_start_stream(bool gyro_en);
bw_status_t imu_read_sample(imu_acc_sample_t *acc_sample, imu_gyr_sample_t *gyr_sample);
bw_status_t imu_stop_stream();
bw_status_t imu_wakeup();
bw_status_t imu_sleep();
bw_status_t imu_shutdown();
spi_handle_t *imu_get_spi_handle();
exti_handle_t *imu_get_exti_handle(exti_callback_t *callback);

#endif

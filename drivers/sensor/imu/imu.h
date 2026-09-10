#ifndef DRIVER_IMU_H
#define DRIVER_IMU_H

#include "drivers/exti/exti.h"
#include "drivers/spi/spi.h"
#include "imu_regs.h"

enum imu_odr {
	IMU_ODR_25 = 0x6,
	IMU_ODR_50 = 0x7,
	IMU_ODR_100 = 0x8,
	IMU_ODR_200 = 0x9,
	IMU_ODR_400 = 0x10,
	IMU_ODR_800 = 0x11,
	IMU_ODR_1600 = 0x12,
};

enum imu_step_mode { IMU_STEP_MODE_NORMAL, IMU_STEP_MODE_SENSITIVE, IMU_STEP_MODE_ROBUST };

enum imu_nomo {
	IMU_NOMO_VERY_LIGHT = 0x0,
	IMU_NOMO_LIGHT = 0x1,
	IMU_NOMO_NORMAL = 0x4,
	IMU_NOMO_FIRM = 0x6,
	IMU_NOMO_HARD = 0x8,
	IMU_NOMO_VERY_HARD = 0xA,
};

// In g's
struct acc_sample {
	float ax;
	float ay;
	float az;
};

// In deg/s
struct gyr_sample {
	float gx;
	float gy;
	float gz;
};

typedef void (*imu_callback_t)();

enum bw_status imu_init(enum imu_odr odr, enum imu_step_mode st_mode, enum imu_nomo nomo_mode,
						uint16_t nomo_dur_s, imu_callback_t callback);
enum bw_status imu_start_foc();
enum bw_status imu_read_error();
enum bw_status imu_read_int_status(uint8_t int_status[2]);
enum bw_status imu_read_step_cnt(uint16_t *step_cnt);
enum bw_status imu_clear_step_cnt();
enum bw_status imu_enable_nomo_int();
enum bw_status imu_disable_nomo_int();
enum bw_status imu_start_stream(bool gyro_en);
enum bw_status imu_read_sample(struct acc_sample *acc, struct gyr_sample *gyr);
enum bw_status imu_stop_stream();
enum bw_status imu_wakeup();
enum bw_status imu_sleep();
enum bw_status imu_shutdown();
struct spi_handle *imu_get_spi_handle();
struct exti_handle *imu_get_exti_handle(exti_callback_t *callback);

#endif

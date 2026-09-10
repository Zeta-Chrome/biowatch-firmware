#ifndef DRIVER_OXIM_H
#define DRIVER_OXIM_H

#include "drivers/exti/exti.h"
#include "drivers/i2c/i2c.h"
#include "lib/status.h"
#include <stdint.h>

#define MAX_SPO2_SAMPLES 512
#define MAX_HR_SAMPLES 128

enum oxim_mode {
	OXIM_MODE_HR = 0x2,
	OXIM_MODE_SPO2 = 0x3,
};

enum oxim_smp_avg {
	OXIM_SMP_AVG_1,
	OXIM_SMP_AVG_2,
	OXIM_SMP_AVG_4,
	OXIM_SMP_AVG_8,
	OXIM_SMP_AVG_16,
	OXIM_SMP_AVG_32,
};

enum oxim_smp_rate {
	OXIM_SMP_RATE_50,
	OXIM_SMP_RATE_100,
	OXIM_SMP_RATE_200,
	OXIM_SMP_RATE_400,
};

typedef void (*oxim_callback_t)();

// MAX30102
enum bw_status oxim_init(enum oxim_smp_avg smp_avg, enum oxim_smp_rate smp_rate,
						 oxim_callback_t callback);
enum bw_status oxim_reconfigure();
struct i2c_handle *oxim_get_i2c_handle();
struct exti_handle *oxim_get_exti_handle(exti_callback_t *callback);
enum bw_status oxim_read_int_status(uint8_t int_status[2]);
enum bw_status oxim_start_temp_conversion();
enum bw_status oxim_read_temp(int *temp_milli_c);
enum bw_status oxim_start_mode(enum oxim_mode mode);
enum bw_status oxim_read_sample(uint32_t *red_sample, uint32_t *ir_sample);
enum bw_status oxim_shutdown();

#endif

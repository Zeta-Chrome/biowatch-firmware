#ifndef DRIVER_OXIM_H
#define DRIVER_OXIM_H

#include "drivers/exti/exti.h"
#include "drivers/i2c/i2c.h"
#include "lib/status.h"
#include <stdint.h>

#define MAX_SPO2_SAMPLES 512
#define MAX_HR_SAMPLES 128

typedef enum
{
    OXIM_MODE_HR = 0x2,
    OXIM_MODE_SPO2 = 0x3,
} oxim_mode_t;

typedef enum
{
    OXIM_SMP_AVG_1,
    OXIM_SMP_AVG_2,
    OXIM_SMP_AVG_4,
    OXIM_SMP_AVG_8,
    OXIM_SMP_AVG_16,
    OXIM_SMP_AVG_32,
} oxim_smp_avg_t;

typedef enum
{
    OXIM_SMP_RATE_50,
    OXIM_SMP_RATE_100,
    OXIM_SMP_RATE_200,
    OXIM_SMP_RATE_400,
} oxim_smp_rate_t;

typedef void (*oxim_callback_t)();

// MAX30102
bw_status_t oxim_init(oxim_smp_avg_t smp_avg, oxim_smp_rate_t smp_rate, oxim_callback_t callback);
bw_status_t oxim_reconfigure();
i2c_handle_t *oxim_get_i2c_handle();
exti_handle_t *oxim_get_exti_handle(exti_callback_t *callback);
bw_status_t oxim_read_int_status(uint8_t int_status[2]);
bw_status_t oxim_start_temp_conversion();
bw_status_t oxim_read_temp(int *temp_milli_c);
bw_status_t oxim_start_mode(oxim_mode_t mode);
bw_status_t oxim_read_sample(uint32_t *red_sample, uint32_t *ir_sample);
bw_status_t oxim_shutdown();

#endif

#ifndef DRIVER_OXIM_H
#define DRIVER_OXIM_H

#include "hal/exti/exti.h"
#include "hal/i2c/i2c.h"
#include "utils/status.h"
#include <stdint.h>

#define MAX_DATA_COUNT 6 * 512

typedef enum
{
    OXIM_STATE_UNINTILAIZED,
    OXIM_STATE_I2C_ERR,
    OXIM_STATE_POR,
    OXIM_STATE_ALC_OVF,
    OXIM_STATE_READY
} oxim_state_t;

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
    OXIM_SMP_RATE_800,
    OXIM_SMP_RATE_1000,
    OXIM_SMP_RATE_1600,
    OXIM_SMP_RATE_3200,
} oxim_smp_rate_t;

typedef enum
{
    OXIM_SMP_64 = 64,
    OXIM_SMP_128 = 128,
    OXIM_SMP_256 = 256,
    OXIM_SMP_512 = 512,
} oxim_smp_t;

typedef struct
{
    uint8_t data[MAX_DATA_COUNT];
    uint16_t count;
    uint8_t sample_size;
} oxim_data_t;

// MAX30102
void oxim_init(oxim_smp_avg_t smp_avg, oxim_smp_rate_t smp_rate, oxim_smp_t samples);
i2c_handle_t *oxim_get_i2c_handle();
exti_handle_t *oxim_get_exti_handle(exti_callback_t *callback);
bw_status_t oxim_read(oxim_mode_t mode);
void oxim_shutdown();
oxim_state_t get_oxim_state();
oxim_data_t *get_oxim_data();

#endif

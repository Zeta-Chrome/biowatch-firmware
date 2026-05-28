#ifndef DRIVER_BARO_H
#define DRIVER_BARO_H

#include "hal/i2c/i2c.h"

typedef enum
{
    BARO_STATE_UNINTILAIZED,
    BARO_STATE_I2C_ERR,
    BARO_STATE_READY
} baro_state_t;

typedef enum 
{
    BARO_MODE_HANDHELD_LP, // Low power
    BARO_MODE_HANDHELD_DY, // dynamic
    BARO_MODE_WEATHER_MON,
    BARO_MODE_FLOOR_CHANGE,
    BARO_MODE_DROP_DETECT,
    BARO_MODE_INDOOR_NAV,
} baro_mode_t;

void baro_init(baro_mode_t mode);
void baro_reconfigure(baro_mode_t mode);
i2c_handle_t *baro_get_i2c_handle();
bw_status_t baro_read(uint32_t *press_hpa, uint32_t *temp_hc);
void baro_sleep();

#endif

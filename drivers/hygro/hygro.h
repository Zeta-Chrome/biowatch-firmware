#ifndef DRIVER_HYGRO_H
#define DRIVER_HYGRO_H

#include "hal/i2c/i2c.h"

typedef enum
{
    HYGRO_REPEATABILITY_HIGH,   // 4ms
    HYGRO_REPEATABILITY_MEDIUM, // 6ms
    HYGRO_REPEATABILITY_LOW,    // 15ms
} hygro_repeatability_t;

void hygro_init();
bw_status_t hygro_read(hygro_repeatability_t repeatability, uint16_t *rhx100, int *tempx100);
bw_status_t hygro_soft_reset();
i2c_handle_t *hygro_get_i2c_handle();

#endif

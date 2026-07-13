#ifndef BMP_H
#define BMP_H

#include "subsys/ui/ui_common.h"

typedef struct
{
    ui_size_t size;
    const uint8_t *data;
} bmp_t;

extern const bmp_t logo_bmp;
extern const bmp_t clock_bmp;
extern const bmp_t steps_bmp;
extern const bmp_t heart_rate_bmp;
extern const bmp_t spo2_bmp;
extern const bmp_t calories_bmp;
extern const bmp_t weather_bmp;
extern const bmp_t ble_bmp;
extern const bmp_t settings_bmp;

#endif

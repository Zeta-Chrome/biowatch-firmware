#ifndef BMP_H
#define BMP_H

#include "subsys/ui/ui_common.h"

struct bmp {
	struct ui_size size;
	const uint8_t *data;
};

extern const struct bmp logo_bmp;
extern const struct bmp clock_bmp;
extern const struct bmp steps_bmp;
extern const struct bmp vitals_bmp;
extern const struct bmp calories_bmp;
extern const struct bmp weather_bmp;
extern const struct bmp ble_bmp;
extern const struct bmp settings_bmp;
extern const struct bmp time_bmp;
extern const struct bmp calendar_bmp;
extern const struct bmp timer_bmp;
extern const struct bmp heart_rate_bmp;
extern const struct bmp spo2_bmp;

#endif

#ifndef DRIVER_HYGRO_H
#define DRIVER_HYGRO_H

#include "drivers/i2c/i2c.h"

enum hygro_repeatability {
	HYGRO_REPEATABILITY_HIGH, // 4ms
	HYGRO_REPEATABILITY_MEDIUM, // 6ms
	HYGRO_REPEATABILITY_LOW, // 15ms
};

void hygro_init();
enum bw_status hygro_read(enum hygro_repeatability repeatability, uint16_t *rhx100, int *tempx100);
enum bw_status hygro_soft_reset();
struct i2c_handle *hygro_get_i2c_handle();

#endif

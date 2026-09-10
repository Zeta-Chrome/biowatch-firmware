#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdint.h>

struct app_settings {
	const char *manuf_name;
	const char *fw_version;
	uint16_t height_cm;
	uint16_t weight_kg;
};

extern struct app_settings g_app_settings;

#endif

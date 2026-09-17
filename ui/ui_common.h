#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdint.h>

#define MAX_FONT_CHARS 224

struct ui_size {
	uint8_t w;
	uint8_t h;
};

struct ui_rect {
	uint8_t x; // 0-63
	uint8_t y; // 0-127
	uint8_t w; // 0-127
	uint8_t h; // 0-63
};

#endif

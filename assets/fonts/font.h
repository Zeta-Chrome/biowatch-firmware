#ifndef FONT_H
#define FONT_H

#include <stdint.h>

struct font_glyph {
	uint16_t bitmap_offset;
	uint8_t width;
	uint8_t x_advance;
	uint8_t x_offset;
};

struct font {
	uint8_t height;
	uint8_t first_char;
	uint8_t num_chars;
	const struct font_glyph *glyphs;
	const uint8_t *bitmap;
};

#endif

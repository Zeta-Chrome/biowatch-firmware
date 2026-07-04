#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct
{
    uint16_t bitmap_offset;
    uint8_t width;
    uint8_t x_advance;
    uint8_t x_offset;
} font_glyph_t;

typedef struct
{
    uint8_t height;
    uint8_t first_char;
    uint8_t num_chars;
    const font_glyph_t *glyphs;
    const uint8_t *bitmap;
} font_t;

#endif

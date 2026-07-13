#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lib/utils.h"
#include <stdint.h>

#define MAX_FONT_CHARS 224

typedef struct
{
    uint8_t w;
    uint8_t h;
} ui_size_t;

typedef struct
{
    uint8_t x; // 0-63
    uint8_t y; // 0-127
    uint8_t w; // 0-127
    uint8_t h; // 0-63
} ui_rect_t;

#endif

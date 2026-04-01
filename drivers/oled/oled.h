#ifndef DRIVER_OLED_H
#define DRIVER_OLED_H

#include "drivers/oled/oled_cmds.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct 
{
    uint8_t col; // 0-127
    uint8_t page; // 0-8
} oled_coord_t;

void oled_init();
bool oled_is_initialized();
bool oled_is_flushed();
bool oled_bus_error();

void oled_power_on();
void oled_power_off();
void oled_display_normal();
void oled_display_inverse();
void oled_set_brightness(uint8_t value);

void oled_flush();
void oled_clear_screen();
void oled_fill_rect(oled_coord_t top_left, oled_coord_t bottom_right, uint8_t value);
void oled_draw_bitmap(oled_coord_t top_left, uint8_t cols, uint8_t pages, const uint8_t *data);

#endif

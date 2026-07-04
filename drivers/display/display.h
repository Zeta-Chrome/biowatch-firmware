#ifndef DRIVER_DISPLAY_H
#define DRIVER_DISPLAY_H

#include "hal/i2c/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_SCREEN_W 128
#define DISPLAY_SCREEN_H 64

typedef enum
{
    DISPLAY_STATE_UNINITIALIZED,
    DISPLAY_STATE_I2C_ERR,
    DISPLAY_STATE_READY
} display_state_t;

void display_init(bool invert_x, bool invert_y);
display_state_t display_get_state();
i2c_handle_t *display_get_i2c_handle();

void display_power_on();
void display_power_off();
void display_normal();
void display_inverse();
void display_set_brightness(uint8_t value);

void display_clear_screen();
void display_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t value);
void display_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *data);
void display_region_invert(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void display_flush();

#endif

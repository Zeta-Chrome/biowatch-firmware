#ifndef BSP_PINS_H
#define BSP_PINS_H

#include "drivers/gpio/gpio_types.h"

// Port A pins
static const gpio_t PA0 = {.port = GPIOA, .pin = 0};
static const gpio_t PA1 = {.port = GPIOA, .pin = 1};
static const gpio_t PA2 = {.port = GPIOA, .pin = 2};
static const gpio_t PA3 = {.port = GPIOA, .pin = 3};
static const gpio_t PA4 = {.port = GPIOA, .pin = 4};
static const gpio_t PA5 = {.port = GPIOA, .pin = 5};
static const gpio_t PA6 = {.port = GPIOA, .pin = 6};
static const gpio_t PA7 = {.port = GPIOA, .pin = 7};
static const gpio_t PA8 = {.port = GPIOA, .pin = 8};
static const gpio_t PA9 = {.port = GPIOA, .pin = 9};
static const gpio_t PA10 = {.port = GPIOA, .pin = 10};
static const gpio_t PA15 = {.port = GPIOA, .pin = 15};

// Port B pins
static const gpio_t PB0 = {.port = GPIOB, .pin = 0};
static const gpio_t PB1 = {.port = GPIOB, .pin = 1};
static const gpio_t PB2 = {.port = GPIOB, .pin = 2};
static const gpio_t PB4 = {.port = GPIOB, .pin = 4};
static const gpio_t PB5 = {.port = GPIOB, .pin = 5};
static const gpio_t PB6 = {.port = GPIOB, .pin = 6};
static const gpio_t PB7 = {.port = GPIOB, .pin = 7};
static const gpio_t PB8 = {.port = GPIOB, .pin = 8};
static const gpio_t PB9 = {.port = GPIOB, .pin = 9};

// Port E pins
static const gpio_t PE4 = {.port = GPIOE, .pin = 4};

#endif

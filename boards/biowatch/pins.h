#ifndef BSP_PINS_H
#define BSP_PINS_H

#include "drivers/gpio/gpio_types.h"

// Port A pins
static const struct gpio PA0 = { .port = GPIOA, .pin = 0 };
static const struct gpio PA1 = { .port = GPIOA, .pin = 1 };
static const struct gpio PA2 = { .port = GPIOA, .pin = 2 };
static const struct gpio PA3 = { .port = GPIOA, .pin = 3 };
static const struct gpio PA4 = { .port = GPIOA, .pin = 4 };
static const struct gpio PA5 = { .port = GPIOA, .pin = 5 };
static const struct gpio PA6 = { .port = GPIOA, .pin = 6 };
static const struct gpio PA7 = { .port = GPIOA, .pin = 7 };
static const struct gpio PA8 = { .port = GPIOA, .pin = 8 };
static const struct gpio PA9 = { .port = GPIOA, .pin = 9 };
static const struct gpio PA10 = { .port = GPIOA, .pin = 10 };
static const struct gpio PA15 = { .port = GPIOA, .pin = 15 };

// Port B pins
static const struct gpio PB0 = { .port = GPIOB, .pin = 0 };
static const struct gpio PB1 = { .port = GPIOB, .pin = 1 };
static const struct gpio PB2 = { .port = GPIOB, .pin = 2 };
static const struct gpio PB4 = { .port = GPIOB, .pin = 4 };
static const struct gpio PB5 = { .port = GPIOB, .pin = 5 };
static const struct gpio PB6 = { .port = GPIOB, .pin = 6 };
static const struct gpio PB7 = { .port = GPIOB, .pin = 7 };
static const struct gpio PB8 = { .port = GPIOB, .pin = 8 };
static const struct gpio PB9 = { .port = GPIOB, .pin = 9 };

// Port E pins
static const struct gpio PE4 = { .port = GPIOE, .pin = 4 };

#endif

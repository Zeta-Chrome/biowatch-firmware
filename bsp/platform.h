#ifndef PLATFORM_H
#define PLATFORM_H

#include "bsp/pins.h"
#include "core/utils/logger.h"
#include "core/device/stm32wb55xx.h"

#define PL_UART USART1
#define PL_UART_TX PA9

#define PL_OXIM_I2C I2C1
#define PL_OXIM_SDA PB7 
#define PL_OXIM_SCL PB6
#define PL_OXIM_INT PB5
#define PL_OXIM_EXTI EXTI9_5_IRQn 

#define PL_OLED_I2C I2C3
#define PL_OLED_SDA PB4
#define PL_OLED_SCL PA7

#define PL_IMU_SPI SPI1
#define PL_IMU_NSS PA4
#define PL_IMU_SCLK PA5
#define PL_IMU_MISO PA11
#define PL_IMU_MOSI PA12

void platform_init();

#endif

#ifndef BSP_H 
#define BSP_H 

#include "pins.h"
#include "stm32wb55xx.h"

#define PL_OXIM_I2C I2C1
#define PL_OXIM_SDA PB7
#define PL_OXIM_SCL PB6
#define PL_OXIM_EXTI PB1
#define PL_OXIM_EXTI_IRQn EXTI1_IRQn

#define PL_BARO_I2C I2C1
#define PL_BARO_SDA PB7
#define PL_BARO_SCL PB6

#define PL_OLED_I2C I2C3
#define PL_OLED_SDA PB4
#define PL_OLED_SCL PA7

#define PL_IMU_SPI SPI1
#define PL_IMU_CS PA4
#define PL_IMU_SCLK PA5
#define PL_IMU_MISO PA6
#define PL_IMU_MOSI PB5
#define PL_IMU_EXTI1 PA3
#define PL_IMU_EXTI1_IRQn EXTI3_IRQn

#define PL_VIB_PIN PA10
#define PL_BUZZ_PIN PB2

void bsp_init();

#endif

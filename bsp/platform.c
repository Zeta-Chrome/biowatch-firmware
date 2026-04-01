#include "bsp/platform.h"
#include "core/hal/reg.h"
#include "stm32wb55xx.h"
#include "system_stm32wbxx.h"

#ifdef DEBUG
void bw_logger_init();
#endif

// Remove this dummy function later
void scheduler_tick()
{}

void clock_init()
{
    // Set MSI to 16Mhz clk
    while ((RCC->CR & RCC_CR_MSION) && !(RCC->CR & RCC_CR_MSIRDY));
    reg_set_field(&RCC->CR, RCC_CR_MSIRANGE_Pos, 4, 0x8U);

    SystemCoreClockUpdate();
}

void enable_irqs()
{
    // OLED I2C 
    NVIC_EnableIRQ(I2C3_EV_IRQn);
    NVIC_EnableIRQ(I2C3_ER_IRQn);
    NVIC_SetPriority(I2C3_EV_IRQn, 2);
    NVIC_SetPriority(I2C3_ER_IRQn, 2);
    
    // Oximeter I2C
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
    NVIC_SetPriority(I2C1_EV_IRQn, 5);
    NVIC_SetPriority(I2C1_ER_IRQn, 5);

    // IMU SPI
    NVIC_EnableIRQ(SPI1_IRQn);
    NVIC_SetPriority(SPI1_IRQn, 1);

    // Oximter EXTI
    NVIC_EnableIRQ(PL_OXIM_EXTI);
    NVIC_SetPriority(PL_OXIM_EXTI, 5);
}

void platform_init()
{
    clock_init();
    enable_irqs();
    bw_logger_init();
}

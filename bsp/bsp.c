#include "bsp.h"
#include "baro/baro.h"
#include "core/utils/logger.h"
#include "core/hal/clock/clock.h"
#include "core/hal/systick/systick.h"
#include "hal/exti/exti.h"
#include "hal/gpio/gpio.h"
#include "hal/spi/spi.h"
#include "imu/imu.h"
#include "oled/oled.h"
#include "oxim/oxim.h"
#include "utils/utils.h"

void fault_init(void)
{
    SCB->SHCSR |= (SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk);

    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
}

void peripheral_init()
{
    // OLED I2C init
    i2c_conf_t conf = {.sda = PL_OLED_SDA,
                       .scl = PL_OLED_SCL,
                       .af = GPIO_AF4,
                       .i2c = PL_OLED_I2C,
                       .speed = I2C_SPEED_FAST,
                       .dnf = 0,
                       .irq_priority = 4};
    hal_i2c_init_dma(&conf, oled_get_i2c_handle());
    
    // Oximeter EXTI init
    exti_callback_t callback;
    exti_handle_t *oxim_exti_h = oxim_get_exti_handle(&callback);
    exti_conf_t exti_conf = {.gpio = PL_OXIM_EXTI,
                             .edge = EXTI_EDGE_FALLING,
                             .irq = PL_OXIM_EXTI_IRQn,
                             .irq_priority = 6,
                             .callback = callback,
                             .user_data = NULL};
    hal_exti_gpio_init(&exti_conf, oxim_exti_h);

    // Oximeter I2C init
    i2c_conf_t i2c_conf = {.sda = PL_OXIM_SDA,
                           .scl = PL_OXIM_SCL,
                           .af = GPIO_AF4,
                           .i2c = PL_OXIM_I2C,
                           .speed = I2C_SPEED_STANDARD,
                           .dnf = 0,
                           .irq_priority = 6};
    hal_i2c_init_dma(&i2c_conf, oxim_get_i2c_handle());
    // Copy the handles
    *baro_get_i2c_handle() = *oxim_get_i2c_handle();

    // IMU CS init
    gpio_conf_t cs_conf = gpio_conf_output(PL_IMU_CS, GPIO_SPEED_MEDIUM);
    hal_gpio_init(&cs_conf);
    
    // IMU SPI Init
    spi_conf_t spi_conf = {.spi = PL_IMU_SPI,
                           .mosi = PL_IMU_MOSI,
                           .miso = PL_IMU_MISO,
                           .sck = PL_IMU_SCLK,
                           .baud_rate = SPI_BAUD_RATE_DIV_16,
                           .cpol = SPI_CLOCK_POLARITY_HIGH,
                           .cpha = SPI_CLOCK_PHASE_TRAILING,
                           .mode = SPI_MODE_FULL_DUPLEX,
                           .frame_format = SPI_FRAME_FORMAT_MSBFIRST,
                           .irq_priority = 4};
    hal_spi_init_dma(&spi_conf, imu_get_spi_handle());
    
    // SPI EXTI init
    exti_handle_t* imu_exti_h = imu_get_exti_handle(&callback);
    exti_conf_t imu_exti_conf = {.gpio = PL_IMU_EXTI1,
                             .edge = EXTI_EDGE_FALLING,
                             .irq = PL_IMU_EXTI1_IRQn,
                             .irq_priority = 6,
                             .callback = callback,
                             .user_data = NULL};
    hal_exti_gpio_init(&imu_exti_conf, imu_exti_h);
}

void bsp_init()
{
    fault_init();
    hal_clock_init();
    hal_systick_init(0);
    bw_logger_init();
    peripheral_init();
}

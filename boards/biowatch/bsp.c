#include "bsp.h"
#include "drivers/clock/clock.h"
#include "drivers/display/display.h"
#include "drivers/exti/exti.h"
#include "drivers/gpio/gpio.h"
#include "drivers/i2c/i2c_bus.h"
#include "drivers/sensor/hygro/hygro.h"
#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/oxim/oxim.h"
#include "drivers/spi/spi.h"
#include "drivers/spi/spi_bus.h"
#include "lib/logger.h"
#include "lib/utils.h"

static void fault_init(void)
{
    SCB->SHCSR |= (SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk);
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;

#ifdef DEBUG
    SCnSCB->ACTLR |= SCnSCB_ACTLR_DISDEFWBUF_Msk;
#endif
}

static void sys_pwr_config()
{
    PWR->C2CR1 = (PWR->C2CR1 & ~PWR_C2CR1_LPMS) | PWR_CR1_LPMS_2;
}

static void peripheral_init()
{
    // BLE EXTI Init
    exti_enable_line(36); // IPCC wakeup interrupts
    exti_enable_line(38); // HSEM wakeup interrupts

    // OLED I2C init
    i2c_conf_t oled_i2c_conf = {.sda = PL_OLED_SDA,
                                .scl = PL_OLED_SCL,
                                .af = GPIO_AF4,
                                .i2c = PL_OLED_I2C,
                                .speed = I2C_SPEED_FAST,
                                .dnf = 0,
                                .irq_priority = 4};
    i2c_init_dma(&oled_i2c_conf, display_get_i2c_handle());

    // Oximeter EXTI init
    exti_callback_t callback;
    exti_handle_t *oxim_exti_h = oxim_get_exti_handle(&callback);
    exti_conf_t oxim_exti_conf = {.gpio = PL_OXIM_EXTI,
                                  .edge = EXTI_EDGE_FALLING,
                                  .irq = PL_OXIM_EXTI_IRQn,
                                  .irq_priority = 6,
                                  .callback = callback,
                                  .user_data = NULL};
    exti_gpio_init(&oxim_exti_conf, oxim_exti_h);

    // Oximeter I2C init
    i2c_conf_t i2c_conf = {.sda = PL_OXIM_SDA,
                           .scl = PL_OXIM_SCL,
                           .af = GPIO_AF4,
                           .i2c = PL_OXIM_I2C,
                           .speed = I2C_SPEED_STANDARD,
                           .dnf = 0,
                           .irq_priority = 6};
    i2c_init_dma(&i2c_conf, oxim_get_i2c_handle());
    // Copy the handles
    *hygro_get_i2c_handle() = *oxim_get_i2c_handle();

    // Initialize the i2c bus for oximeter and hygrometer
    i2c_bus_init(oxim_get_i2c_handle()->perip);

    // IMU CS init
    gpio_conf_t cs_conf = gpio_conf_output(PL_IMU_CS, GPIO_SPEED_MEDIUM);
    gpio_init(&cs_conf);

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
    spi_init_dma(&spi_conf, imu_get_spi_handle());
    spi_bus_init(imu_get_spi_handle()->perip);

    // SPI EXTI init
    exti_handle_t *imu_exti_h = imu_get_exti_handle(&callback);
    exti_conf_t imu_exti_conf = {.gpio = PL_IMU_EXTI1,
                                 .edge = EXTI_EDGE_FALLING,
                                 .irq = PL_IMU_EXTI1_IRQn,
                                 .irq_priority = 6,
                                 .callback = callback,
                                 .user_data = NULL};
    exti_gpio_init(&imu_exti_conf, imu_exti_h);
}

void bsp_init()
{
    fault_init();
    bw_logger_init();

    clock_conf_t clock_conf = clock_conf_performance();
    clock_reconfigure(&clock_conf);

    sys_pwr_config();
    peripheral_init();
}

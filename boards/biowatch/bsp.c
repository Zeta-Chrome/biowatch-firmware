#include "bsp.h"
#include "app/tasks/task_ui.h"
#include "drivers/clock/clock.h"
#include "drivers/clock/clock_srcs.h"
#include "drivers/display/display.h"
#include "drivers/exti/exti.h"
#include "drivers/gpio/gpio.h"
#include "drivers/i2c/i2c_bus.h"
#include "drivers/rtc/rtc.h"
#include "drivers/sensor/hygro/hygro.h"
#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/oxim/oxim.h"
#include "drivers/spi/spi.h"
#include "drivers/spi/spi_bus.h"
#include "lib/logger.h"
#include "lib/utils.h"
#include "stm32wb55xx.h"
#include <stddef.h>

static void fault_init(void)
{
	SCB->SHCSR |=
		(SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk);
	SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;

#ifdef DEBUG
	SCnSCB->ACTLR |= SCnSCB_ACTLR_DISDEFWBUF_Msk;
#endif
}

static void clock_init()
{
	// Clock is PLL with source set to HSE
	struct clock_conf clock_conf = clock_conf_performance();
	clock_reconfigure(&clock_conf);

	// Turn on LSE
	clock_enable_lse();
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
	struct i2c_conf oled_i2c_conf = { .sda = PL_OLED_SDA,
									  .scl = PL_OLED_SCL,
									  .af = GPIO_AF4,
									  .i2c = PL_OLED_I2C,
									  .speed = I2C_SPEED_FAST,
									  .dnf = 0,
									  .irq_priority = 4 };
	i2c_init_dma(&oled_i2c_conf, display_get_i2c_handle());

	// Oximeter EXTI init
	exti_callback_t callback;
	struct exti_handle *oxim_exti_h = oxim_get_exti_handle(&callback);
	struct exti_conf oxim_exti_conf = { .gpio = PL_OXIM_EXTI,
										.edge = EXTI_EDGE_FALLING,
										.irq = PL_OXIM_EXTI_IRQn,
										.irq_priority = 6,
										.callback = callback,
										.user_data = NULL };
	exti_gpio_init(&oxim_exti_conf, oxim_exti_h);

	// Oximeter I2C init
	struct i2c_conf i2c_conf = { .sda = PL_OXIM_SDA,
								 .scl = PL_OXIM_SCL,
								 .af = GPIO_AF4,
								 .i2c = PL_OXIM_I2C,
								 .speed = I2C_SPEED_STANDARD,
								 .dnf = 0,
								 .irq_priority = 6 };
	i2c_init_dma(&i2c_conf, oxim_get_i2c_handle());
	// Copy the handles
	*hygro_get_i2c_handle() = *oxim_get_i2c_handle();

	// Initialize the i2c bus for oximeter and hygrometer
	i2c_bus_init(oxim_get_i2c_handle()->perip);

	// IMU CS init
	struct gpio_conf cs_conf = gpio_conf_output(PL_IMU_CS, GPIO_SPEED_MEDIUM);
	gpio_init(&cs_conf);

	// IMU SPI Init
	struct spi_conf spi_conf = { .spi = PL_IMU_SPI,
								 .mosi = PL_IMU_MOSI,
								 .miso = PL_IMU_MISO,
								 .sck = PL_IMU_SCLK,
								 .baud_rate = SPI_BAUD_RATE_DIV_16,
								 .cpol = SPI_CLOCK_POLARITY_HIGH,
								 .cpha = SPI_CLOCK_PHASE_TRAILING,
								 .mode = SPI_MODE_FULL_DUPLEX,
								 .frame_format = SPI_FRAME_FORMAT_MSBFIRST,
								 .irq_priority = 4 };
	spi_init_dma(&spi_conf, imu_get_spi_handle());
	spi_bus_init(imu_get_spi_handle()->perip);

	// IMU EXTI init
	struct exti_handle *imu_exti_h = imu_get_exti_handle(&callback);
	struct exti_conf imu_exti_conf = { .gpio = PL_IMU_EXTI1,
									   .pupd = GPIO_PULL_NONE,
									   .edge = EXTI_EDGE_FALLING,
									   .irq = PL_IMU_EXTI1_IRQn,
									   .irq_priority = 6,
									   .callback = callback,
									   .user_data = NULL };
	exti_gpio_init(&imu_exti_conf, imu_exti_h);

	// Next button
	struct exti_handle *next_btn_exti_h = ui_get_next_btn_handle(&callback);
	struct exti_conf next_btn_conf = { .gpio = PL_NEXT_PIN,
									   .pupd = GPIO_PULL_UP,
									   .edge = EXTI_EDGE_BOTH,
									   .irq = EXTI9_5_IRQn,
									   .irq_priority = 5,
									   .callback = callback,
									   .user_data = next_btn_exti_h->user_data };
	exti_gpio_init(&next_btn_conf, next_btn_exti_h);

	struct exti_handle *click_btn_exti_h = ui_get_click_btn_handle(&callback);
	struct exti_conf click_btn_conf = { .gpio = PL_CLICK_PIN,
										.pupd = GPIO_PULL_UP,
										.edge = EXTI_EDGE_BOTH,
										.irq = EXTI9_5_IRQn,
										.irq_priority = 5,
										.callback = callback,
										.user_data = click_btn_exti_h->user_data };
	exti_gpio_init(&click_btn_conf, click_btn_exti_h);

	struct exti_handle *home_btn_exti_h = ui_get_home_btn_handle(&callback);
	struct exti_conf home_btn_conf = { .gpio = PL_HOME_PIN,
									   .pupd = GPIO_PULL_UP,
									   .edge = EXTI_EDGE_FALLING,
									   .irq = EXTI2_IRQn,
									   .irq_priority = 5,
									   .callback = callback,
									   .user_data = home_btn_exti_h->user_data };
	exti_gpio_init(&home_btn_conf, home_btn_exti_h);

	// Start RTC
	rtc_init();
	rtc_set_hr_format(RTC_HR_FMT_12);
}

void bsp_init()
{
	fault_init();
	bw_logger_init();
	clock_init();
	sys_pwr_config();
	peripheral_init();
}

#include "drivers/oled/oled.h"
#include "drivers/oxim/oxim.h"
#include "core/hal/systick/systick.h"
#include "bsp/platform.h"
#include "hal/gpio/gpio.h"

int main()
{
    platform_init();

    gpio_t led_gpio = PA15;
    gpio_conf_t led_conf = gpio_conf_output(led_gpio, GPIO_SPEED_HIGH);
    hal_gpio_init(&led_conf);
    hal_gpio_set_level(led_gpio, 1);

    BW_LOG("Introducing the BIOWATCH: A Low Power health monitoring watch\n");

    oxim_init();
    oxim_read();

    while (1)
    {
        hal_gpio_set_level(led_gpio, 0);
        hal_systick_delay_ms(1000);
        hal_gpio_set_level(led_gpio, 1);
        hal_systick_delay_ms(1000);
    };

    return 0;
}

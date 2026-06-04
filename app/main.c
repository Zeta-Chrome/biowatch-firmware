#include "baro/baro.h"
#include "ble/ble.h"
#include "bsp.h"
#include "hal/gpio/gpio.h"
#include "hal/gpio/gpio_types.h"
#include "oled/oled.h"
#include "pins.h"
#include "rtos/rtos.h"
#include "rtos/task/task.h"
#include "utils/utils.h"
#include <stdint.h>

#include "oxim/oxim.h"

void ble_task(void *user_data)
{
    ble_init();

    while(1)
    {}
}

void oximeter_task(void *user_data)
{
    (void)user_data;
    oxim_init(OXIM_SMP_AVG_1, OXIM_SMP_RATE_100, OXIM_SMP_64);
    if (get_oxim_state() != OXIM_STATE_READY)
    {
        rtos_task_delete(NULL);
    }

    bw_status_t status = oxim_read(OXIM_MODE_HR);
    if (status != STATUS_OK)
    {
        BW_LOG("Oximeter exited with status: %d and state: %d\n", status, get_oxim_state());
    }

    oxim_data_t *data = get_oxim_data();
    for (int i = 0; i < data->count; i++)
    {
        BW_PRINT("%d ", data->data[i] >> 14);
        rtos_task_delay(10);
    }
    BW_PRINT("\n");

    while (1)
    {
        rtos_task_delay(5000);
    }
}

void barometer_task(void *user_data)
{
    (void)user_data;
    baro_init(BARO_MODE_INDOOR_NAV);

    uint32_t press = 0, temp = 0;
    while (1)
    {
        baro_read(&press, &temp);
        BW_LOG(
            "Pressure: %d.%d, temperature: %d.%d\n",
            press / 10,
            press % 10,
            temp / 10,
            temp % 10);
        rtos_task_delay(5000);
    }
}

void display_task(void *user_data)
{
    (void)user_data;
    oled_init();

    oled_clear_screen();
    oled_flush();

    uint8_t counter = 0;
    while (1)
    {
        oled_clear_screen();
        oled_fill_rect((oled_coord_t){0, 0}, (oled_coord_t){127, 2}, 0xF0);
        oled_fill_rect((oled_coord_t){0, 3}, (oled_coord_t){127, 5}, 0xCC);
        oled_fill_rect((oled_coord_t){0, 6}, (oled_coord_t){127, 7}, 0xAA);
        oled_flush();

        oled_set_brightness(counter++);
        oled_display_inverse();
        rtos_task_delay(100);
        oled_display_normal();
        rtos_task_delay(100);
    }
}

static void idle_hook(void *user_data)
{
    (void)user_data;
}

void led_task(void *user_data)
{
    (void)user_data;
    gpio_conf_t gpio_conf = gpio_conf_output(PA15, GPIO_SPEED_FAST);
    hal_gpio_init(&gpio_conf);

    hal_gpio_set_level(PA15, 1);
    while (1)
    {
        hal_gpio_set_level(PA15, 0);
        rtos_task_delay(500);
        hal_gpio_set_level(PA15, 1);
        rtos_task_delay(500);
    }
    hal_gpio_set_level(PA15, 0);
}

int main()
{
    bsp_init();

    rtos_conf_t conf = {
        .pool_confs =
            {{.sz = MEM_BLOCK_SZ_2048, .count = 2},
             {.sz = MEM_BLOCK_SZ_1024, .count = 2},
             {.sz = MEM_BLOCK_SZ_512, .count = 4},
             {.sz = MEM_BLOCK_SZ_256, .count = 4},
             {.sz = MEM_BLOCK_SZ_128, .count = 4}},
        .idle_hook = idle_hook,
        .idle_data = NULL};
    rtos_init(&conf);
    task_handle_t handle0;
    rtos_task_create(ble_task, "Button Task", 7, 256, NULL, &handle0);

    task_handle_t handle3;
    rtos_task_create(led_task, "LED Task", 7, 32, NULL, &handle3);
    rtos_run();
}

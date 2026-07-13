#include "assets/fonts/tamzen12b.h"
#include "assets/fonts/tamzen9.h"
#include "biowatch/bsp.h"
#include "biowatch/pins.h"
#include "drivers/adc/adc.h"
#include "drivers/exti/exti.h"
#include "drivers/gpio/gpio.h"
#include "drivers/sensor/hygro/hygro.h"
#include "kernel/sync/event.h"
#include "kernel/task/task.h"
#include "lib/utils.h"
#include "subsys/ble/ble.h"
#include "subsys/ui/ui.h"
#include "subsys/ui/widget.h"

static exti_handle_t bp_h;
static exti_handle_t b1_h;
static exti_handle_t b2_h;

static void b1_callback(void *user_data)
{
    BW_LOG("Button 1 pressed !!!\n");
}

static void b2_callback(void *user_data)
{
    BW_LOG("Button 2 pressed !!!\n");
}

static void bp_callback(void *user_data)
{
    BW_LOG("power button pressed !!!\n");
}

static void button_task(void *user_data)
{
    exti_conf_t b1conf = {.gpio = PA8,
                          .pupd = GPIO_PULL_UP,
                          .edge = EXTI_EDGE_FALLING,
                          .irq = EXTI9_5_IRQn,
                          .irq_priority = 5,
                          .callback = b1_callback,
                          .user_data = NULL};
    exti_gpio_init(&b1conf, &b1_h);

    exti_conf_t b2conf = {.gpio = PA9,
                          .pupd = GPIO_PULL_UP,
                          .edge = EXTI_EDGE_FALLING,
                          .irq = EXTI9_5_IRQn,
                          .irq_priority = 5,
                          .callback = b2_callback,
                          .user_data = NULL};
    exti_gpio_init(&b2conf, &b2_h);

    exti_conf_t bpconf = {.gpio = PA2,
                          .pupd = GPIO_PULL_UP,
                          .edge = EXTI_EDGE_FALLING,
                          .irq = EXTI2_IRQn,
                          .irq_priority = 5,
                          .callback = bp_callback,
                          .user_data = NULL};
    exti_gpio_init(&bpconf, &bp_h);

    while (1)
    {
        kernel_task_delay(1000);
    }
}

static void vibration_task(void *user_data)
{
    gpio_conf_t conf = gpio_conf_output(PL_BUZZ_PIN, GPIO_SPEED_LOW);
    gpio_init(&conf);

    while (1)
    {
        gpio_set_level(PL_BUZZ_PIN, 0);
        kernel_task_delay(50);
        gpio_set_level(PL_BUZZ_PIN, 1);
        kernel_task_delay(5);
    }
}

static void display_task(void *user_data)
{
    (void)user_data;

    ui_init();
    ui_widget_t *startup_page = ui_widget_create_col(1, false, 0x00, 0, 1);
    ui_widget_t *logo = ui_widget_create_image(4, false, false, &logo_bmp);
    ui_widget_t *title = ui_widget_create_text(1, false, true, "BIOWATCH", &tamzen12b);

    ui_container_add_child(startup_page, logo);
    ui_container_add_child(startup_page, title);
    ui_build(startup_page);

    ui_widget_t *app_page = ui_widget_create_col(1, false, 0xFF, 0, 2);
    ui_widget_t *clock = ui_widget_create_row(2, false, 0x00, 2, 1);
    ui_widget_t *time = ui_widget_create_text(1, false, false, "11:43 PM", &tamzen12b);
    ui_widget_t *date = ui_widget_create_text(1, false, false, "03/07/26", &tamzen9);
    ui_widget_t *app_grid = ui_widget_create_grid(5, false, 0x00, 2, 2, 2, 4);
    ui_widget_t *clock_app = ui_widget_create_image(1, true, false, &clock_bmp);
    ui_widget_t *steps_app = ui_widget_create_image(1, true, false, &steps_bmp);
    ui_widget_t *heart_app = ui_widget_create_image(1, true, false, &heart_rate_bmp);
    ui_widget_t *spo2_app = ui_widget_create_image(1, true, false, &spo2_bmp);
    ui_widget_t *calories_app = ui_widget_create_image(1, true, false, &calories_bmp);
    ui_widget_t *weather_app = ui_widget_create_image(1, true, false, &weather_bmp);
    ui_widget_t *ble_app = ui_widget_create_image(1, true, false, &ble_bmp);
    ui_widget_t *settings_app = ui_widget_create_image(1, true, false, &settings_bmp);

    ui_container_add_child(app_page, clock);
    ui_container_add_child(clock, time);
    ui_container_add_child(clock, date);
    ui_container_add_child(app_page, app_grid);
    ui_container_add_child(app_grid, clock_app);
    ui_container_add_child(app_grid, steps_app);
    ui_container_add_child(app_grid, heart_app);
    ui_container_add_child(app_grid, spo2_app);
    ui_container_add_child(app_grid, calories_app);
    ui_container_add_child(app_grid, weather_app);
    ui_container_add_child(app_grid, ble_app);
    ui_container_add_child(app_grid, settings_app);
    ui_build(app_page);

    ui_set_root_widget(app_page);
    ui_draw();

    while (1)
    {
        kernel_task_delay(1000);
    }
}

#define EVENT_LDR_SUCCESS BIT(0)
#define EVENT_LDR_FAILURE BIT(1)

static event_t adc_event;

static void adc_callback(bw_status_t status, void *user_data)
{
    if (status == STATUS_OK)
    {
        kernel_event_set(&adc_event, EVENT_LDR_SUCCESS);
    }
    else
    {
        kernel_event_set(&adc_event, EVENT_LDR_FAILURE);
    }
}

static void ldr_task(void *user_data)
{
    adc_conf_t conf = {.inp = ADC_INP_SINGLE,
                       .gpios = {PA1},
                       .smp = {ADC_SMP_640_5_CLK},
                       .in = {ADC_CH_PA1},
                       .inlen = 1,
                       .irq_priority = 5};
    adc_init(&conf);

    uint16_t value;
    adc_handle_t handle = {.buf = &value, .inseq = {ADC_CH_PA1}, .inseqlen = 1, .callback = adc_callback};

    while (1)
    {
        adc_convert(&handle);

        uint32_t event_bit;
        bw_status_t status = kernel_event_wait(&adc_event, EVENT_LDR_SUCCESS | EVENT_LDR_FAILURE, &event_bit, true, false,
                                             2000);
        if (status != STATUS_OK)
        {
            BW_LOG("Exited with status: %d\n", status);
            continue;
        }

        if (event_bit & EVENT_LDR_SUCCESS)
        {
            BW_LOG("ADC Read Value is : %u\n", value);
        }
        else if (event_bit & EVENT_LDR_FAILURE)
        {
            BW_LOG("ADC Read failed\n");
        }

        kernel_task_delay(100);
    }
}

static void hygro_task(void *user_data)
{
    (void)user_data;

    hygro_init();

    uint16_t rhx100;
    int tempx100;
    while (1)
    {
        hygro_read(HYGRO_REPEATABILITY_HIGH, &rhx100, &tempx100);
        kernel_task_delay(1000);
    }
}

static void ble_task(void *user_data)
{
    (void)user_data;
    ble_sys_init();
    ble_init(68, 6, 2, "Bio Watch", BLE_APPEARANCE_GENERIC_WATCH, BLE_IO_CAPABILITY_NO_INPUT_NO_OUTPUT,
             BLE_MITM_PROTECTION_NOT_REQUIRED, BLE_SECURE_NOT_SUPPORTED, true);

    while (1)
    {
        kernel_task_delay(1000);
    }
}

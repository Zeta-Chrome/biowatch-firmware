#include "bsp.h"
#include "hal/exti/exti.h"
#include "hal/gpio/gpio.h"
#include "pins.h"
#include "rtos/sync/event.h"
#include "rtos/task/task.h"
#include "hal/adc/adc.h"
#include "utils/utils.h"


static exti_handle_t bp_h;
static exti_handle_t b1_h;
static exti_handle_t b2_h;

void b1_callback(void *user_data)
{
    BW_LOG("Button 1 pressed !!!\n");
}

void b2_callback(void *user_data)
{
    BW_LOG("Button 2 pressed !!!\n");
}

void bp_callback(void *user_data)
{
    BW_LOG("power button pressed !!!\n");
}

void button_task(void *user_data)
{
    exti_conf_t b1conf = {.gpio = PA8,
                          .pupd = GPIO_PULL_UP,
                          .edge = EXTI_EDGE_FALLING,
                          .irq = EXTI9_5_IRQn,
                          .irq_priority = 5,
                          .callback = b1_callback,
                          .user_data = NULL};
    hal_exti_gpio_init(&b1conf, &b1_h);

    exti_conf_t b2conf = {.gpio = PA9,
                          .pupd = GPIO_PULL_UP,
                          .edge = EXTI_EDGE_FALLING,
                          .irq = EXTI9_5_IRQn,
                          .irq_priority = 5,
                          .callback = b2_callback,
                          .user_data = NULL};
    hal_exti_gpio_init(&b2conf, &b2_h);

    exti_conf_t bpconf = {.gpio = PA2,
                          .pupd = GPIO_PULL_UP,
                          .edge = EXTI_EDGE_FALLING,
                          .irq = EXTI2_IRQn,
                          .irq_priority = 5,
                          .callback = bp_callback,
                          .user_data = NULL};
    hal_exti_gpio_init(&bpconf, &bp_h);

    while (1)
    {
        rtos_task_delay(1000);
    }
}

void vibration_task(void *user_data)
{
    gpio_conf_t conf = gpio_conf_output(PL_BUZZ_PIN, GPIO_SPEED_LOW);
    hal_gpio_init(&conf);

    while(1)
    {
        hal_gpio_set_level(PL_BUZZ_PIN, 0);
        rtos_task_delay(50);
        hal_gpio_set_level(PL_BUZZ_PIN, 1);
        rtos_task_delay(5);
    }
}

#include "imu/imu.h"
#include "imu/imu_regs.h"

static task_handle_t g_imu_task_h;
static uint16_t g_step_count = UINT16_MAX;

void imu_task(void *user_data)
{
    bw_status_t status;
    uint8_t int_status[4];

    imu_init(g_imu_task_h, IMU_STEP_MODE_NORMAL, IMU_TAP_NORMAL, IMU_DTAP_DUR_250MS, IMU_NOMO_NORMAL, 2);
    while (1)
    {
        rtos_task_notify_wait(0, 0, NULL, MAX_TIMEOUT);

        status = imu_read_int_status(int_status);
        if (status != STATUS_OK)
        {
            continue;
        }

        if (int_status[0] & IMU_INT_ST0_STAP_Msk)
        {
            BW_LOG("Single Tap!\n");
        }

        if (int_status[0] & IMU_INT_ST0_DTAP_Msk)
        {
            BW_LOG("Double Tap!!\n");
        }

        if (int_status[0] & IMU_INT_ST0_STEP_Msk)
        {
            status = imu_read_step_cnt(&g_step_count);
            BW_LOG("Steps: %d\n", g_step_count);
        }

        if (int_status[1] & IMU_INT_ST1_NOMO_Msk)
        {
            BW_LOG("No motion\n");
        }

        BW_LOG("%x, %x, %x, %x\n", int_status[0], int_status[1], int_status[2], int_status[3]);
    }
}

#define EVENT_LDR_SUCCESS BIT(0)
#define EVENT_LDR_FAILURE BIT(1)

static event_t adc_event;

void adc_callback(bw_status_t status, void *user_data)
{
    if (status == STATUS_OK)
    {
        rtos_event_set(&adc_event, EVENT_LDR_SUCCESS);
    }
    else
    {
        rtos_event_set(&adc_event, EVENT_LDR_FAILURE);
    }
}

void ldr_task(void *user_data)
{
    adc_conf_t conf = {.inp = ADC_INP_SINGLE,
                       .gpios = {PA1},
                       .smp = {ADC_SMP_640_5_CLK},
                       .in = {ADC_CH_PA1},
                       .inlen = 1,
                       .irq_priority = 5};
    hal_adc_init(&conf);

    uint16_t value;
    adc_handle_t handle = {
    .buf = &value, .inseq = {ADC_CH_PA1}, .inseqlen = 1, .callback = adc_callback};

    while (1)
    {
        hal_adc_convert(&handle);

        uint32_t event_bit;
        bw_status_t status = rtos_event_wait(&adc_event, EVENT_LDR_SUCCESS | EVENT_LDR_FAILURE,
                                             &event_bit, true, false, 2000);
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

        rtos_task_delay(100);
    }
}








#include "hal/i2c/i2c.h"
#include "hal/i2c/i2c_bus.h"
#include "hygro.h"
#include "hygro_regs.h"
#include "rtos/sync/event.h"
#include "rtos/task/task.h"
#include "utils/status.h"
#include <stdint.h>

#define HYGRO_ADDR 0x44
#define EVENT_OK BIT(0)
#define EVENT_NACK BIT(1)
#define EVENT_ERR BIT(2)

static uint8_t g_trnf_buf[5];
static i2c_handle_t g_i2c_h;
static event_t g_event;

static void on_i2c_callback(bw_status_t status, void *user_data)
{
    (void)user_data;
    if (status == STATUS_OK)
    {
        rtos_event_set(&g_event, EVENT_OK);
    }
    else if (status == STATUS_I2C_NACKF)
    {
        rtos_event_set(&g_event, EVENT_NACK);
    }
    else // i2c err or dma error
    {
        rtos_event_set(&g_event, EVENT_ERR);
    }
}

static bw_status_t send_cmd(uint8_t msb, uint8_t lsb)
{
    g_trnf_buf[0] = msb;
    g_trnf_buf[1] = lsb;
    g_i2c_h.len = 2; // Always 2 byte commands

    hal_i2c_bus_lock(g_i2c_h.perip);
    hal_i2c_transmit(&g_i2c_h);

    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_OK | EVENT_NACK | EVENT_ERR, &event_bit, true, false, 1000);
    hal_i2c_bus_unlock(g_i2c_h.perip);

    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_NACK | EVENT_ERR)))
    {
        BW_LOG("I2C Write Fail: %s\n", (status == STATUS_TIMEOUT) ? "TIMEOUT" : (event_bit & EVENT_NACK) ? "NACK" : "ERR");
        return STATUS_ERR;
    }

    return STATUS_OK;
}

static bw_status_t recieve_data(uint8_t len)
{
    g_i2c_h.len = len;

    hal_i2c_bus_lock(g_i2c_h.perip);
    hal_i2c_receive(&g_i2c_h);

    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_OK | EVENT_NACK | EVENT_ERR, &event_bit, true, false, 1000);
    hal_i2c_bus_unlock(g_i2c_h.perip);

    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_NACK | EVENT_ERR)))
    {
        BW_LOG("I2C Recieve Fail: %s\n", (status == STATUS_TIMEOUT) ? "TIMEOUT" : (event_bit & EVENT_NACK) ? "NACK" : "ERR");
        return STATUS_ERR;
    }

    return STATUS_OK;
}

void hygro_init()
{
    rtos_event_init(&g_event);

    // tPU timeout to enter idle state
    rtos_task_delay(1);

    // Initialize i2c handle
    g_i2c_h.addr = HYGRO_ADDR;
    g_i2c_h.buf = g_trnf_buf;
    g_i2c_h.repeat = false;
    g_i2c_h.user_data = NULL;
    g_i2c_h.callback = on_i2c_callback;
}

i2c_handle_t *hygro_get_i2c_handle()
{
    return &g_i2c_h;
}

bw_status_t hygro_read(hygro_repeatability_t repeatability, uint16_t *rhx100, int *tempx100)
{
    bw_status_t status;
    uint8_t delay;

    switch (repeatability)
    {
    case HYGRO_REPEATABILITY_HIGH:
        status = send_cmd(HYGRO_START_MEAS_HRS_MSB, HYGRO_START_MEAS_HRS_LSB);
        delay = HYGRO_REPEATABILITY_HIGH_TMS;
        break;

    case HYGRO_REPEATABILITY_MEDIUM:
        status = send_cmd(HYGRO_START_MEAS_MRS_MSB, HYGRO_START_MEAS_MRS_LSB);
        delay = HYGRO_REPEATABILITY_MEDIUM_TMS;
        break;

    case HYGRO_REPEATABILITY_LOW:
        status = send_cmd(HYGRO_START_MEAS_LRS_MSB, HYGRO_START_MEAS_LRS_LSB);
        delay = HYGRO_REPEATABILITY_LOW_TMS;
        break;

    default:
        return STATUS_ERR;
    }

    if (status == STATUS_ERR)
    {
        return status;
    }
    rtos_task_delay(delay);
    status = recieve_data(5);
    if (status != STATUS_OK)
    {
        return status;
    }

    *rhx100 = (10000 * (((uint16_t)g_trnf_buf[3] << 8) | g_trnf_buf[4])) / UINT16_MAX;
    *tempx100 = -4500 + (17500 * (((uint16_t)g_trnf_buf[0] << 8) | g_trnf_buf[1])) / UINT16_MAX;

    return status;
}

bw_status_t hygro_soft_reset()
{
    bw_status_t status = send_cmd(HYGRO_SOFT_RESET_MSB, HYGRO_SOFT_RESET_LSB);
    return status;
}

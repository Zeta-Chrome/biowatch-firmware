#include "biowatch/bsp.h"
#include "drivers/exti/exti.h"
#include "drivers/gpio/gpio.h"
#include "drivers/i2c/i2c.h"
#include "drivers/i2c/i2c_bus.h"
#include "kernel/sync/event.h"
#include "lib/status.h"
#include "lib/utils.h"
#include "oxim.h"
#include "oxim_regs.h"

#define OXIM_ADDR 0x57
#define EVENT_INT BIT(0)
#define EVENT_OK BIT(1)
#define EVENT_NACK BIT(2)
#define EVENT_ERR BIT(3)
#define LED_PA_RED 0x96
#define LED_PA_IR 0x32

typedef struct
{
    oxim_callback_t callback;
    oxim_smp_avg_t smp_avg;
    oxim_smp_rate_t smp_rate;
    uint8_t sample_size;
} oxim_conf_t;

static event_t g_event;
static exti_handle_t g_exti_h;
static i2c_handle_t g_i2c_h;
static uint8_t g_tx_buf[12];
static uint8_t g_rx_buf[8];
static oxim_conf_t g_conf;

static void on_interrupt(void *user_data)
{
    (void)user_data;
    g_conf.callback();
}

static void on_i2c_callback(bw_status_t status, void *user_data)
{
    if (status == STATUS_OK)
    {
        kernel_event_set(&g_event, EVENT_OK);
    }
    else if (status == STATUS_I2C_REPEATED_START)
    {
        void (*func_callback)(void) = user_data;
        func_callback();
    }
    else if (status == STATUS_I2C_NACKF)
    {
        BW_LOG("NACK after %d bytes remaining\n", g_i2c_h.remaining);
        kernel_event_set(&g_event, EVENT_NACK);
    }
    else // i2c err or dma error
    {
        kernel_event_set(&g_event, EVENT_ERR);
    }
}

static bw_status_t transmit_and_wait(uint8_t *buf, uint8_t len, bool repeat, uint32_t timeout, void *user_data)
{
    uint32_t event_bit;

    g_i2c_h.buf = buf;
    g_i2c_h.len = len;
    g_i2c_h.repeat = repeat;
    g_i2c_h.user_data = user_data;

    i2c_bus_lock(g_i2c_h.perip);
    i2c_transmit(&g_i2c_h);
    bw_status_t status = kernel_event_wait(&g_event, EVENT_OK | EVENT_NACK | EVENT_ERR, &event_bit, true, false, timeout);
    i2c_bus_unlock(g_i2c_h.perip);

    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_NACK | EVENT_ERR)))
    {
        BW_LOG("I2C Write Fail: %s\n", (status == STATUS_TIMEOUT) ? "TIMEOUT" : (event_bit & EVENT_NACK) ? "NACK" : "ERR");
        i2c_bus_reset(&g_i2c_h);
        return STATUS_ERR;
    }
    return STATUS_OK;
}

static void read_int_status()
{
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_rx_buf;
    g_i2c_h.len = 2;
    g_i2c_h.repeat = false;
}

bw_status_t oxim_init(oxim_smp_avg_t smp_avg, oxim_smp_rate_t smp_rate, oxim_callback_t callback)
{
    g_conf.callback = callback;
    g_conf.smp_avg = smp_avg;
    g_conf.smp_rate = smp_rate;

    // Event init
    kernel_event_init(&g_event);

    // Configure handle
    g_i2c_h.addr = OXIM_ADDR;
    g_i2c_h.callback = on_i2c_callback;

    bw_status_t status;

    // Check if powered on
    if (gpio_read_level(PL_OXIM_EXTI) != 0)
    {
        g_tx_buf[0] = OXIM_MODE_CONF;
        g_tx_buf[1] = OXIM_MODE_CONF_RST_Msk;
        if (transmit_and_wait(g_tx_buf, 2, false, 100, NULL) != STATUS_OK)
        {
            status = kernel_event_wait(&g_event, EVENT_INT, NULL, true, true, 2000);
            if (status != STATUS_OK)
            {
                BW_LOG("Never powered on\n");
                return status;
            }
        }
    }

    g_tx_buf[0] = OXIM_MODE_CONF;
    g_tx_buf[1] = OXIM_MODE_CONF_RST_Msk;
    status = transmit_and_wait(g_tx_buf, 2, false, 100, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to reset!\n");
        return status;
    }

    status = oxim_reconfigure();
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to configure oximeter\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t oxim_reconfigure()
{
    g_tx_buf[0] = OXIM_INT_EN1;
    g_tx_buf[1] = OXIM_INT_EN1_ALC_OVF | OXIM_INT_EN1_PPG_RDY;
    g_tx_buf[2] = OXIM_INT_EN2_DIE_TEMP_RDY;
    bw_status_t status = transmit_and_wait(g_tx_buf, 3, false, 100, NULL);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to enable interrupts\n");
        return status;
    }

    // Write FIFO and SPO2 Configuation registers
    g_tx_buf[0] = OXIM_FIFO_CONF;
    g_tx_buf[1] = g_conf.smp_avg << OXIM_FIFO_CONF_SMP_AVE_Pos;
    g_tx_buf[2] = OXIM_MODE_CONF_SHDN_Msk;
    g_tx_buf[3] = 0x1 << OXIM_SPO2_CONF_ADC_RGE_Pos | g_conf.smp_rate << OXIM_SPO2_CONF_SR_Pos
                  | 0x3 << OXIM_SPO2_CONF_LED_PW_Pos;
    status = transmit_and_wait(g_tx_buf, 4, false, 100, NULL);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to write fifo and mode configuration\n");
        return status;
    }

    // Write LED PAs
    g_tx_buf[0] = OXIM_LED1_PA;
    g_tx_buf[1] = LED_PA_RED;
    g_tx_buf[2] = LED_PA_IR;
    status = transmit_and_wait(g_tx_buf, 3, false, 100, NULL);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to set LED currents\n");
        return status;
    }

    return STATUS_OK;
}

i2c_handle_t *oxim_get_i2c_handle()
{
    return &g_i2c_h;
}

exti_handle_t *oxim_get_exti_handle(exti_callback_t *callback)
{
    *callback = on_interrupt;
    return &g_exti_h;
}

static void read_fifo()
{
    // Reset the read, write and ovf counters
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_rx_buf;
    g_i2c_h.len = g_conf.sample_size;
    g_i2c_h.repeat = false;
}

static void read_temp_regs()
{
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_rx_buf;
    g_i2c_h.len = 2;
    g_i2c_h.repeat = false;
}

bw_status_t oxim_read_int_status(uint8_t int_status[2])
{
    g_tx_buf[0] = OXIM_INT_ST1;
    bw_status_t status = transmit_and_wait(g_tx_buf, 1, true, 100, read_int_status);
    if (status != STATUS_OK)
    {
        BW_LOG("Interrupt status read failed \n");
        return status;
    }

    int_status[0] = g_rx_buf[0];
    int_status[1] = g_rx_buf[1];

    return STATUS_OK;
}

bw_status_t oxim_start_temp_conversion()
{
    g_tx_buf[0] = OXIM_DIE_TEMP_EN;
    g_tx_buf[1] = 1;
    bw_status_t status = transmit_and_wait(g_tx_buf, 2, false, 100, read_temp_regs);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to set temperature enable\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t oxim_read_temp(int *temp_milli_c)
{
    bw_status_t status;

    g_tx_buf[0] = OXIM_DIE_TEMP_INT;
    status = transmit_and_wait(g_tx_buf, 1, true, 100, read_temp_regs);
    if (status == STATUS_ERR)
    {
        return status;
    }

    *temp_milli_c = (int)(int8_t)g_rx_buf[0] * 1000 + ((g_rx_buf[1] & 0xF) * 625) / 10;

    return STATUS_OK;
}

bw_status_t oxim_start_mode(oxim_mode_t mode)
{
    g_conf.sample_size = mode == OXIM_MODE_SPO2 ? 6 : 3;

    // Clear WR, OVR and RD to 0
    g_tx_buf[0] = OXIM_FIFO_WR_PTR;
    g_tx_buf[1] = 0;
    g_tx_buf[2] = 0;
    g_tx_buf[3] = 0;
    bw_status_t status = transmit_and_wait(g_tx_buf, 4, false, 100, NULL);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to reset pointers\n");
        return status;
    }

    g_tx_buf[0] = OXIM_MODE_CONF;
    g_tx_buf[1] = mode << OXIM_MODE_CONF_MODE_Pos;
    status = transmit_and_wait(g_tx_buf, 2, false, 100, NULL);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to configure mode\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t oxim_read_sample(uint32_t *red_sample, uint32_t *ir_sample)
{
    g_tx_buf[0] = OXIM_FIFO_DATA;
    bw_status_t status = transmit_and_wait(g_tx_buf, 1, true, 100, read_fifo);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to read fifo\n");
        return status;
    }

    if (red_sample)
    {
        *red_sample = 0x3FFFF & (((uint32_t)g_rx_buf[0] << 16) | ((uint32_t)g_rx_buf[1] << 8) | (uint32_t)g_rx_buf[2]);
    }

    if (ir_sample)
    {
        *ir_sample = 0x3FFFF & (((uint32_t)g_rx_buf[3] << 16) | ((uint32_t)g_rx_buf[4] << 8) | (uint32_t)g_rx_buf[5]);
    }

    return STATUS_OK;
}

bw_status_t oxim_shutdown()
{
    g_tx_buf[0] = OXIM_MODE_CONF;
    g_tx_buf[1] = OXIM_MODE_CONF_SHDN_Msk;
    bw_status_t status = transmit_and_wait(g_tx_buf, 2, false, 100, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to shutdown\n");
        return status;
    }

    return STATUS_OK;
}

#include "oxim.h"
#include "hal/exti/exti.h"
#include "hal/gpio/gpio.h"
#include "hal/i2c/i2c.h"
#include "oxim/oxim_regs.h"
#include "bsp.h"
#include "rtos/sync/event.h"
#include "utils/status.h"
#include "utils/utils.h"

#define OXIM_ADDR 0x57
#define EVENT_INT BIT(0)
#define EVENT_OK BIT(1)
#define EVENT_NACK BIT(2)
#define EVENT_ERR BIT(3)
#define A_FULL 0xF

typedef struct
{
    oxim_smp_avg_t smp_avg;
    oxim_smp_rate_t smp_rate;
    oxim_smp_t samples;
} oxim_conf_t;

static oxim_conf_t g_conf;
static event_t g_event;
static exti_handle_t g_exti_h;
static i2c_handle_t g_i2c_h;
static oxim_state_t g_state = OXIM_STATE_UNINTILAIZED;
static uint8_t g_tx_buf[12];
static uint8_t g_rx_buf[8];
static oxim_data_t g_data;

static void on_interrupt(void *user_data)
{
    BW_LOG("INT fired\n");
    rtos_event_set(&g_event, EVENT_INT);
}

static void on_i2c_callback(bw_status_t status, void *user_data)
{
    if (status == STATUS_OK)
    {
        rtos_event_set(&g_event, EVENT_OK);
    }
    else if (status == STATUS_I2C_REPEATED_START)
    {
        BW_LOG("Repeated Start\n");
        void (*func_callback)(void) = user_data;
        func_callback();
    }
    else if (status == STATUS_I2C_NACKF)
    {
        BW_LOG("NACK after %d bytes remaining\n", g_i2c_h.remaining);
        rtos_event_set(&g_event, EVENT_NACK);
    }
    else  // i2c err or dma error
    {
        rtos_event_set(&g_event, EVENT_ERR);
    }
}

static bw_status_t transmit_and_wait(uint8_t *buf, uint8_t len, bool repeat, void *user_data)
{
    g_i2c_h.buf = buf;
    g_i2c_h.len = len;
    g_i2c_h.repeat = repeat;
    g_i2c_h.user_data = user_data;

    hal_i2c_transmit_dma(&g_i2c_h);

    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_OK | EVENT_NACK | EVENT_ERR,
                                         &event_bit, true, false, 1000);

    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_NACK | EVENT_ERR)))
    {
        BW_LOG("I2C Write Fail: %s\n", (status == STATUS_TIMEOUT)   ? "TIMEOUT"
                                         : (event_bit & EVENT_NACK) ? "NACK"
                                                                    : "ERR");
        return STATUS_ERR;
    }
    return STATUS_OK;
}

static void oxim_configure()
{
    g_tx_buf[0] = OXIM_INT_EN1;
    g_tx_buf[1] = OXIM_INT_EN1_ALC_OVF | OXIM_INT_EN1_A_FULL;
    bw_status_t status = transmit_and_wait(g_tx_buf, 2, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed to enable interrupts\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        return;
    }
    BW_LOG("Oximeter enabled interrupts\n");

    // Write FIFO and SPO2 Configuation registers
    g_tx_buf[0] = OXIM_FIFO_CONF;
    g_tx_buf[1] = g_conf.smp_avg << OXIM_FIFO_CONF_SMP_AVE_Pos | A_FULL << OXIM_FIFO_CONF_A_FULL_Pos;
    g_tx_buf[2] = OXIM_MODE_CONF_SHDN_Msk;
    g_tx_buf[3] = 0x3 << OXIM_SPO2_CONF_ADC_RGE_Pos | g_conf.smp_rate << OXIM_SPO2_CONF_SR_Pos | 0x3;
    status = transmit_and_wait(g_tx_buf, 4, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed to write fifo and mode configuration\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        return;
    }
    BW_LOG("Oximeter set fifo and mode configurations\n");

    // Write LED PAs
    g_tx_buf[0] = OXIM_LED1_PA;
    g_tx_buf[1] = 0x02;
    g_tx_buf[2] = 0x02;
    status = transmit_and_wait(g_tx_buf, 3, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed to set LED currents\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        return;
    }
    BW_LOG("Oximeter set LED currents\n");
}

static void read_int_status()
{
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_rx_buf;
    g_i2c_h.len = 2;
    g_i2c_h.repeat = false;
}

void oxim_init(oxim_smp_avg_t smp_avg, oxim_smp_rate_t smp_rate, oxim_smp_t samples)
{
    g_state = OXIM_STATE_UNINTILAIZED;

    // Init conf
    g_conf.smp_avg = smp_avg;
    g_conf.smp_rate = smp_rate;
    g_conf.samples = samples;

    // Event init
    rtos_event_init(&g_event);

    // Configure handle
    g_i2c_h.addr = OXIM_ADDR;
    g_i2c_h.callback = on_i2c_callback;

    // Check if powered on
    if (hal_gpio_read_level(PL_OXIM_EXTI) != 0)
    {
        g_tx_buf[0] = OXIM_MODE_CONF;
        g_tx_buf[1] = OXIM_MODE_CONF_RST_Msk;
        bw_status_t status = transmit_and_wait(g_tx_buf, 2, false, NULL);
        if (status != STATUS_OK)  // If transmission failed, the oximeter is off
        {
            status = rtos_event_wait(&g_event, EVENT_INT, NULL, true, true, 2000);
            if (status == STATUS_TIMEOUT)
            {
                BW_LOG("Oximeter never powered on\n");
                return;
            }
        }
    }
    else
    {
        // Clear all pending interrupts
        g_tx_buf[0] = OXIM_INT_ST1;
        bw_status_t status = transmit_and_wait(g_tx_buf, 1, true, read_int_status);
        if (status == STATUS_ERR)
        {
            BW_LOG("Oximeter failed to clear interrupts!\n");
            g_state = OXIM_STATE_I2C_ERR;
            hal_i2c_reset_dma(&g_i2c_h);
            return;
        }
    }
    BW_LOG("Oximeter powered on\n");

    oxim_configure();

    g_state = OXIM_STATE_READY;
    BW_LOG("Oximeter init succeeded\n");
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

static bw_status_t oxim_handle_por()
{
    bw_status_t status = rtos_event_wait(&g_event, EVENT_INT, NULL, true, true, 0);
    if (status == STATUS_TIMEOUT)
    {
        return STATUS_OK;
    }

    // Read interrupt status
    g_tx_buf[0] = OXIM_INT_ST1;
    status = transmit_and_wait(g_tx_buf, 1, true, read_int_status);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed to read interrupt status!\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        return status;
    }

    if (g_rx_buf[0] & OXIM_INT_ST1_PPG_RDY)
    {
        oxim_configure();
    }

    return STATUS_OK;
}

static void read_status_and_ptrs()
{
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_rx_buf;
    g_i2c_h.len = OXIM_FIFO_RD_PTR + 1;
    g_i2c_h.repeat = false;
}

static void read_fifo()
{
    uint8_t count = (32 + g_rx_buf[OXIM_FIFO_WR_PTR] - g_rx_buf[OXIM_FIFO_RD_PTR]) % 32;
    BW_LOG("Expected Data count: %d\n", count);
    g_data.count += count;

    // Reset the read, write and ovf counters
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = (uint8_t *)g_data.data;
    g_i2c_h.len = count * g_data.sample_size;
    g_i2c_h.repeat = false;
}

bw_status_t oxim_read(oxim_mode_t mode)
{
    if (g_state <= OXIM_STATE_I2C_ERR)
    {
        g_state = OXIM_STATE_I2C_ERR;
        return STATUS_ERR;
    }

    bw_status_t status = oxim_handle_por();
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to handle power on reset\n");
        oxim_shutdown();
        return status;
    }

    // Clear WR, OVR and RD to 0
    g_tx_buf[0] = OXIM_FIFO_WR_PTR;
    g_tx_buf[1] = 0;
    g_tx_buf[2] = 0;
    g_tx_buf[3] = 0;
    status = transmit_and_wait(g_tx_buf, 4, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed in writing WR, OVR and RD\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        return status;
    }
    BW_LOG("Oximeter writing WR, OVR and RD succeeded\n");

    // Configure Mode
    g_tx_buf[0] = OXIM_MODE_CONF;
    g_tx_buf[1] = mode << OXIM_MODE_CONF_MODE_Pos;
    status = transmit_and_wait(g_tx_buf, 2, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed to write fifo and mode configuration\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        oxim_shutdown();
        return status;
    }
    BW_LOG("Oximeter mode configuration succeeded\n");

    // Initialize data struct
    g_data.sample_size = mode == OXIM_MODE_SPO2 ? 6 : 3;
    g_data.count = 0;

    while (g_data.count < g_conf.samples)
    {
        // Wait for an interrupt
        status = rtos_event_wait(&g_event, EVENT_INT, NULL, true, true, 20000);
        if (status == STATUS_TIMEOUT)
        {
            BW_LOG("Oximeter never interrupted!\n");
            oxim_shutdown();
            return status;
        }

        // Read the status registers and pointers to know which interrupt
        g_tx_buf[0] = OXIM_INT_ST1;
        status = transmit_and_wait(g_tx_buf, 1, true, read_status_and_ptrs);
        if (status == STATUS_ERR)
        {
            BW_LOG("Oximeter fifo read failed \n");
            g_state = OXIM_STATE_I2C_ERR;
            hal_i2c_reset_dma(&g_i2c_h);
            oxim_shutdown();
            return status;
        }

        if (g_rx_buf[0] & OXIM_INT_ST1_PWR_RDY)
        {
            BW_LOG("Oximeter triggered power on reset\n");
            oxim_configure();
            g_state = OXIM_STATE_POR;
            oxim_shutdown();
            return STATUS_ERR;
        }
        else if (g_rx_buf[0] & OXIM_INT_ST1_ALC_OVF)
        {
            BW_LOG("Oximeter triggered ambient light cancellation overflow\n");
            g_state = OXIM_STATE_ALC_OVF;
            oxim_shutdown();
            return STATUS_ERR;
        }
        else if (!(g_rx_buf[0] & OXIM_INT_ST1_A_FULL))
        {
            BW_LOG("Oximeter triggered unknown interrupt\nST1: %x, ST2: %x\n", g_rx_buf[0], g_rx_buf[1]);
            oxim_shutdown();
            return STATUS_ERR;
        }

        BW_LOG("Oximeter triggered interrupt\nST1: %x, ST2: %x\n", g_rx_buf[0], g_rx_buf[1]);

        // Read FIFO data
        g_tx_buf[0] = OXIM_FIFO_DATA;  // start reading from INT status reg 1
        status = transmit_and_wait(g_tx_buf, 1, true, read_fifo);
        if (status == STATUS_ERR)
        {
            BW_LOG("Oximeter fifo read failed \n");
            g_state = OXIM_STATE_I2C_ERR;
            hal_i2c_reset_dma(&g_i2c_h);
            oxim_shutdown();
            return status;
        }

        BW_LOG("Data Count: %d\n", g_data.count);
    }

    g_state = OXIM_STATE_READY;
    oxim_shutdown();

    return STATUS_OK;
}

void oxim_shutdown()
{
    g_tx_buf[0] = OXIM_MODE_CONF;
    g_tx_buf[1] = OXIM_MODE_CONF_SHDN_Msk;
    bw_status_t status = transmit_and_wait(g_tx_buf, 2, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Oximeter failed to shutdown\n");
        g_state = OXIM_STATE_I2C_ERR;
        hal_i2c_reset_dma(&g_i2c_h);
        return;
    }
}

oxim_state_t get_oxim_state()
{
    return g_state;
}

oxim_data_t *get_oxim_data()
{
    return &g_data;
}

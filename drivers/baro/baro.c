#include "baro.h"
#include "baro/baro_regs.h"
#include "hal/i2c/i2c.h"
#include "rtos/sync/event.h"
#include "utils/logger.h"
#include "utils/status.h"
#include "utils/utils.h"

#define BARO_ADDR 0x76
#define EVENT_OK BIT(0)
#define EVENT_NACK BIT(1)
#define EVENT_ERR BIT(2)

static uint8_t g_tx_buf[4];
static uint8_t g_rx_buf[8];
static uint8_t g_calib_buf[32];
static i2c_handle_t g_i2c_h;
static event_t g_event;
static baro_state_t g_state = BARO_STATE_UNINTILAIZED;

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

    hal_i2c_transmit(&g_i2c_h);

    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_OK | EVENT_NACK | EVENT_ERR, &event_bit, true,
                                         false, 1000);

    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_NACK | EVENT_ERR)))
    {
        BW_LOG("I2C Write Fail: %s\n", (status == STATUS_TIMEOUT) ? "TIMEOUT"
                                       : (event_bit & EVENT_NACK) ? "NACK"
                                                                  : "ERR");
        return STATUS_ERR;
    }
    return STATUS_OK;
}

void baro_init(baro_mode_t mode)
{
    g_state = BARO_STATE_UNINTILAIZED;

    g_i2c_h.addr = BARO_ADDR;
    g_i2c_h.callback = on_i2c_callback;

    // Event init
    rtos_event_init(&g_event);

    // Reconfigure barometer
    baro_reconfigure(mode);
}

void baro_reconfigure(baro_mode_t mode)
{
    // Configure ctrl_meas and config
    g_tx_buf[0] = BARO_CTRL_MEAS;

    switch (mode)
    {
    case BARO_MODE_HANDHELD_LP:
        g_tx_buf[1] = 0x5 << BARO_CTRL_MEAS_OSRSP_Pos | 0x2 << BARO_CTRL_MEAS_OSRST_Pos | 0x3;
        g_tx_buf[2] = 0x1 << BARO_CONF_TSB_Pos | 0x2 << BARO_CONF_FILTER_Pos;
        break;
    case BARO_MODE_HANDHELD_DY:
        g_tx_buf[1] = 0x3 << BARO_CTRL_MEAS_OSRSP_Pos | 0x1 << BARO_CTRL_MEAS_OSRST_Pos | 0x3;
        g_tx_buf[2] = 0x0 << BARO_CONF_TSB_Pos | 0x4 << BARO_CONF_FILTER_Pos;
        break;
    case BARO_MODE_WEATHER_MON:
        g_tx_buf[1] = 0x1 << BARO_CTRL_MEAS_OSRSP_Pos | 0x1 << BARO_CTRL_MEAS_OSRST_Pos | 0x2;
        g_tx_buf[2] = 0x0 << BARO_CONF_FILTER_Pos;
        break;
    case BARO_MODE_FLOOR_CHANGE:
        g_tx_buf[1] = 0x3 << BARO_CTRL_MEAS_OSRSP_Pos | 0x1 << BARO_CTRL_MEAS_OSRST_Pos | 0x3;
        g_tx_buf[2] = 0x2 << BARO_CONF_TSB_Pos | 0x2 << BARO_CONF_FILTER_Pos;
        break;
    case BARO_MODE_DROP_DETECT:
        g_tx_buf[1] = 0x2 << BARO_CTRL_MEAS_OSRSP_Pos | 0x1 << BARO_CTRL_MEAS_OSRST_Pos | 0x3;
        g_tx_buf[2] = 0x0 << BARO_CONF_TSB_Pos | 0x0 << BARO_CONF_FILTER_Pos;
        break;
    case BARO_MODE_INDOOR_NAV:
        g_tx_buf[1] = 0x5 << BARO_CTRL_MEAS_OSRSP_Pos | 0x2 << BARO_CTRL_MEAS_OSRST_Pos | 0x3;
        g_tx_buf[2] = 0x0 << BARO_CONF_TSB_Pos | 0x4 << BARO_CONF_FILTER_Pos;
        break;
    }
    bw_status_t status = transmit_and_wait(g_tx_buf, 3, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Barometer failed to configure\n");
        g_state = BARO_STATE_I2C_ERR;
        hal_i2c_reset(&g_i2c_h);
        return;
    }
    BW_LOG("Barometer is configured\n");
    g_state = BARO_STATE_READY;
}

i2c_handle_t *baro_get_i2c_handle()
{
    return &g_i2c_h;
}

static void read_meas()
{
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_rx_buf;
    g_i2c_h.len = 6;
    g_i2c_h.repeat = false;
}

static void read_calib()
{
    g_i2c_h.type = I2C_TYPE_RX;
    g_i2c_h.buf = g_calib_buf;
    g_i2c_h.len = 24;
    g_i2c_h.repeat = false;
}

static uint32_t baro_compensate_temp(int32_t *t_fine)
{
    uint32_t adc_temp = ((uint32_t)g_rx_buf[3] << 12) | ((uint32_t)g_rx_buf[4] << 4) |
                        (g_rx_buf[5] >> 4);

    uint16_t dig_T1 = (uint16_t)(g_calib_buf[1] << 8 | g_calib_buf[0]);
    int16_t dig_T2 = (int16_t)(g_calib_buf[3] << 8 | g_calib_buf[2]);
    int16_t dig_T3 = (int16_t)(g_calib_buf[5] << 8 | g_calib_buf[4]);

    uint32_t var1 = ((((adc_temp >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    uint32_t var2 = (((((adc_temp >> 4) - ((int32_t)dig_T1)) * ((adc_temp >> 4) - ((int32_t)dig_T1))) >>
                      12) *
                     ((int32_t)dig_T3)) >>
                    14;
    *t_fine = var1 + var2;
    uint32_t temp = (*t_fine * 5 + 128) >> 8;
    return temp;
}

static uint32_t baro_compensate_press(int32_t t_fine)
{
    uint32_t adc_press = ((uint32_t)g_rx_buf[0] << 12) | ((uint32_t)g_rx_buf[1] << 4) |
                         (g_rx_buf[2] >> 4);

    uint16_t dig_P1 = (uint16_t)(g_calib_buf[7] << 8 | g_calib_buf[6]);
    int16_t dig_P2 = (int16_t)(g_calib_buf[9] << 8 | g_calib_buf[8]);
    int16_t dig_P3 = (int16_t)(g_calib_buf[11] << 8 | g_calib_buf[10]);
    int16_t dig_P4 = (int16_t)(g_calib_buf[13] << 8 | g_calib_buf[12]);
    int16_t dig_P5 = (int16_t)(g_calib_buf[15] << 8 | g_calib_buf[14]);
    int16_t dig_P6 = (int16_t)(g_calib_buf[17] << 8 | g_calib_buf[16]);
    int16_t dig_P7 = (int16_t)(g_calib_buf[19] << 8 | g_calib_buf[18]);
    int16_t dig_P8 = (int16_t)(g_calib_buf[21] << 8 | g_calib_buf[20]);
    int16_t dig_P9 = (int16_t)(g_calib_buf[23] << 8 | g_calib_buf[22]);

    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
    if (var1 == 0)
    {
        return 0;  // avoid exception caused by division by zero
    }

    int64_t press = 1048576 - adc_press;
    press = (((press << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (press >> 13) * (press >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * press) >> 19;
    press = ((press + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (uint32_t)press;
}

bw_status_t baro_read(uint32_t *press_hpa, uint32_t *temp_hc)
{
    g_tx_buf[0] = BARO_PRESS;
    bw_status_t status = transmit_and_wait(g_tx_buf, 1, true, read_meas);
    if (status == STATUS_ERR)
    {
        BW_LOG("Barometer failed to read measurements\n");
        g_state = BARO_STATE_I2C_ERR;
        hal_i2c_reset(&g_i2c_h);
        return STATUS_ERR;
    }
    g_state = BARO_STATE_READY;

    g_tx_buf[0] = BARO_CALIB;
    status = transmit_and_wait(g_tx_buf, 1, true, read_calib);
    if (status == STATUS_ERR)
    {
        BW_LOG("Barometer failed to read calibration data\n");
        g_state = BARO_STATE_I2C_ERR;
        hal_i2c_reset(&g_i2c_h);
        return STATUS_ERR;
    }
    g_state = BARO_STATE_READY;

    int32_t t_fine;
    *temp_hc = baro_compensate_temp(&t_fine);
    *press_hpa = (baro_compensate_press(t_fine) * 100) / 256;

    return STATUS_OK;
}

void baro_sleep()
{
    g_tx_buf[0] = BARO_CTRL_MEAS;
    g_tx_buf[1] = 0;
    bw_status_t status = transmit_and_wait(g_tx_buf, 2, false, NULL);
    if (status == STATUS_ERR)
    {
        BW_LOG("Barometer failed to sleep\n");
        g_state = BARO_STATE_I2C_ERR;
        hal_i2c_reset(&g_i2c_h);
    }
    g_state = BARO_STATE_READY;
}

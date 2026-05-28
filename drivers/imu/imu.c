#include "imu.h"
#include "hal/exti/exti.h"
#include "hal/gpio/gpio.h"
#include "imu/imu_regs.h"
#include "bsp.h"
#include "hal/spi/spi.h"
#include "rtos/sync/event.h"
#include "rtos/task/task.h"
#include "utils/status.h"
#include "utils/utils.h"
#include <string.h>

#define EVENT_OK BIT(0)
#define EVENT_MODF BIT(1)
#define EVENT_OVR BIT(2)
#define EVENT_ERR BIT(3)

static task_handle_t g_task_handle;
static exti_handle_t g_exti1_h;
static spi_handle_t g_spi_h;
static event_t g_event;
static uint8_t g_rx_buf[8];
static uint8_t g_tx_buf[8];

static void on_interrupt1(void *user_data)
{
    (void)user_data;
    BW_LOG("Triggered Interrupt 1\n");
    rtos_task_notify(g_task_handle, 0, NOTIFY_ACTION_NONE);
}

static void on_spi_callback(bw_status_t status, void *user_data)
{
    (void)user_data;
    if (status == STATUS_OK)
    {
        rtos_event_set_from_isr(&g_event, EVENT_OK);
    }
    else if (status == STATUS_SPI_MODF)
    {
        rtos_event_set_from_isr(&g_event, EVENT_MODF);
    }
    else if (status == STATUS_SPI_OVR)
    {
        rtos_event_set_from_isr(&g_event, EVENT_OVR);
    }
    else
    {
        rtos_event_set_from_isr(&g_event, EVENT_ERR);
    }
}

static bw_status_t transact_and_wait(uint16_t len)
{
    g_spi_h.len = len;
    hal_gpio_set_level(PL_IMU_CS, 0);
    hal_spi_transact_dma(&g_spi_h);

    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_OK | EVENT_MODF | EVENT_OVR | EVENT_ERR,
                                         &event_bit, true, false, 1000);
    hal_gpio_set_level(PL_IMU_CS, 1);
    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_MODF | EVENT_OVR | EVENT_ERR)))
    {
        BW_LOG("SPI Fail: %s\n", (status == STATUS_TIMEOUT)  ? "TIMEOUT"
                                 : (event_bit & EVENT_MODF)  ? "MODF"
                                   : (event_bit & EVENT_OVR) ? "OVR"
                                                             : "ERR");
        return STATUS_ERR;
    }

    return STATUS_OK;
}

static uint8_t encode_no_motion_dur(uint8_t duration_sec)
{
    if (duration_sec <= 20)
    {
        uint8_t val = (uint8_t)(duration_sec * 100 / 128);
        if (val > 0)
            val -= 1;
        val &= 0x0F;
        return val;
    }
    else if (duration_sec <= 102)
    {
        uint8_t val = (uint8_t)(duration_sec * 100 / 512);
        if (val >= 5)
            val -= 5;
        val &= 0x0F;
        return (0x01 << 4) | val;
    }
    else
    {
        uint8_t val = (uint8_t)(duration_sec * 100 / 1024);
        if (val >= 11)
            val -= 11;
        val &= 0x1F;
        return (0x01 << 5) | val;
    }
}

void imu_init(task_handle_t handle, imu_step_mode_t st_mode, imu_tap_t tap_mode, imu_dtap_dur_t dtap_dur,
              imu_nomo_t nomo_mode, uint16_t nomo_dur_s)
{
    BW_ASSERT(nomo_dur_s >= 2 && nomo_dur_s <= 431,
              "Double tap duration not in valid range (Expected 2s-431s)");

    g_task_handle = handle;

    // Set CS to high to use BMI160 in SPI mode 
    hal_gpio_set_level(PL_IMU_CS, 1);

    // Let IMU power up
    rtos_task_delay(10);

    // Init event
    rtos_event_init(&g_event);

    // Common handle configurations
    g_spi_h.data_sz = 8;
    g_spi_h.rx_buf = g_rx_buf;
    g_spi_h.tx_buf = g_tx_buf;
    g_spi_h.user_data = NULL;
    g_spi_h.callback = on_spi_callback;

    // Dummy byte transaction
    g_tx_buf[0] = IMU_READ | 0x7F;
    bw_status_t status = transact_and_wait(1);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to read Dummy byte\n");
        return;
    }
    rtos_task_delay(10);

    imu_reconfigure(st_mode, tap_mode, dtap_dur, nomo_mode, nomo_dur_s);
}

void imu_reconfigure(imu_step_mode_t st_mode, imu_tap_t tap_mode, imu_dtap_dur_t dtap_dur,
                     imu_nomo_t nomo_mode, uint16_t nomo_dur_s)
{
    // Configure acc with undersampling, 4 sample average and data rate of 200Hz
    g_tx_buf[0] = IMU_WRITE | IMU_ACC_CONF;
    g_tx_buf[1] = IMU_ACC_CONF_US_Msk | 0x0 << IMU_ACC_CONF_BWP_Pos | 0x9 << IMU_ACC_CONF_ODR_Pos;
    g_tx_buf[2] = 0x8 << IMU_ACC_RANGE_Pos;
    bw_status_t status = transact_and_wait(3);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to turn on accelerometer in IMU\n");
        return;
    }
    BW_LOG("Turned on accelerometer in IMU\n");

    // Configure step conf with normal mode
    g_tx_buf[0] = IMU_WRITE | IMU_STEP_CONF;
    switch (st_mode)
    {
    case IMU_STEP_MODE_NORMAL:
        g_tx_buf[1] = 0x15;
        g_tx_buf[2] = IMU_STEP_CNT_EN_Msk | 0x3;
        break;
    case IMU_STEP_MODE_SENSITIVE:
        g_tx_buf[1] = 0x2D;
        g_tx_buf[2] = IMU_STEP_CNT_EN_Msk | 0x0;
        break;
    case IMU_STEP_MODE_ROBUST:
        g_tx_buf[1] = 0x1D;
        g_tx_buf[2] = IMU_STEP_CNT_EN_Msk | 0x7;
        break;
    }
    status = transact_and_wait(3);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to configure step in IMU\n");
        return;
    }
    BW_LOG("Configured step in IMU\n");

    // Configure tap int and nomotion int
    g_tx_buf[0] = IMU_INT_MO0;
    g_tx_buf[1] = encode_no_motion_dur(nomo_dur_s) << IMU_INT_MO0_SN_DUR_Pos;
    g_tx_buf[2] = 0;
    g_tx_buf[3] = nomo_mode << IMU_INT_MO2_SN_TH_Pos;
    g_tx_buf[4] = IMU_INT_MO3_NOMO_SEL_Msk;
    g_tx_buf[5] = (dtap_dur / 50 - 1) << IMU_INT_TAP_DUR_Pos;
    g_tx_buf[6] = tap_mode << IMU_INT_TAP_TH_Pos;
    status = transact_and_wait(7);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to configure tap and nomotion in IMU\n");
        return;
    }
    BW_LOG("Configured tap and nomotion in IMU\n");

    // Map interrupts
    g_tx_buf[0] = IMU_WRITE | IMU_INT1_MAP0;
    g_tx_buf[1] = IMU_INT1_MAP0_STAP_Msk | IMU_INT1_MAP0_DTAP_Msk | IMU_INT1_MAP0_NOMO_Msk;
    status = transact_and_wait(2);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to map interrupts in IMU\n");
        return;
    }
    BW_LOG("Mapped interrupts in IMU\n");

    // Power up accelormeter
    g_tx_buf[0] = IMU_WRITE | IMU_CMD;
    g_tx_buf[1] = IMU_CMD_ACC_LOW_PWR;
    status = transact_and_wait(2);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to turn on accelerometer in IMU\n");
        return;
    }
    rtos_task_delay(5);  // Delay of maximum 3.8ms
    BW_LOG("Turned on accelerometer in IMU\n");

    // Configure interrupts
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN0;
    g_tx_buf[1] = IMU_INT_EN0_STAP_Msk | IMU_INT_EN0_DTAP_Msk;
    g_tx_buf[2] = 0;
    g_tx_buf[3] = IMU_INT_EN2_STEP_Msk | IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk |
                  IMU_INT_EN2_NOMOZ_Msk;
    g_tx_buf[4] = IMU_INT1_OUT_EN_Msk;  // PUSH PULL + ACTIVE LOW
    status = transact_and_wait(5);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to configure interrupts in IMU\n");
        return;
    }
    BW_LOG("Configured interrupts in IMU\n");
}

spi_handle_t* imu_get_spi_handle()
{
    return &g_spi_h;
}

exti_handle_t* imu_get_exti_handle(exti_callback_t *callback)
{
    *callback = on_interrupt1;
    return &g_exti1_h;
}

void imu_read_error()
{
    g_tx_buf[0] = IMU_READ | IMU_ERR;
    bw_status_t status = transact_and_wait(2);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to read error in IMU\n");
        return;
    }

    BW_LOG("Fatal error : %d, Error Code : %d, Dropped cmd error: %d\n", g_rx_buf[1] & 0x1,
           g_rx_buf[1] & 0x1E, g_rx_buf[1] & 0x40);
}

bw_status_t imu_read_int_status(uint8_t int_status[2])
{
    g_tx_buf[0] = IMU_READ | IMU_INT_ST0;
    bw_status_t status = transact_and_wait(5);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to read interrupt status\n");
        return STATUS_ERR;
    }

    int_status[0] = g_rx_buf[1];
    int_status[1] = g_rx_buf[2];
    int_status[2] = g_rx_buf[3];
    int_status[3] = g_rx_buf[4];

    return STATUS_OK;
}

bw_status_t imu_read_step_cnt(uint16_t *step_cnt)
{
    g_tx_buf[0] = IMU_READ | IMU_STEP_CNT;
    bw_status_t status = transact_and_wait(3);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to read step count\n");
        return STATUS_ERR;
    }

    *step_cnt = *(uint16_t*)(&g_rx_buf[1]);
    return STATUS_OK;
}

void imu_sleep()
{
    // Configure interrupts
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN2;
    g_tx_buf[1] = 0;
    bw_status_t status = transact_and_wait(2);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to disable interrupts in IMU\n");
        return;
    }
}

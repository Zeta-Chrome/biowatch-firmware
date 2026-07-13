#include "biowatch/bsp.h"
#include "drivers/exti/exti.h"
#include "drivers/gpio/gpio.h"
#include "drivers/spi/spi.h"
#include "drivers/spi/spi_bus.h"
#include "imu.h"
#include "imu_regs.h"
#include "kernel/sync/event.h"
#include "kernel/sync/mutex.h"
#include "kernel/task/task.h"
#include "lib/status.h"
#include "lib/utils.h"
#include <stdint.h>
#include <string.h>

#define EVENT_OK BIT(0)
#define EVENT_MODF BIT(1)
#define EVENT_OVR BIT(2)
#define EVENT_ERR BIT(3)

static imu_callback_t g_callback;
static exti_handle_t g_exti1_h;
static spi_handle_t g_spi_h;
static event_t g_event;
static uint8_t g_rx_buf[16];
static uint8_t g_tx_buf[16];
static bool g_gyro_en = false;
static uint8_t g_en_ints[2];
static mutex_t g_mutex;

static void on_interrupt1(void *user_data)
{
    (void)user_data;
    g_callback();
}

static void on_spi_callback(bw_status_t status, void *user_data)
{
    (void)user_data;
    if (status == STATUS_OK)
    {
        kernel_event_set_from_isr(&g_event, EVENT_OK);
    }
    else if (status == STATUS_SPI_MODF)
    {
        kernel_event_set_from_isr(&g_event, EVENT_MODF);
    }
    else if (status == STATUS_SPI_OVR)
    {
        kernel_event_set_from_isr(&g_event, EVENT_OVR);
    }
    else
    {
        kernel_event_set_from_isr(&g_event, EVENT_ERR);
    }
}

static bw_status_t transact_and_wait(uint16_t len)
{
    g_spi_h.len = len;
    gpio_set_level(PL_IMU_CS, 0);

    spi_bus_lock(g_spi_h.perip);
    spi_transact_dma(&g_spi_h);

    uint32_t event_bit;
    bw_status_t status = kernel_event_wait(&g_event, EVENT_OK | EVENT_MODF | EVENT_OVR | EVENT_ERR, &event_bit, true, false,
                                           100);
    spi_bus_unlock(g_spi_h.perip);

    gpio_set_level(PL_IMU_CS, 1);
    if (status == STATUS_TIMEOUT || (event_bit & (EVENT_MODF | EVENT_OVR | EVENT_ERR)))
    {
        BW_LOG("SPI Fail: %s\n", (status == STATUS_TIMEOUT) ? "TIMEOUT"
                                 : (event_bit & EVENT_MODF) ? "MODF"
                                 : (event_bit & EVENT_OVR)  ? "OVR"
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

static bw_status_t imu_configure(imu_odr_t odr, imu_step_mode_t st_mode, imu_nomo_t nomo_mode, uint16_t nomo_dur_s)
{
    // Configure accelerometer and gyroscope
    g_tx_buf[0] = IMU_WRITE | IMU_ACC_CONF;
    g_tx_buf[1] = IMU_ACC_CONF_US_Msk | 0x0 << IMU_ACC_CONF_BWP_Pos | odr << IMU_ACC_CONF_ODR_Pos; // us, OSR4
    g_tx_buf[2] = 0x8 << IMU_ACC_RANGE_Pos;                                                        // +- 8g
    g_tx_buf[3] = 0x0 << IMU_GYR_CONF_BWP_Pos | odr << IMU_GYR_CONF_ODR_Pos;                       // OSR4
    g_tx_buf[4] = 0x3 << IMU_GYR_RANGE_Pos;                                                        // 250 deg/s
    bw_status_t status = transact_and_wait(5);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to configure accelerometer and gyroscope\n");
        return status;
    }

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
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to configure step\n");
        return status;
    }

    // Configure nomotion int
    g_tx_buf[0] = IMU_WRITE | IMU_INT_MO0;
    g_tx_buf[1] = encode_no_motion_dur(nomo_dur_s) << IMU_INT_MO0_SN_DUR_Pos;
    g_tx_buf[2] = 0;
    g_tx_buf[3] = nomo_mode << IMU_INT_MO2_SN_TH_Pos;
    g_tx_buf[4] = IMU_INT_MO3_NOMO_SEL_Msk;
    status = transact_and_wait(5);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to configure nomotion\n");
        return status;
    }

    // Map interrupts
    g_tx_buf[0] = IMU_WRITE | IMU_INT1_MAP0;
    g_tx_buf[1] = IMU_INT1_MAP0_NOMO_Msk;
    g_tx_buf[2] = IMU_INT1_MAP1_DRDY_Msk;
    status = transact_and_wait(3);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to map interrupts\n");
        return status;
    }

    // Power up accelormeter
    g_tx_buf[0] = IMU_WRITE | IMU_CMD;
    g_tx_buf[1] = IMU_CMD_ACC_LOW_PWR;
    status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to power on accelerometer\n");
        return status;
    }
    kernel_task_delay(5); // Delay of maximum 3.8ms

    // Enable interrupts
    g_en_ints[0] = 0;
    g_en_ints[1] = IMU_INT_EN2_STEP_Msk | IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk | IMU_INT_EN2_NOMOZ_Msk;
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
    g_tx_buf[1] = g_en_ints[0];
    g_tx_buf[2] = g_en_ints[1];
    g_tx_buf[3] = IMU_INT1_OUT_EN_Msk; // PUSH PULL + ACTIVE LOW
    status = transact_and_wait(4);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to enable interrupts\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t imu_init(imu_odr_t odr, imu_step_mode_t st_mode, imu_nomo_t nomo_mode, uint16_t nomo_dur_s,
                     imu_callback_t callback)
{
    BW_ASSERT(nomo_dur_s >= 2 && nomo_dur_s <= 431, "Double tap duration not in valid range (Expected 2s-431s)");

    g_callback = callback;

    // Set CS to high to use BMI160 in SPI mode
    gpio_set_level(PL_IMU_CS, 1);

    // Let IMU power up
    kernel_task_delay(10);

    // Init event
    kernel_event_init(&g_event);
    kernel_mutex_init(&g_mutex);

    // Common handle configurations
    g_spi_h.data_sz = 8;
    g_spi_h.rx_buf = g_rx_buf;
    g_spi_h.tx_buf = g_tx_buf;
    g_spi_h.user_data = NULL;
    g_spi_h.callback = on_spi_callback;

    // Dummy byte transaction
    g_tx_buf[0] = IMU_READ | 0x7F;
    bw_status_t status = transact_and_wait(1);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to read Dummy byte\n");
        return status;
    }
    kernel_task_delay(10);

    status = imu_configure(odr, st_mode, nomo_mode, nomo_dur_s);
    if (status != STATUS_OK)
    {
        return status;
    }

    return STATUS_OK;
}

bw_status_t imu_read_error()
{
    g_tx_buf[0] = IMU_READ | IMU_ERR;
    g_tx_buf[1] = 0;
    bw_status_t status = transact_and_wait(2);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to read error\n");
        return status;
    }

    BW_LOG("Err: %x, Fatal error : %d, Error Code : %d, Dropped cmd error: %d\n", g_rx_buf[1], g_rx_buf[1] & 0x1,
           g_rx_buf[1] & 0x1E, g_rx_buf[1] & 0x40);

    return STATUS_OK;
}

bw_status_t imu_read_int_status(uint8_t int_status[4])
{
    g_tx_buf[0] = IMU_READ | IMU_INT_ST0;
    g_tx_buf[1] = 0;
    g_tx_buf[2] = 0;
    g_tx_buf[3] = 0;
    g_tx_buf[4] = 0;
    bw_status_t status = transact_and_wait(5);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to read interrupt status\n");
        return STATUS_ERR;
    }

    int_status[0] = g_rx_buf[1];
    int_status[1] = g_rx_buf[2];
    int_status[2] = g_rx_buf[3];
    int_status[3] = g_rx_buf[4];

    g_tx_buf[0] = IMU_WRITE | IMU_CMD;
    g_tx_buf[1] = IMU_CMD_INT_RST;
    status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to reset interrupt status\n");
        return STATUS_ERR;
    }

    return STATUS_OK;
}

bw_status_t imu_read_step_cnt(uint16_t *step_cnt)
{
    g_tx_buf[0] = IMU_READ | IMU_STEP_CNT;
    g_tx_buf[1] = 0;
    g_tx_buf[2] = 0;
    bw_status_t status = transact_and_wait(3);
    if (status == STATUS_ERR)
    {
        BW_LOG("Failed to read step count\n");
        return STATUS_ERR;
    }

    *step_cnt = *(uint16_t *)(&g_rx_buf[1]);
    return STATUS_OK;
}

bw_status_t imu_enable_nomo_int()
{
    g_en_ints[1] |= IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk | IMU_INT_EN2_NOMOZ_Msk;
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN2;
    g_tx_buf[1] = g_en_ints[1];
    bw_status_t status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to enable nomotion interrupts\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t imu_disable_nomo_int()
{
    g_en_ints[1] &= ~(IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk | IMU_INT_EN2_NOMOZ_Msk);
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN2;
    g_tx_buf[1] = g_en_ints[1];
    bw_status_t status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to enable nomotion interrupts\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t imu_start_stream(bool gyro_en)
{
    bw_status_t status;
    kernel_mutex_lock(&g_mutex, MAX_TIMEOUT);

    if (gyro_en)
    {
        g_tx_buf[0] = IMU_WRITE | IMU_CMD;
        g_tx_buf[1] = IMU_CMD_GYR_NORMAL;
        status = transact_and_wait(2);
        if (status != STATUS_OK)
        {
            BW_LOG("Failed to turn on gyroscope\n");
            kernel_mutex_unlock(&g_mutex);
            return STATUS_ERR;
        }
        g_gyro_en = true;
        kernel_task_delay(80);
    }

    g_en_ints[0] |= IMU_INT_EN1_DRDY_Msk;
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
    g_tx_buf[1] = g_en_ints[0];
    status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to enable data ready interrupt\n");
        kernel_mutex_unlock(&g_mutex);
        return status;
    }

    kernel_mutex_unlock(&g_mutex);

    return STATUS_OK;
}

bw_status_t imu_read_sample(imu_acc_sample_t *acc_sample, imu_gyr_sample_t *gyr_sample)
{
    bw_status_t status;
    kernel_mutex_lock(&g_mutex, MAX_TIMEOUT);

    memset(g_tx_buf + 1, 0, 12);

    if (g_gyro_en && gyr_sample)
    {
        g_tx_buf[0] = IMU_READ | IMU_GYR_DATA;
        status = transact_and_wait(13);
    }
    else
    {
        g_tx_buf[0] = IMU_READ | IMU_ACC_DATA;
        status = transact_and_wait(7);
    }
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to read data\n");
        kernel_mutex_unlock(&g_mutex);
        return status;
    }

    if (g_gyro_en && gyr_sample)
    {
        *gyr_sample = *(imu_gyr_sample_t *)(g_rx_buf + 1);
    }

    if (acc_sample)
    {
        *acc_sample = *(imu_acc_sample_t *)(g_rx_buf + 7);
    }

    kernel_mutex_unlock(&g_mutex);

    return STATUS_OK;
}

bw_status_t imu_stop_stream()
{
    bw_status_t status;
    kernel_mutex_lock(&g_mutex, MAX_TIMEOUT);

    g_en_ints[0] &= ~IMU_INT_EN1_DRDY_Msk;
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
    g_tx_buf[1] = g_en_ints[0];
    status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to disable data ready interrupt\n");
        kernel_mutex_unlock(&g_mutex);
        return status;
    }

    if (g_gyro_en)
    {
        g_tx_buf[0] = IMU_WRITE | IMU_CMD;
        g_tx_buf[1] = IMU_CMD_GYR_SUSPEND;
        status = transact_and_wait(2);
        if (status != STATUS_OK)
        {
            BW_LOG("Failed to suspend gyroscope\n");
            kernel_mutex_unlock(&g_mutex);
            return STATUS_ERR;
        }
        g_gyro_en = false;
    }

    kernel_mutex_unlock(&g_mutex);

    return STATUS_OK;
}

bw_status_t imu_wakeup()
{
    bw_status_t status;

    // Enable interrupts
    g_en_ints[0] = 0;
    g_en_ints[1] = IMU_INT_EN2_STEP_Msk | IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk | IMU_INT_EN2_NOMOZ_Msk;
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
    g_tx_buf[1] = g_en_ints[0];
    g_tx_buf[2] = g_en_ints[1];
    status = transact_and_wait(3);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to enable interrupts\n");
        return status;
    }

    // Turn on accelerometer
    g_tx_buf[0] = IMU_WRITE | IMU_CMD;
    g_tx_buf[1] = IMU_CMD_ACC_LOW_PWR;
    status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to turn on accelerometer\n");
        return status;
    }

    return STATUS_OK;
}

bw_status_t imu_sleep()
{
    // Disable interrupts
    g_en_ints[0] = 0;
    g_en_ints[1] = 0;
    g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
    g_tx_buf[1] = g_en_ints[0];
    g_tx_buf[2] = g_en_ints[1];
    bw_status_t status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to disable interrupts\n");
        return STATUS_ERR;
    }

    if (g_gyro_en)
    {
        // Turn off gyroscope
        g_tx_buf[0] = IMU_WRITE | IMU_CMD;
        g_tx_buf[1] = IMU_CMD_GYR_SUSPEND;
        status = transact_and_wait(2);
        if (status != STATUS_OK)
        {
            BW_LOG("Failed to turn off gyroscope\n");
            return status;
        }
    }

    return STATUS_OK;
}

bw_status_t imu_shutdown()
{
    bw_status_t status = imu_sleep();
    if (status != STATUS_OK)
    {
        return status;
    }

    // Turn off accelerometer
    g_tx_buf[0] = IMU_WRITE | IMU_CMD;
    g_tx_buf[1] = IMU_CMD_ACC_SUSPEND;
    status = transact_and_wait(2);
    if (status != STATUS_OK)
    {
        BW_LOG("Failed to turn off accelerometer\n");
        return status;
    }

    return STATUS_OK;
}

spi_handle_t *imu_get_spi_handle()
{
    return &g_spi_h;
}

exti_handle_t *imu_get_exti_handle(exti_callback_t *callback)
{
    *callback = on_interrupt1;
    return &g_exti1_h;
}

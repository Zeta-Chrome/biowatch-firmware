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
#include "subsys/lpm/lpm.h"
#include <stdint.h>
#include <string.h>

#define EVENT_OK BIT(0)
#define EVENT_MODF BIT(1)
#define EVENT_OVR BIT(2)
#define EVENT_ERR BIT(3)

#define IMU_INT_LATCH 0x54

static imu_callback_t g_callback;
static struct exti_handle g_exti1_h;
static struct spi_handle g_spi_h;
static struct event g_event;
static uint8_t g_rx_buf[16];
static uint8_t g_tx_buf[16];
static bool g_gyro_en = false;
static uint8_t g_en_ints[2];
static struct mutex g_mutex;

static void on_interrupt1(void *user_data)
{
	(void)user_data;

	if (g_callback)
		g_callback();
}

static void on_spi_callback(enum bw_status status, void *user_data)
{
	(void)user_data;
	if (status == STATUS_OK)
		kernel_event_set_from_isr(&g_event, EVENT_OK);
	else if (status == STATUS_SPI_MODF)
		kernel_event_set_from_isr(&g_event, EVENT_MODF);
	else if (status == STATUS_SPI_OVR)
		kernel_event_set_from_isr(&g_event, EVENT_OVR);
	else
		kernel_event_set_from_isr(&g_event, EVENT_ERR);
}

static enum bw_status transact_and_wait(uint16_t len)
{
	enum bw_status status;

	g_spi_h.len = len;
	gpio_set_level(PL_IMU_CS, 0);

	spi_bus_lock(g_spi_h.perip);
	lpm_disable_mode(LPM_MODE_LP_SLEEP, "IMU");
	spi_transact_dma(&g_spi_h);

	uint32_t event_bit;
	status = kernel_event_wait(&g_event, EVENT_OK | EVENT_MODF | EVENT_OVR | EVENT_ERR, &event_bit,
							   true, false, 100);
	lpm_enable_mode(LPM_MODE_LP_SLEEP, "IMU");
	spi_bus_unlock(g_spi_h.perip);

	gpio_set_level(PL_IMU_CS, 1);
	if (status == STATUS_TIMEOUT || (event_bit & (EVENT_MODF | EVENT_OVR | EVENT_ERR))) {
		BW_LOG("SPI Fail: %s\n", (status == STATUS_TIMEOUT) ? "TIMEOUT" :
								 (event_bit & EVENT_MODF)	? "MODF" :
								 (event_bit & EVENT_OVR)	? "OVR" :
															  "ERR");
		return STATUS_ERR;
	}

	return STATUS_OK;
}

static uint8_t encode_no_motion_dur(uint8_t duration_sec)
{
	if (duration_sec <= 20) {
		uint8_t val = (uint8_t)(DIVC(duration_sec * 100, 128));
		if (val > 0)
			val -= 1;
		return val & 0x0F;
	} else if (duration_sec <= 102) {
		uint8_t val = (uint8_t)(DIVC(duration_sec * 100, 512));
		if (val >= 5)
			val -= 5;
		return (0x01 << 4) | (val & 0x0F);
	} else {
		uint8_t val = (uint8_t)(DIVC(duration_sec * 100, 1024));
		if (val >= 11)
			val -= 11;
		return (0x01 << 5) | (val & 0x1F);
	}
}

static enum bw_status imu_configure(enum imu_odr odr, enum imu_step_mode st_mode,
									enum imu_nomo nomo_mode, uint16_t nomo_dur_s)
{
	enum bw_status status;

	// 1. Configure Accelerometer: acc_us = 0, bwp = 2 (Normal mode OSR4 filter), ODR = odr
	g_tx_buf[0] = IMU_WRITE | IMU_ACC_CONF;
	g_tx_buf[1] = (0x2 << IMU_ACC_CONF_BWP_Pos) | (odr & 0x0F);
	g_tx_buf[2] = 0x8 << IMU_ACC_RANGE_Pos; // +/- 8g
	status = transact_and_wait(3);
	if (status != STATUS_OK)
		return status;

	// 2. Power on Accelerometer in Normal Mode
	g_tx_buf[0] = IMU_WRITE | IMU_CMD;
	g_tx_buf[1] = IMU_CMD_ACC_NORMAL;
	status = transact_and_wait(2);
	if (status != STATUS_OK)
		return status;
	kernel_task_delay(5);

	// 3. Configure Step Counter
	g_tx_buf[0] = IMU_WRITE | IMU_STEP_CONF;
	switch (st_mode) {
	case IMU_STEP_MODE_SENSITIVE:
		g_tx_buf[1] = 0x2D;
		g_tx_buf[2] = IMU_STEP_CNT_EN_Msk | 0x0;
		break;
	case IMU_STEP_MODE_ROBUST:
		g_tx_buf[1] = 0x1D;
		g_tx_buf[2] = IMU_STEP_CNT_EN_Msk | 0x7;
		break;
	case IMU_STEP_MODE_NORMAL:
	default:
		g_tx_buf[1] = 0x15;
		g_tx_buf[2] = IMU_STEP_CNT_EN_Msk | 0x3;
		break;
	}
	status = transact_and_wait(3);
	if (status != STATUS_OK)
		return status;

	// 4. Configure No-motion
	g_tx_buf[0] = IMU_WRITE | IMU_INT_MO0;
	g_tx_buf[1] = encode_no_motion_dur(nomo_dur_s) << IMU_INT_MO0_SN_DUR_Pos;
	g_tx_buf[2] = 0;
	g_tx_buf[3] = nomo_mode << IMU_INT_MO2_SN_TH_Pos;
	g_tx_buf[4] = IMU_INT_MO3_NOMO_SEL_Msk;
	status = transact_and_wait(5);
	if (status != STATUS_OK)
		return status;

	// 5. Map INT1: Step & No-Motion on MAP0, DRDY on MAP1
	g_tx_buf[0] = IMU_WRITE | IMU_INT1_MAP0;
	g_tx_buf[1] = IMU_INT1_MAP0_NOMO_Msk | IMU_INT1_MAP0_STEP_Msk;
	g_tx_buf[2] = IMU_INT1_MAP1_DRDY_Msk;
	status = transact_and_wait(3);
	if (status != STATUS_OK)
		return status;

	// 6. Configure Electrical Pin Characteristics & Latching
	// 0x53 (INT_OUT_CTRL): int1_output_en = 1, int1_od = 0 (Push-Pull), int1_lvl = 0 (Active-Low), int1_edge = 0 (Level)
	// 0x54 (INT_LATCH): 0x00 (Non-latched mode)
	g_tx_buf[0] = IMU_WRITE | IMU_INT_OUT;
	g_tx_buf[1] = IMU_INT1_OUT_EN_Msk; // Active-Low, Push-Pull, Level-driven
	g_tx_buf[2] = 0x00; // Non-latched mode
	status = transact_and_wait(3);
	if (status != STATUS_OK)
		return status;

	// 7. Enable Interrupts (Step + No-Motion initially; DRDY controlled dynamically)
	g_en_ints[0] = 0;
	g_en_ints[1] = IMU_INT_EN2_STEP_Msk | IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk |
				   IMU_INT_EN2_NOMOZ_Msk;
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
	g_tx_buf[1] = g_en_ints[0];
	g_tx_buf[2] = g_en_ints[1];
	status = transact_and_wait(3);
	if (status != STATUS_OK)
		return status;

	// 8. Enable Offset Compensation
	g_tx_buf[0] = IMU_WRITE | IMU_OFFSET_CONF;
	g_tx_buf[1] = IMU_OFFSET_CONF_ACC_EN_Msk | IMU_OFFSET_CONF_GYR_EN_Msk;
	return transact_and_wait(2);
}

enum bw_status imu_init(enum imu_odr odr, enum imu_step_mode st_mode, enum imu_nomo nomo_mode,
						uint16_t nomo_dur_s, imu_callback_t callback)
{
	BW_ASSERT(nomo_dur_s >= 2 && nomo_dur_s <= 431,
			  "Duration not in valid range (Expected 2s-431s)");

	g_callback = callback;
	gpio_set_level(PL_IMU_CS, 1);
	kernel_task_delay(10);

	kernel_event_init(&g_event);
	kernel_mutex_init(&g_mutex);

	g_spi_h.data_sz = 8;
	g_spi_h.rx_buf = g_rx_buf;
	g_spi_h.tx_buf = g_tx_buf;
	g_spi_h.user_data = NULL;
	g_spi_h.callback = on_spi_callback;

	// Dummy read to enable SPI mode on the BMI160
	g_tx_buf[0] = IMU_READ | 0x7F;
	enum bw_status status = transact_and_wait(1);
	if (status != STATUS_OK)
		return status;
	kernel_task_delay(10);

	return imu_configure(odr, st_mode, nomo_mode, nomo_dur_s);
}

enum bw_status imu_read_int_status(uint8_t int_status[2])
{
	g_tx_buf[0] = IMU_READ | IMU_INT_ST0;
	g_tx_buf[1] = 0;
	g_tx_buf[2] = 0;
	enum bw_status status = transact_and_wait(3);
	if (status != STATUS_OK)
		return STATUS_ERR;

	int_status[0] = g_rx_buf[1];
	int_status[1] = g_rx_buf[2];

	return STATUS_OK;
}

enum bw_status imu_read_step_cnt(uint16_t *step_cnt)
{
	g_tx_buf[0] = IMU_READ | IMU_STEP_CNT;
	g_tx_buf[1] = 0;
	g_tx_buf[2] = 0;
	enum bw_status status = transact_and_wait(3);
	if (status != STATUS_OK)
		return STATUS_ERR;

	*step_cnt = (uint16_t)(g_rx_buf[1] | (g_rx_buf[2] << 8));
	return STATUS_OK;
}

enum bw_status imu_clear_step_cnt(void)
{
	g_tx_buf[0] = IMU_WRITE | IMU_CMD;
	g_tx_buf[1] = IMU_CMD_STEP_CNT_CLR;
	return transact_and_wait(2);
}

enum bw_status imu_enable_nomo_int(void)
{
	g_en_ints[1] |= IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk | IMU_INT_EN2_NOMOZ_Msk;
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN2;
	g_tx_buf[1] = g_en_ints[1];
	return transact_and_wait(2);
}

enum bw_status imu_disable_nomo_int(void)
{
	g_en_ints[1] &= ~(IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk | IMU_INT_EN2_NOMOZ_Msk);
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN2;
	g_tx_buf[1] = g_en_ints[1];
	return transact_and_wait(2);
}

enum bw_status imu_enable_drdy_int(void)
{
	g_en_ints[0] |= IMU_INT_EN1_DRDY_Msk;
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
	g_tx_buf[1] = g_en_ints[0];
	return transact_and_wait(2);
}

enum bw_status imu_disable_drdy_int(void)
{
	g_en_ints[0] &= ~IMU_INT_EN1_DRDY_Msk;
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
	g_tx_buf[1] = g_en_ints[0];
	return transact_and_wait(2);
}

enum bw_status imu_start_stream(bool gyro_en)
{
	enum bw_status status;
	kernel_mutex_lock(&g_mutex, MAX_TIMEOUT);

	if (gyro_en) {
		g_tx_buf[0] = IMU_WRITE | IMU_CMD;
		g_tx_buf[1] = IMU_CMD_GYR_NORMAL;
		status = transact_and_wait(2);
		if (status != STATUS_OK) {
			kernel_mutex_unlock(&g_mutex);
			return status;
		}
		g_gyro_en = true;
		kernel_task_delay(80);

		g_tx_buf[0] = IMU_WRITE | IMU_GYR_CONF;
		g_tx_buf[1] = (0x2 << IMU_GYR_CONF_BWP_Pos) | (IMU_ODR_200 & 0x0F);
		g_tx_buf[2] = 0x3 << IMU_GYR_RANGE_Pos; // 250 deg/s
		transact_and_wait(3);
	}

	status = imu_enable_drdy_int();
	kernel_mutex_unlock(&g_mutex);
	return status;
}

enum bw_status imu_read_sample(struct acc_sample *acc, struct gyr_sample *gyr)
{
	enum bw_status status;
	kernel_mutex_lock(&g_mutex, MAX_TIMEOUT);

	// Always burst read all 12 bytes (0x0C to 0x17) so the BMI160 hardware clears DRDY
	g_tx_buf[0] = IMU_READ | IMU_GYR_DATA;
	status = transact_and_wait(13);
	if (status != STATUS_OK) {
		BW_LOG("Failed to read data\n");
		kernel_mutex_unlock(&g_mutex);
		return status;
	}

	if (g_gyro_en && gyr) {
		int16_t gx = (int16_t)(g_rx_buf[1] | (g_rx_buf[2] << 8));
		int16_t gy = (int16_t)(g_rx_buf[3] | (g_rx_buf[4] << 8));
		int16_t gz = (int16_t)(g_rx_buf[5] | (g_rx_buf[6] << 8));
		gyr->gx = ((float)gx * 250.0f) / 32768.0f;
		gyr->gy = ((float)gy * 250.0f) / 32768.0f;
		gyr->gz = ((float)gz * 250.0f) / 32768.0f;
	}

	if (acc) {
		int16_t ax = (int16_t)(g_rx_buf[7] | (g_rx_buf[8] << 8));
		int16_t ay = (int16_t)(g_rx_buf[9] | (g_rx_buf[10] << 8));
		int16_t az = (int16_t)(g_rx_buf[11] | (g_rx_buf[12] << 8));
		acc->ax = ((float)ax * 8.0f) / 32768.0f;
		acc->ay = ((float)ay * 8.0f) / 32768.0f;
		acc->az = ((float)az * 8.0f) / 32768.0f;
	}

	kernel_mutex_unlock(&g_mutex);
	return STATUS_OK;
}

enum bw_status imu_stop_stream(void)
{
	enum bw_status status;
	kernel_mutex_lock(&g_mutex, MAX_TIMEOUT);

	status = imu_disable_drdy_int();
	if (status != STATUS_OK) {
		kernel_mutex_unlock(&g_mutex);
		return status;
	}

	if (g_gyro_en) {
		g_tx_buf[0] = IMU_WRITE | IMU_CMD;
		g_tx_buf[1] = IMU_CMD_GYR_SUSPEND;
		status = transact_and_wait(2);
		if (status != STATUS_OK) {
			kernel_mutex_unlock(&g_mutex);
			return STATUS_ERR;
		}
		g_gyro_en = false;
	}

	kernel_mutex_unlock(&g_mutex);
	return STATUS_OK;
}

enum bw_status imu_wakeup(void)
{
	g_en_ints[0] = 0;
	g_en_ints[1] = IMU_INT_EN2_STEP_Msk | IMU_INT_EN2_NOMOX_Msk | IMU_INT_EN2_NOMOY_Msk |
				   IMU_INT_EN2_NOMOZ_Msk;
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
	g_tx_buf[1] = g_en_ints[0];
	g_tx_buf[2] = g_en_ints[1];
	enum bw_status status = transact_and_wait(3);
	if (status != STATUS_OK)
		return status;

	g_tx_buf[0] = IMU_WRITE | IMU_CMD;
	g_tx_buf[1] = IMU_CMD_ACC_NORMAL;
	return transact_and_wait(2);
}

enum bw_status imu_sleep(void)
{
	g_en_ints[0] = 0;
	g_en_ints[1] = 0;
	g_tx_buf[0] = IMU_WRITE | IMU_INT_EN1;
	g_tx_buf[1] = g_en_ints[0];
	g_tx_buf[2] = g_en_ints[1];
	enum bw_status status = transact_and_wait(3);
	if (status != STATUS_OK)
		return STATUS_ERR;

	if (g_gyro_en) {
		g_tx_buf[0] = IMU_WRITE | IMU_CMD;
		g_tx_buf[1] = IMU_CMD_GYR_SUSPEND;
		status = transact_and_wait(2);
		if (status != STATUS_OK)
			return status;
	}

	return STATUS_OK;
}

enum bw_status imu_shutdown(void)
{
	enum bw_status status = imu_sleep();
	if (status != STATUS_OK)
		return status;

	g_tx_buf[0] = IMU_WRITE | IMU_CMD;
	g_tx_buf[1] = IMU_CMD_ACC_SUSPEND;
	return transact_and_wait(2);
}

struct spi_handle *imu_get_spi_handle(void)
{
	return &g_spi_h;
}

struct exti_handle *imu_get_exti_handle(exti_callback_t *callback)
{
	*callback = on_interrupt1;
	return &g_exti1_h;
}

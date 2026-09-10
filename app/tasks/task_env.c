#include "task_env.h"
#include "kernel/critical.h"
#include "task_ble.h"
#include "task_ui.h"
#include "drivers/rtc/rtc.h"
#include "drivers/sensor/hygro/hygro.h"
#include "lib/utils.h"
#include "biowatch/bsp.h"
#include "kernel/sync/mqueue.h"
#include "core/drivers/adc/adc.h"
#include "kernel/task/task.h"

#define ENV_QUEUE_CAPACITY 240
#define ADC_SUCCESS_NTF BIT(0)
#define ADC_FAILURE_NTF BIT(1)

task_handle_t g_task_env_h;
static struct env_record g_env_buf[ENV_QUEUE_CAPACITY];
struct mqueue g_env_mqueue;
static struct env_record g_current_env;

static void adc_callback(enum bw_status status, void *user_data)
{
	(void)user_data;
	if (status == STATUS_OK)
		kernel_task_notify(g_task_env_h, ADC_SUCCESS_NTF, NOTIFY_ACTION_SET_BITS);
	else
		kernel_task_notify(g_task_env_h, ADC_FAILURE_NTF, NOTIFY_ACTION_SET_BITS);
}

void task_env(void *user_data)
{
	(void)user_data;

	uint32_t ntf;
	uint16_t ldr_adc;
	uint32_t secs = 0;

	uint32_t rhx100_sum = 0;
	int tempx100_sum = 0;
	int temp_count = 0;
	uint32_t luxx100_sum = 0;
	int lux_count = 0;

	kernel_mqueue_init(&g_env_mqueue, g_env_buf, ENV_QUEUE_CAPACITY, sizeof(struct env_record));

	struct adc_conf conf = { .inp = ADC_INP_SINGLE,
							 .gpios = { PL_LDR_PIN },
							 .smp = { ADC_SMP_640_5_CLK },
							 .in = { PL_LDR_ADC_CH },
							 .inlen = 1,
							 .irq_priority = 5 };
	adc_init(&conf);
	struct adc_handle handle = {
		.buf = &ldr_adc, .inseq = { PL_LDR_ADC_CH }, .inseqlen = 1, .callback = adc_callback
	};

	hygro_init();

	while (1) {
		// every 1 second
		adc_convert(&handle);

		// every 1 minutes
		if (secs % 60 == 0) {
			int tempx100;
			uint16_t rhx100;
			hygro_read(HYGRO_REPEATABILITY_HIGH, &rhx100, &tempx100);
			g_current_env.tempx100 = (int16_t)tempx100;
			g_current_env.rhx100 = rhx100;
			rhx100_sum += g_current_env.rhx100;
			tempx100_sum += g_current_env.tempx100;
			temp_count++;
		}

		if (kernel_task_notify_wait(0, 0xFF, &ntf, 100) == STATUS_OK && ntf & ADC_SUCCESS_NTF) {
			g_current_env.luxx100 = (uint16_t)((float)ldr_adc * 16.11328125f);
			luxx100_sum += g_current_env.luxx100;
			lux_count++;
			kernel_task_notify(g_task_ui_h, UI_ENV_CHANGED_NTF, NOTIFY_ACTION_SET_BITS);
		}

		// Every 10 mintues
		if (secs % 600 == 0) {
			struct env_record rec = { .timestamp = rtc_get_timestamp(),
									  .rhx100 = rhx100_sum / temp_count,
									  .tempx100 = (int16_t)(tempx100_sum / temp_count),
									  .luxx100 = luxx100_sum / lux_count };
			kernel_mqueue_overwrite(&g_env_mqueue, &rec);
			kernel_task_notify(g_task_ble_h, BLE_ENV_CHANGED_NTF, NOTIFY_ACTION_SET_BITS);
		}

		secs++;
		kernel_task_delay(1000);
	}
}

void task_env_get_latest_record(struct env_record *rec)
{
	if (!rec)
		return;

	KERNEL_ENTER_CRITICAL();
	*rec = g_current_env;
	KERNEL_EXIT_CRITICAL();
}

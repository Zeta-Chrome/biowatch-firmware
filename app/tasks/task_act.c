#include "kernel/critical.h"
#include "kernel/sync/event.h"
#include "task_ble.h"
#include "task_ui.h"
#include "arm_math.h"
#include "drivers/rtc/rtc.h"
#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/imu/imu_regs.h"
#include "kernel/kernel.h"
#include "kernel/sync/mqueue.h"
#include "kernel/task/task.h"
#include "task_act.h"
#include "kernel/timer.h"
#include "tasks.h"
#include "app/settings.h"
#include <stdint.h>
#include <string.h>

#define NOMO_DUR_S 5
#define ACT_QUEUE_CAPACITY 8
#define IMU_BUF_CAPACITY 512
#define IMU_INT_NTF BIT(0)
#define A_COEFF 0.087135f
#define B_COEFF 0.078120f
#define C_COEFF 0.411146f
#define D_COEFF -0.339232f
#define MAX_INACTIVITY_PERIOD 2000 // 2 seconds

task_handle_t g_task_act_h;
static struct act_record g_act_buf[ACT_QUEUE_CAPACITY];
struct mqueue g_act_mqueue;
static struct act_record g_current_act;
struct kernel_timer g_inactive_timer;

static struct {
	float acc_mag_buf[IMU_BUF_CAPACITY];
	uint16_t acc_mag_cnt;
	uint64_t last_step_time_ms;
	bool have_last_step;
	bool is_streaming;
} g_act_metrics;

static void act_metrics_reset(void *user_data)
{
	(void)user_data;
	kernel_task_notify(g_task_act_h, ACT_RESET_NTF, NOTIFY_ACTION_SET_BITS);
}

static void push_record(void)
{
	kernel_mqueue_overwrite(&g_act_mqueue, &g_current_act);
}

static void act_metrics_add_acc(void)
{
	if (g_act_metrics.acc_mag_cnt >= IMU_BUF_CAPACITY) {
		g_act_metrics.acc_mag_cnt = 0;
	}
	struct acc_sample acc;
	if (imu_read_sample(&acc, NULL) == STATUS_OK) {
		arm_sqrt_f32(acc.ax * acc.ax + acc.ay * acc.ay + acc.az * acc.az,
					 &g_act_metrics.acc_mag_buf[g_act_metrics.acc_mag_cnt++]);
	}
}

static void act_metrics_on_step(void)
{
	uint16_t hw_steps = 0;
	imu_read_step_cnt(&hw_steps);

	if (hw_steps > 0) {
		g_current_act.steps = hw_steps;
	} else {
		g_current_act.steps++;
	}

	uint64_t ms = kernel_timer_ms();
	uint32_t dt_ms = (uint32_t)(ms - g_act_metrics.last_step_time_ms);
	g_act_metrics.last_step_time_ms = ms;

	// Dynamically activate 200 Hz stream if dormant
	if (!g_act_metrics.is_streaming) {
		imu_enable_drdy_int();
		g_act_metrics.is_streaming = true;
	}

	if (dt_ms == 0 || !g_act_metrics.have_last_step || g_act_metrics.acc_mag_cnt < 2) {
		g_act_metrics.have_last_step = true;
		g_act_metrics.acc_mag_cnt = 0;
		return;
	}

	float step_freq = 1000.0f / (float)dt_ms;

	float acc_var;
	arm_var_f32(g_act_metrics.acc_mag_buf, g_act_metrics.acc_mag_cnt, &acc_var);

	float step_len = ((float)g_app_settings.height_cm / 100.0f) *
						 (A_COEFF * step_freq + B_COEFF * acc_var + C_COEFF) +
					 D_COEFF;

	step_len = MAX(step_len, 0.20f);

	g_current_act.distance_m += step_len;
	g_current_act.dur_ms += dt_ms;
	g_current_act.speed_ms = g_current_act.distance_m * 1000.0f / (float)g_current_act.dur_ms;

	float dt_sec = (float)dt_ms / 1000.0f;
	float step_speed = step_len / dt_sec;

	float mets = 1.714f * step_speed + 1.0f;
	if (mets < 1.0f)
		mets = 1.0f;

	float weight_kg = (float)g_app_settings.weight_kg;
	float step_kcal = mets * weight_kg * (0.0175f / 60.0f) * dt_sec;

	g_current_act.calories_kcal += step_kcal;

	g_act_metrics.have_last_step = true;
	g_act_metrics.acc_mag_cnt = 0;
}

static void imu_callback(void)
{
	kernel_task_notify_from_isr(g_task_act_h, IMU_INT_NTF, NOTIFY_ACTION_SET_BITS);
}

void task_act(void *user_data)
{
	(void)user_data;

	g_inactive_timer.type = KERNEL_TIMER_ONE_SHOT;
	g_inactive_timer.ticks = MAX_INACTIVITY_PERIOD;
	g_inactive_timer.callback = act_metrics_reset;
	kernel_mqueue_init(&g_act_mqueue, g_act_buf, ACT_QUEUE_CAPACITY, sizeof(struct act_record));

	enum bw_status status =
		imu_init(IMU_ODR_200, IMU_STEP_MODE_NORMAL, IMU_NOMO_NORMAL, NOMO_DUR_S, imu_callback);
	if (status != STATUS_OK)
		BW_LOG("Failed to initialize, Exit with status: %d\n", status);

#ifdef IMU_FOC
	imu_start_foc();
#endif

	kernel_event_set(&g_app_evt, IMU_RDY_EVT);

	// DO NOT call imu_start_stream here! DRDY stays OFF until a step occurs.
	g_act_metrics.have_last_step = false;
	g_act_metrics.acc_mag_cnt = 0;
	g_act_metrics.is_streaming = false;

	uint32_t ntf;
	uint8_t int_status[2];

	while (1) {
		kernel_task_notify_wait(0, 0xFFFFFFFFu, &ntf, MAX_TIMEOUT);

		if (ntf & IMU_INT_NTF) {
			status = imu_read_int_status(int_status);
			if (status == STATUS_OK) {
				if (int_status[0] & IMU_INT_ST0_STEP_Msk) {
					BW_LOG("STEP\n");
					act_metrics_on_step();
					kernel_timer_start(&g_inactive_timer);
					kernel_task_notify(g_task_ui_h, UI_ACT_CHANGED_NTF, NOTIFY_ACTION_SET_BITS);
					kernel_task_notify(g_task_ble_h, BLE_ACT_CHANGED_NTF, NOTIFY_ACTION_SET_BITS);
				}
				if (int_status[1] & IMU_INT_ST1_NOMO_Msk) {
					act_metrics_reset(NULL);
				}
				if (int_status[1] & IMU_INT_ST1_DRDY_Msk) {
					BW_LOG("DATA RDY\n");
					act_metrics_add_acc();
				}
			}
		}

		if (ntf & ACT_NEW_REC_NTF) {
			push_record();
			kernel_task_notify(g_task_ui_h, UI_ACT_CHANGED_NTF, NOTIFY_ACTION_SET_BITS);
			kernel_task_notify(g_task_ble_h, BLE_DRAIN_QUEUE_NTF, NOTIFY_ACTION_SET_BITS);
			memset(&g_current_act, 0, sizeof(g_current_act));
			g_current_act.timestamp = rtc_get_timestamp();
		}

		// Inactivity timer fired or hardware no-motion occurred
		if (ntf & ACT_RESET_NTF) {
			if (g_act_metrics.is_streaming) {
				imu_disable_drdy_int(); // Shut down 200 Hz interrupts
				g_act_metrics.is_streaming = false;
			}
			g_act_metrics.have_last_step = false;
			g_act_metrics.acc_mag_cnt = 0;
		}
	}
}

void task_act_get_latest_record(struct act_record *rec)
{
	if (!rec)
		return;

	uint32_t key = KERNEL_ENTER_CRITICAL();
	*rec = g_current_act;
	KERNEL_EXIT_CRITICAL(key);
}

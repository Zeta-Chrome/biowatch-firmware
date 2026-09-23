#include "task_vitals.h"
#include "arm_math.h"
#include "core/kernel/critical.h"
#include "drivers/rtc/rtc.h"
#include "drivers/sensor/imu/imu.h"
#include "drivers/sensor/oxim/oxim.h"
#include "drivers/sensor/oxim/oxim_regs.h"
#include "kernel/task/task.h"
#include "lib/logger.h"
#include "lib/status.h"
#include "task_ble.h"
#include "task_ui.h"
#include "tasks.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VITALS_QUEUE_CAPACITY 32
#define SAMPLE_RATE_HZ 25
#define INT_NTF BIT(0)
#define NUM_SKIP_SAMPLES 16
#define NUM_HR_SAMPLES 128
#define NUM_SPO2_SAMPLES 256
#define MAX_ALC_CTR 2
#define DATA_SCALE 8
#define BANDPASS_NUM_STAGES 2
#define BANDPASS_POSTSHIFT 2
#define BANDPASS_BLOCKSIZE 32
#define NUM_SKIP_WARMUP 50
#define NLMS_TAPS 16
#define NLMS_LEN (3 * NLMS_TAPS)
#define NLMS_MU 0.3f
#define NLMS_EPS 1e-3f
#define NLMS_MIN_POWER 0.01f
#define NLMS_LEAK 0.999f
#define MIN_HR_BPM 42
#define MAX_HR_BPM 180
#define REFRACTORY_SAMPLES DIVC(SAMPLE_RATE_HZ * 60, MAX_HR_BPM)
#define MAX_IBI_SAMPLES DIVC(SAMPLE_RATE_HZ * 60, MIN_HR_BPM)

#define SPO2_A -10.0f
#define SPO2_B 3.0f
#define SPO2_C 101.0f

#define PPG_MIN_RAW_DC_COUNTS 15000.0f
#define PPG_MIN_AC_ENERGY 0.0003f
#define HR_MIN_AUTOCORR_PEAK 0.1f

task_handle_t g_task_vitals_h;
static struct vitals_record g_vitals_buf[VITALS_QUEUE_CAPACITY];
struct mqueue g_vitals_mqueue;
static struct vitals_record g_current_vitals;

enum oxim_int {
	OXIM_INT_PWR_RDY,
	OXIM_INT_PPG_RDY,
	OXIM_INT_ALC_OVF,
	OXIM_INT_DIE_TEMP_RDY,
	OXIM_INT_ERR,
};

struct bandpass_filter {
	arm_biquad_cascade_df2T_instance_f32 inst;
	float32_t state[2 * BANDPASS_NUM_STAGES];
};

float32_t g_red_ppg[NUM_SPO2_SAMPLES];
float32_t g_ir_ppg[NUM_SPO2_SAMPLES];
float32_t g_ax[NUM_SPO2_SAMPLES];
float32_t g_ay[NUM_SPO2_SAMPLES];
float32_t g_az[NUM_SPO2_SAMPLES];
struct bandpass_filter g_filt_red;
struct bandpass_filter g_filt_ir;
struct bandpass_filter g_filt_ax;
struct bandpass_filter g_filt_ay;
struct bandpass_filter g_filt_az;
float32_t g_nlms_refs[NLMS_TAPS * 3];
float32_t g_nlms_red_weights[NLMS_TAPS * 3];
float32_t g_nlms_ir_weights[NLMS_TAPS * 3];

const float32_t g_bp_coeffs[5 * BANDPASS_NUM_STAGES] = {
	// Stage 1
	0.05859663f, 0.11719327f, 0.05859663f, 1.20323278f, -0.53670347f,
	// Stage 2
	1.00000000f, -2.00000000f, 1.00000000f, 1.78851849f, -0.82471540f
};

static void oxim_callback(void)
{
	kernel_task_notify_from_isr(g_task_vitals_h, INT_NTF, NOTIFY_ACTION_SET_BITS);
}

static void nlms_filters_init(void)
{
	memset(g_nlms_red_weights, 0, sizeof(g_nlms_red_weights));
	memset(g_nlms_ir_weights, 0, sizeof(g_nlms_ir_weights));
}

static enum oxim_int handle_interrupt(void)
{
	uint8_t int_status[2];

	if (oxim_read_int_status(int_status) != STATUS_OK) {
		BW_LOG("oxim_read_int_status failed\n");
		return OXIM_INT_ERR;
	}

	if (int_status[0] & OXIM_INT_ST1_PWR_RDY) {
		BW_LOG("Brown out occurred\n");
		oxim_reconfigure();
		return OXIM_INT_PWR_RDY;
	}

	if (int_status[0] & OXIM_INT_ST1_ALC_OVF)
		return OXIM_INT_ALC_OVF;

	if (int_status[1] & OXIM_INT_ST2_DIE_TEMP_RDY)
		return OXIM_INT_DIE_TEMP_RDY;

	if (!(int_status[0] & OXIM_INT_ST1_PPG_RDY)) {
		BW_LOG("Unknown interrupt occurred: (0x%x, 0x%x)\n", int_status[0], int_status[1]);
		return OXIM_INT_ERR;
	}

	return OXIM_INT_PPG_RDY;
}

static void convert_ppg_sample(enum oxim_mode mode, uint32_t red_ppg, uint32_t ir_ppg, uint16_t idx)
{
	g_red_ppg[idx] = (float32_t)red_ppg / ((1 << 18) - 1) * DATA_SCALE;
	if (mode == OXIM_MODE_SPO2)
		g_ir_ppg[idx] = (float32_t)ir_ppg / ((1 << 18) - 1) * DATA_SCALE;
}

static void convert_acc_sample(struct acc_sample *acc, uint16_t idx)
{
	g_ax[idx] = acc->ax;
	g_ay[idx] = acc->ay;
	g_az[idx] = acc->az;
}

static void bandpass_filters_init(enum oxim_mode mode)
{
	arm_biquad_cascade_df2T_init_f32(&g_filt_ax.inst, BANDPASS_NUM_STAGES, g_bp_coeffs,
									 g_filt_ax.state);
	arm_biquad_cascade_df2T_init_f32(&g_filt_ay.inst, BANDPASS_NUM_STAGES, g_bp_coeffs,
									 g_filt_ay.state);
	arm_biquad_cascade_df2T_init_f32(&g_filt_az.inst, BANDPASS_NUM_STAGES, g_bp_coeffs,
									 g_filt_az.state);
	arm_biquad_cascade_df2T_init_f32(&g_filt_red.inst, BANDPASS_NUM_STAGES, g_bp_coeffs,
									 g_filt_red.state);
	if (mode == OXIM_MODE_SPO2) {
		arm_biquad_cascade_df2T_init_f32(&g_filt_ir.inst, BANDPASS_NUM_STAGES, g_bp_coeffs,
										 g_filt_ir.state);
	}
}

static void bandpass_filters_apply(enum oxim_mode mode, uint16_t block_size)
{
	arm_biquad_cascade_df2T_f32(&g_filt_ax.inst, g_ax, g_ax, block_size);
	arm_biquad_cascade_df2T_f32(&g_filt_ay.inst, g_ay, g_ay, block_size);
	arm_biquad_cascade_df2T_f32(&g_filt_az.inst, g_az, g_az, block_size);
	arm_biquad_cascade_df2T_f32(&g_filt_red.inst, g_red_ppg, g_red_ppg, block_size);
	if (mode == OXIM_MODE_SPO2) {
		arm_biquad_cascade_df2T_f32(&g_filt_ir.inst, g_ir_ppg, g_ir_ppg, block_size);
	}
}

static void nlms_filters_apply(float32_t *signal, float32_t *weights, float32_t *ax, float32_t *ay,
							   float32_t *az, uint16_t block_size)
{
	for (int i = 0; i < block_size; i++) {
		uint16_t len = MIN(i + 1, NLMS_TAPS);
		uint16_t sz = sizeof(float32_t) * len;
		if (sz < NLMS_TAPS * sizeof(float32_t))
			memset(g_nlms_refs, 0, sizeof(float32_t) * NLMS_LEN);

		uint16_t idx = (NLMS_TAPS - len);
		memcpy(g_nlms_refs + idx, &ax[i - (len - 1)], sz);
		idx += NLMS_TAPS;
		memcpy(g_nlms_refs + idx, &ay[i - (len - 1)], sz);
		idx += NLMS_TAPS;
		memcpy(g_nlms_refs + idx, &az[i - (len - 1)], sz);

		// Predict and remove estimated motion noise
		float32_t noise;
		arm_dot_prod_f32(g_nlms_refs, weights, NLMS_LEN, &noise);
		signal[i] -= noise;

		// Calculate reference energy
		float32_t power = 0;
		arm_power_f32(g_nlms_refs, NLMS_LEN, &power);

		if (power > NLMS_MIN_POWER) {
			float32_t scale = signal[i] * NLMS_MU / (power + NLMS_EPS);
			arm_scale_f32(g_nlms_refs, scale, g_nlms_refs, NLMS_LEN);
			arm_scale_f32(weights, NLMS_LEAK, weights, NLMS_LEN);
			arm_add_f32(weights, g_nlms_refs, weights, NLMS_LEN);
		}
	}
}

static float32_t autocorr_at_lag(const float32_t *x, uint16_t n, uint16_t lag)
{
	float32_t sum = 0.0f;
	uint16_t count = n - lag;

	arm_dot_prod_f32((float32_t *)x, (float32_t *)(x + lag), count, &sum);

	return sum / (float32_t)count;
}

static uint8_t read_hr(float32_t *red_ppg, uint16_t block_size)
{
	uint16_t min_lag = REFRACTORY_SAMPLES;
	uint16_t max_lag = MAX_IBI_SAMPLES;

	// Calculate zero-lag autocorrelation (total signal variance)
	float32_t r0 = autocorr_at_lag(red_ppg, block_size, 0);
	if (r0 <= 1e-6f)
		return 0; // Pure flatline

	static float32_t vals[MAX_IBI_SAMPLES + 1];
	float32_t best_val = -1.0f;
	uint16_t best_lag = 0;

	for (uint16_t lag = min_lag; lag <= max_lag; lag++) {
		float32_t r = autocorr_at_lag(red_ppg, block_size, lag);
		float32_t norm_r = r / r0; // Normalize between -1.0 and 1.0
		vals[lag] = norm_r;

		if (norm_r > best_val) {
			best_val = norm_r;
			best_lag = lag;
		}
	}

	// Reject if correlation peak does not indicate a distinct periodic heartbeat
	if (best_val < HR_MIN_AUTOCORR_PEAK || best_lag == 0) {
		BW_LOG("read_hr: Low peak periodicity (norm_r: %f < %f)\n", best_val, HR_MIN_AUTOCORR_PEAK);
		return 0;
	}

	float32_t refined_lag = (float32_t)best_lag;
	if (best_lag > min_lag && best_lag < max_lag) {
		float32_t y0 = vals[best_lag - 1];
		float32_t y1 = vals[best_lag];
		float32_t y2 = vals[best_lag + 1];
		float32_t denom = (y0 - 2.0f * y1 + y2);

		if (denom != 0.0f) {
			float32_t delta = 0.5f * (y0 - y2) / denom;
			if (delta > 1.0f)
				delta = 1.0f;
			if (delta < -1.0f)
				delta = -1.0f;
			refined_lag = (float32_t)best_lag + delta;
		}
	}

	return (uint8_t)(60.0f * SAMPLE_RATE_HZ / refined_lag);
}

static float32_t read_spo2(float32_t *red_ppg, float32_t *ir_ppg, float32_t red_dc, float32_t ir_dc,
						   uint16_t block_size)
{
	float32_t red_ac = 0.0f;
	float32_t ir_ac = 0.0f;
	arm_rms_f32(red_ppg, block_size, &red_ac);
	arm_rms_f32(ir_ppg, block_size, &ir_ac);

	if (ir_dc <= 1e-4f || ir_ac <= 1e-4f || red_dc <= 1e-4f || red_ac <= 1e-4f) {
		BW_LOG("read_spo2: Weak AC or DC (red_ac=%f, ir_ac=%f)\n", red_ac, ir_ac);
		return 0.0f;
	}

	float32_t r = (red_ac / red_dc) / (ir_ac / ir_dc);
	float32_t spo2 = SPO2_A * r * r + SPO2_B * r + SPO2_C;

	if (spo2 < 70.0f || spo2 > 100.0f) {
		BW_LOG("read_spo2: Result out of physiological bounds: %f%%\n", spo2);
		return 0.0f;
	}

	return spo2;
}

static enum bw_status process_data(enum oxim_mode mode)
{
	uint16_t block_size = mode == OXIM_MODE_HR ? NUM_HR_SAMPLES : NUM_SPO2_SAMPLES;

	// 1. Calculate DC baseline
	float32_t spo2_red_dc = 0.0f;
	float32_t spo2_ir_dc = 0.0f;
	arm_mean_f32(g_red_ppg, block_size, &spo2_red_dc);
	if (mode == OXIM_MODE_SPO2) {
		arm_mean_f32(g_ir_ppg, block_size, &spo2_ir_dc);
	}

	// 2. Reject if sensor is not coupled to skin (ambient/floating check)
	float32_t raw_red_dc_counts = (spo2_red_dc / DATA_SCALE) * ((1 << 18) - 1);
	if (raw_red_dc_counts < PPG_MIN_RAW_DC_COUNTS) {
		BW_LOG("process_data: No finger detected! (Raw DC counts: %f < %f)\n", raw_red_dc_counts,
			   PPG_MIN_RAW_DC_COUNTS);
		return STATUS_ERR;
	}

	if (mode == OXIM_MODE_SPO2) {
		float32_t raw_ir_dc_counts = (spo2_ir_dc / DATA_SCALE) * ((1 << 18) - 1);
		if (raw_ir_dc_counts < PPG_MIN_RAW_DC_COUNTS) {
			BW_LOG("process_data: IR DC counts too low (%f)\n", raw_ir_dc_counts);
			return STATUS_ERR;
		}
	}

	// 3. Remove DC baseline
	arm_offset_f32(g_red_ppg, -spo2_red_dc, g_red_ppg, block_size);
	if (mode == OXIM_MODE_SPO2) {
		arm_offset_f32(g_ir_ppg, -spo2_ir_dc, g_ir_ppg, block_size);
	}

	// 4. Bandpass filter 0.5Hz - 4.0Hz
	bandpass_filters_init(mode);
	bandpass_filters_apply(mode, block_size);

	// 5. Adaptive motion artifact removal
	nlms_filters_apply(g_red_ppg + NUM_SKIP_WARMUP, g_nlms_red_weights, g_ax + NUM_SKIP_WARMUP,
					   g_ay + NUM_SKIP_WARMUP, g_az + NUM_SKIP_WARMUP,
					   block_size - NUM_SKIP_WARMUP);
	if (mode == OXIM_MODE_SPO2) {
		nlms_filters_apply(g_ir_ppg + NUM_SKIP_WARMUP, g_nlms_ir_weights, g_ax + NUM_SKIP_WARMUP,
						   g_ay + NUM_SKIP_WARMUP, g_az + NUM_SKIP_WARMUP,
						   block_size - NUM_SKIP_WARMUP);
	}

	// 6. Verify minimal post-filter AC energy to rule out filtered white noise
	float32_t red_ac_energy = 0.0f;
	arm_rms_f32(g_red_ppg + NUM_SKIP_WARMUP, block_size - NUM_SKIP_WARMUP, &red_ac_energy);
	if (red_ac_energy < PPG_MIN_AC_ENERGY) {
		BW_LOG("process_data: AC pulse energy too low (%f < %f)\n", red_ac_energy,
			   PPG_MIN_AC_ENERGY);
		return STATUS_ERR;
	}

	// 7. Extract metrics
	struct vitals_record rec = { .timestamp = rtc_get_timestamp() };

	if (mode == OXIM_MODE_HR) {
		uint8_t hr = read_hr(g_red_ppg + NUM_SKIP_WARMUP, block_size - NUM_SKIP_WARMUP);
		if (hr == 0) {
			BW_LOG("process_data: read_hr could not confirm pulse\n");
			return STATUS_ERR;
		}
		BW_LOG("Heart rate confirmed: %d bpm\n", hr);
		rec.type = VITALS_TYPE_HR;
		rec.value.hr_bpm = hr;
	} else {
		float32_t spo2 = read_spo2(g_red_ppg + NUM_SKIP_WARMUP, g_ir_ppg + NUM_SKIP_WARMUP,
								   spo2_red_dc, spo2_ir_dc, block_size - NUM_SKIP_WARMUP);
		if (spo2 <= 0.0f) {
			BW_LOG("process_data: read_spo2 invalid\n");
			return STATUS_ERR;
		}
		BW_LOG("SpO2 confirmed: %f%%\n", spo2);
		rec.type = VITALS_TYPE_SPO2;
		rec.value.spo2_pct = spo2;
	}

	// 8. Commit valid metrics to buffer
	kernel_mqueue_overwrite(&g_vitals_mqueue, &rec);

	uint32_t key = KERNEL_ENTER_CRITICAL();
	g_current_vitals = rec;
	KERNEL_EXIT_CRITICAL(key);

	return STATUS_OK;
}

static enum bw_status vitals_run_session(enum oxim_mode mode, int samples)
{
	enum bw_status status = oxim_start_mode(mode);
	if (status != STATUS_OK) {
		BW_LOG("oxim_start_mode exited with status: %d\n", status);
		return status;
	}

	if (mode == OXIM_MODE_SPO2) {
		status = oxim_start_temp_conversion();
		if (status != STATUS_OK) {
			BW_LOG("oxim_start_temp_conversion exited with status: %d\n", status);
			oxim_shutdown();
			return status;
		}
	}

	bool failed = false;
	uint8_t ppg_skip_ctr = 0;
	uint16_t count = 0;
	uint16_t alc_ctr = 0;

	while (count < samples && !failed) {
		uint32_t ntf;
		kernel_task_notify_wait(0, INT_NTF, &ntf, MAX_TIMEOUT);
		if (!(ntf & INT_NTF))
			continue;

		switch (handle_interrupt()) {
		case OXIM_INT_ERR:
		case OXIM_INT_PWR_RDY:
			failed = true;
			continue;

		case OXIM_INT_ALC_OVF:
			if (++alc_ctr > MAX_ALC_CTR) {
				BW_LOG("ALC overflow exceeded retry limit\n");
				failed = true;
			}
			continue;

		case OXIM_INT_DIE_TEMP_RDY: {
			int temp_milli_c;
			if (oxim_read_temp(&temp_milli_c) != STATUS_OK) {
				BW_LOG("oxim_read_temp failed\n");
				failed = true;
			}
			continue;
		}

		case OXIM_INT_PPG_RDY:
			if (ppg_skip_ctr++ < NUM_SKIP_SAMPLES) {
				uint32_t red_sample, ir_sample;
				oxim_read_sample(&red_sample, &ir_sample);
				continue;
			}
			break;
		}

		uint32_t red_sample;
		uint32_t ir_sample;
		status = oxim_read_sample(&red_sample, &ir_sample);
		if (status != STATUS_OK) {
			BW_LOG("oxim_read_sample failed with status: %d\n", status);
			failed = true;
			continue;
		}

		struct acc_sample acc;
		status = imu_read_sample(&acc, NULL);
		if (status != STATUS_OK) {
			BW_LOG("imu_read_sample failed with status: %d\n", status);
			failed = true;
			continue;
		}

		convert_ppg_sample(mode, red_sample, ir_sample, count);
		convert_acc_sample(&acc, count);
		count++;
		BW_LOG("%d\n", count);
	}

	oxim_shutdown();

	if (failed)
		return STATUS_ERR;

	return process_data(mode);
}

void task_vitals(void *user_data)
{
	(void)user_data;

	kernel_mqueue_init(&g_vitals_mqueue, g_vitals_buf, VITALS_QUEUE_CAPACITY,
					   sizeof(struct vitals_record));

	enum bw_status status = oxim_init(OXIM_SMP_AVG_4, OXIM_SMP_RATE_100, oxim_callback);
	if (status != STATUS_OK) {
		BW_LOG("oxim_init exited with status: %d\n", status);
		kernel_task_delete(NULL);
	}

	nlms_filters_init();

	// Wait for IMU initialization
	kernel_event_wait(&g_app_evt, IMU_RDY_EVT, NULL, false, true, MAX_TIMEOUT);

	while (1) {
		uint32_t ntf;
		kernel_task_notify_wait(INT_NTF, VITALS_READ_HR_NTF | VITALS_READ_SPO2_NTF, &ntf,
								MAX_TIMEOUT);
		if (!(ntf & (VITALS_READ_HR_NTF | VITALS_READ_SPO2_NTF)))
			continue;

		bool is_hr = ntf & VITALS_READ_HR_NTF;
		status = vitals_run_session(is_hr ? OXIM_MODE_HR : OXIM_MODE_SPO2,
									is_hr ? NUM_HR_SAMPLES : NUM_SPO2_SAMPLES);

		if (status == STATUS_OK) {
			kernel_task_notify(g_task_ui_h, UI_VIT_CHANGED_NTF, NOTIFY_ACTION_SET_BITS);
			kernel_task_notify(g_task_ble_h, BLE_DRAIN_QUEUE_NTF, NOTIFY_ACTION_SET_BITS);
		} else {
			BW_LOG("Vitals measurement failed\n");
			kernel_task_notify(g_task_ui_h, UI_VIT_ERROR_NTF, NOTIFY_ACTION_SET_BITS);
		}
	}
}

void task_vitals_get_latest_record(struct vitals_record *rec)
{
	if (!rec)
		return;

	uint32_t key = KERNEL_ENTER_CRITICAL();
	*rec = g_current_vitals;
	KERNEL_EXIT_CRITICAL(key);
}

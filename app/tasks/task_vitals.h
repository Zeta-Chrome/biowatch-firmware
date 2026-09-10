#ifndef APP_TASK_VITALS
#define APP_TASK_VITALS

#include "kernel/kernel.h"
#include "lib/utils.h"

#define VITALS_READ_HR_NTF BIT(1)
#define VITALS_READ_SPO2_NTF BIT(2)

enum vitals_type : uint32_t { VITALS_TYPE_HR, VITALS_TYPE_SPO2 };

struct vitals_record {
	uint32_t timestamp;
	enum vitals_type type;
	union {
		uint8_t hr_bpm;
		float spo2_pct;
	} value;
};

extern task_handle_t g_task_vitals_h;
extern struct mqueue g_vitals_mqueue;

void task_vitals(void *user_data);
void task_vitals_get_latest_record(struct vitals_record *rec);

#endif

#ifndef APP_TASK_ACTIVITY
#define APP_TASK_ACTIVITY

#include "kernel/kernel.h"
#include "lib/utils.h"

#define ACT_NEW_REC_NTF BIT(1)

extern task_handle_t g_task_act_h;
extern struct mqueue g_act_mqueue;

struct act_record {
	uint32_t timestamp;
	uint32_t dur_ms;
	float distance_m;
	float speed_ms;
	float calories_kcal;
	uint16_t steps;
	uint16_t reserved;
};

void task_act(void *user_data);
void task_act_get_latest_record(struct act_record *rec);

#endif

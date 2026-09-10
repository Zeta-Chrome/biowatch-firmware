#ifndef APP_TASK_ENV
#define APP_TASK_ENV

#include "kernel/kernel.h"

struct env_record {
	uint32_t timestamp;
	uint16_t rhx100;
	int16_t tempx100;
	uint16_t luxx100;
	uint16_t reserved;
};

extern task_handle_t g_task_env_h;
extern struct mqueue g_env_mqueue;

void task_env(void *user_data);
void task_env_get_latest_record(struct env_record *rec);

#endif

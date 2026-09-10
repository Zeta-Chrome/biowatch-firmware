#ifndef APP_TASK_HAPTICS
#define APP_TASK_HAPTICS

#include "kernel/kernel.h"

#define HAPTICS_BUZZ_NTF BIT(0)
#define HAPTICS_VIB_NTF BIT(1)

extern task_handle_t g_task_hap_h;

void task_haptics(void *user_data);

#endif

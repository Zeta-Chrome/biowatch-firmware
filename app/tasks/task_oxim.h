#ifndef APP_TASK_OXIM
#define APP_TASK_OXIM

#include "kernel/kernel.h"
#include "lib/utils.h"

#define OXIM_READ_HR_NTF BIT(1)
#define OXIM_READ_SPO2_NTF BIT(2)

extern task_handle_t g_oxim_task_h;

void oxim_task(void *user_data);

#endif

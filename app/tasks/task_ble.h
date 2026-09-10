#ifndef APP_TASK_BLE
#define APP_TASK_BLE

#include "kernel/task/task.h"
#include "lib/utils.h"

#define BLE_START_NTF BIT(0)
#define BLE_START_LP_ADV_NTF BIT(1)
#define BLE_ACT_CHANGED_NTF BIT(2)
#define BLE_VIT_CHANGED_NTF BIT(3)
#define BLE_ENV_CHANGED_NTF BIT(4)
#define BLE_DRAIN_QUEUE_NTF BIT(5)
#define BLE_STOP_NTF BIT(6)

extern task_handle_t g_task_ble_h;

void task_ble(void *user_data);

#endif

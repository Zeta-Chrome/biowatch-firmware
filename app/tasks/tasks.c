#include "kernel/sync/event.h"
#include "task_act.h"
#include "task_haptics.h"
#include "task_env.h"
#include "task_ble.h"
#include "task_ui.h"
#include "task_vitals.h"
#include "tasks.h"

struct event g_app_evt;

void tasks_create()
{
	kernel_event_init(&g_app_evt);
	kernel_task_create(task_ui, "UI Task", PRIO_UI, STACK_UI, NULL, &g_task_ui_h);
	kernel_task_create(task_ble, "BLE Task", PRIO_BLE, STACK_BLE, NULL, &g_task_ble_h);
	kernel_task_create(task_act, "Activity Task", PRIO_ACT, STACK_ACT, NULL, &g_task_act_h);
	kernel_task_create(task_vitals, "Vitals Task", PRIO_VIT, STACK_VIT, NULL, &g_task_vitals_h);
	kernel_task_create(task_env, "Env Task", PRIO_ENV, STACK_ENV, NULL, &g_task_env_h);
	kernel_task_create(task_haptics, "Haptics Task", PRIO_HAP, STACK_HAP, NULL, &g_task_hap_h);
}

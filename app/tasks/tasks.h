#ifndef APP_TASKS_H
#define APP_TASKS_H

#define IMU_RDY_EVT BIT(0)

// Task priorities
#define PRIO_UI 1
#define PRIO_BLE 4
#define PRIO_ACT 0
#define PRIO_VIT 2
#define PRIO_ENV 3
#define PRIO_HAP 5

// Task stack sizes
#define STACK_UI 256
#define STACK_BLE 256
#define STACK_ACT 128
#define STACK_VIT 256
#define STACK_ENV 128
#define STACK_HAP 64

extern struct event g_app_evt;

void tasks_create();

#endif

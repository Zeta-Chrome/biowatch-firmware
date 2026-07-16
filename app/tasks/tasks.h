#ifndef APP_TASKS_H
#define APP_TASKS_H

// Task priorities
#define PRIO_IMU 0
#define PRIO_OXIM 1

// Task stack sizes
#define STACK_IMU 128
#define STACK_OXIM 256

void tasks_create();

#endif

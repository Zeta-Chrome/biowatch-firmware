#include "task_haptics.h"
#include "biowatch/bsp.h"
#include "drivers/gpio/gpio.h"
#include "kernel/task/task.h"
#include "kernel/timer.h"
#include "lib/utils.h"

#define BUZZ_PERIOD_MS 1
#define VIB_PERIOD_MS 2000

task_handle_t g_task_hap_h;

static void stop_buzz(void *user_data);
static void stop_vib(void *user_data);

struct kernel_timer g_buzz_timer = { .type = KERNEL_TIMER_ONE_SHOT,
									 .ticks = BUZZ_PERIOD_MS,
									 .callback = stop_buzz,
									 .user_data = NULL };

struct kernel_timer g_vib_timer = { .type = KERNEL_TIMER_ONE_SHOT,
									.ticks = VIB_PERIOD_MS,
									.callback = stop_vib,
									.user_data = NULL };

static void stop_buzz(void *user_data)
{
	(void)user_data;
	gpio_set_level(PL_BUZZ_PIN, 0);
}

static void stop_vib(void *user_data)
{
	(void)user_data;
	gpio_set_level(PL_VIB_PIN, 0);
}

void task_haptics(void *user_data)
{
	(void)user_data;

	// BUZZ init
	struct gpio_conf conf = gpio_conf_output(PL_BUZZ_PIN, GPIO_SPEED_LOW);
	gpio_init(&conf);
	kernel_timer_register(&g_buzz_timer);

	// VIB init
	conf = gpio_conf_output(PL_VIB_PIN, GPIO_SPEED_LOW);
	gpio_init(&conf);
	kernel_timer_register(&g_vib_timer);

	uint32_t ntf;
	while (1) {
		kernel_task_notify_wait(0, UINT32_MAX, &ntf, MAX_TIMEOUT);

		if (ntf & HAPTICS_BUZZ_NTF) {
			gpio_set_level(PL_BUZZ_PIN, 1);
			kernel_timer_start(&g_buzz_timer);
		}

		if (ntf & HAPTICS_VIB_NTF) {
			gpio_set_level(PL_VIB_PIN, 1);
			kernel_timer_start(&g_vib_timer);
		}
	}
}

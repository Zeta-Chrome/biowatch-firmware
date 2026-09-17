#include "app/tasks/tasks.h"
#include "biowatch/bsp.h"
#include "kernel/kernel.h"
#include "kernel/task/mem.h"
#include "subsys/lpm/lpm.h"

static void idle_hook(void *user_data)
{
	(void)user_data;
	lpm_enter_mode();
}

int main()
{
	bsp_init();

	struct kernel_conf conf = { .pool_confs = { { .sz = MEM_BLOCK_SZ_2048, .count = 0 },
												{ .sz = MEM_BLOCK_SZ_1024, .count = 6 },
												{ .sz = MEM_BLOCK_SZ_512, .count = 2 },
												{ .sz = MEM_BLOCK_SZ_256, .count = 1 } },
								.idle_hook = idle_hook,
								.idle_task_size = 256,
								.idle_data = NULL };
	kernel_init(&conf);
	tasks_create();
	kernel_start();
}

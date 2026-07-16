#include "app/tasks/tasks.h"
#include "biowatch/bsp.h"
#include "kernel/kernel.h"

static void idle_hook(void *user_data)
{
    (void)user_data;
}

int main()
{
    bsp_init();

    kernel_conf_t conf = {.pool_confs = {{.sz = MEM_BLOCK_SZ_2048, .count = 2},
                                         {.sz = MEM_BLOCK_SZ_1024, .count = 4},
                                         {.sz = MEM_BLOCK_SZ_512, .count = 4},
                                         {.sz = MEM_BLOCK_SZ_256, .count = 4},
                                         {.sz = MEM_BLOCK_SZ_128, .count = 4}},
                          .idle_hook = idle_hook,
                          .idle_data = NULL};
    kernel_init(&conf);
    tasks_create();
    kernel_start();
}

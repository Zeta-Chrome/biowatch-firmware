#ifndef APP_TASK_UI
#define APP_TASK_UI

#include "drivers/exti/exti.h"
#include "kernel/task/task.h"

#define UI_DRAW_NTF BIT(0)
#define UI_WUT_1HZ_NTF BIT(1)
#define UI_ACT_CHANGED_NTF BIT(2)
#define UI_VIT_CHANGED_NTF BIT(3)
#define UI_ENV_CHANGED_NTF BIT(4)
#define UI_BLE_CONNECTED_NTF BIT(5)
#define UI_BLE_ADVERTISING_NTF BIT(6)
#define UI_BLE_DISCONNECTED_NTF BIT(7)
#define UI_SET_CHANGED_NTF BIT(8)

extern task_handle_t g_task_ui_h;

void task_ui(void *user_data);
struct exti_handle *ui_get_next_btn_handle(exti_callback_t *callback);
struct exti_handle *ui_get_click_btn_handle(exti_callback_t *callback);
struct exti_handle *ui_get_home_btn_handle(exti_callback_t *callback);

#endif

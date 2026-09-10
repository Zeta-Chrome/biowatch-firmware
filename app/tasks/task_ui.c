#include "task_ui.h"
#include "app/settings.h"
#include "app/tasks/task_act.h"
#include "app/tasks/task_ble.h"
#include "app/tasks/task_env.h"
#include "app/tasks/task_haptics.h"
#include "drivers/display/display.h"
#include "kernel/task/task.h"
#include "kernel/timer.h"
#include "lib/logger.h"
#include "stdbool.h"
#include "subsys/ui/widget.h"
#include "task_vitals.h"
#include "assets/fonts/tamzen12b.h"
#include "assets/fonts/tamzen9.h"
#include "drivers/gpio/gpio.h"
#include "drivers/rtc/rtc.h"
#include "kernel/critical.h"
#include "subsys/ui/ui.h"
#include "biowatch/bsp.h"
#include "kernel/kernel.h"
#include <stdio.h>

#define BTN_DEBOUNCE 30
#define BTN_REPEAT_MS 250
#define BTN_FAST_REPEAT_MS 90
#define NEXT_BTN BIT(0)
#define CLICK_BTN BIT(1)
#define PARENT_BTN (NEXT_BTN | CLICK_BTN)
#define FPS 30

task_handle_t g_task_ui_h;
static struct ui_widget *g_root_stack;

// Buttons
enum ui_button { UI_BTN_HOME, UI_BTN_CLICK, UI_BTN_NEXT, UI_BTN_COUNT };
static const struct gpio g_btn_pin[UI_BTN_COUNT] = { PL_HOME_PIN, PL_CLICK_PIN, PL_NEXT_PIN };
static struct exti_handle g_btn_exti[UI_BTN_COUNT];
static struct kernel_timer g_btn_timer[UI_BTN_COUNT];

// flat index of every page in g_root_stack
enum root_stack_idx {
	ROOT_IDX_STARTUP,
	ROOT_IDX_APP_HOME,
	ROOT_IDX_CLOCK,
	ROOT_IDX_STEPS,
	ROOT_IDX_VITALS,
	ROOT_IDX_WEATHER,
	ROOT_IDX_BLE,
	ROOT_IDX_SETTINGS,
};

enum header_stack_idx {
	HEADER_IDX_CLOCK,
	HEADER_IDX_STEPS,
	HEADER_IDX_VITALS,
	HEADER_IDX_WEATHER,
	HEADER_IDX_BLE,
	HEADER_IDX_SETTINGS,
};

_Static_assert(ROOT_IDX_CLOCK + HEADER_IDX_SETTINGS == ROOT_IDX_SETTINGS,
			   "root_stack_idx and header_stack_idx have drifted out of sync");

typedef enum {
	FIELD_TYPE_INT,
	FIELD_TYPE_MERIDIEM,
	FIELD_TYPE_DAY,
} clock_field_type_t;

typedef struct {
	struct ui_widget **widget_ref;
	clock_field_type_t type;
	uint16_t *val;
	uint16_t min;
	uint16_t max;
	const char *fmt;
	char buf[8];
} clock_field_desc_t;

static struct {
	struct ui_widget *root;
	struct ui_widget *logo;
	struct ui_widget *title;
} g_startup_page;

static struct {
	struct ui_widget *root;
	struct ui_widget *header_stack;
	struct ui_widget *clock_header;
	struct ui_widget *activity_header;
	struct ui_widget *vitals_header;
	struct ui_widget *weather_header;
	struct ui_widget *ble_header;
	struct ui_widget *settings_header;
	struct ui_widget *app_grid;
	struct ui_widget *clock_app;
	struct ui_widget *steps_app;
	struct ui_widget *vitals_app;
	struct ui_widget *weather_app;
	struct ui_widget *ble_app;
	struct ui_widget *settings_app;

	char clock_buf[32];
	char act_buf[32];
	char vitals_buf[32];
	char weather_buf[32];
} g_app_page;

static struct {
	struct ui_widget *root;

	struct ui_widget *time_edit_row;
	struct ui_widget *time_icon;
	struct ui_widget *hour_edit;
	struct ui_widget *minute_edit;
	struct ui_widget *meridiem_edit;

	struct ui_widget *date_edit_row;
	struct ui_widget *date_icon;
	struct ui_widget *month_edit;
	struct ui_widget *day_edit;
	struct ui_widget *year_edit;

	struct ui_widget *timer_row;
	struct ui_widget *timer_icon;
	struct ui_widget *timer_display;
	struct ui_widget *timer_toggle_btn;
	struct ui_widget *timer_toggle_btn_text;

	uint16_t hour;
	uint16_t minute;
	uint16_t second;
	uint16_t meridiem; // 0 = AM, 1 = PM
	uint16_t month;
	uint16_t day;
	uint16_t year;
	uint16_t timer_h;
	uint16_t timer_m;
	uint16_t timer_s;
	bool timer_running;

	char timer_display_buf[20];
} g_clock_page;

static struct {
	struct ui_widget *root;
	struct ui_widget *act_grid;
	struct ui_widget *steps;
	struct ui_widget *duration;
	struct ui_widget *distance;
	struct ui_widget *speed;
	struct ui_widget *calories;

	char steps_buf[16];
	char dur_buf[16];
	char dist_buf[16];
	char speed_buf[16];
	char cal_buf[16];
} g_act_page;

enum vitals_measuring { VITALS_IDLE, VITALS_MEASURING_HR, VITALS_MEASURING_SPO2 };

static struct {
	struct ui_widget *root;
	struct ui_widget *hr_btn;
	struct ui_widget *hr_icon;
	struct ui_widget *hr_text;
	struct ui_widget *spo2_btn;
	struct ui_widget *spo2_icon;
	struct ui_widget *spo2_text;

	char hr_buf[16];
	char spo2_buf[16];

	enum vitals_measuring measuring;
} g_vitals_page;

static struct {
	struct ui_widget *root;
	struct ui_widget *temp;
	struct ui_widget *humidity;
	struct ui_widget *lux;

	char temp_buf[16];
	char hum_buf[16];
	char lux_buf[16];
} g_weather_page;

enum ble_state { BLE_STATE_OFF, BLE_STATE_ADVERTISING, BLE_STATE_CONNECTED };

static struct {
	struct ui_widget *root;
	struct ui_widget *conn_btn;
	struct ui_widget *conn_btn_text;
	struct ui_widget *status;

	char btn_buf[16];
	char status_buf[16];
	enum ble_state state;
} g_ble_page = { .btn_buf = "Enable BLE", .status_buf = "Not Connected", .state = BLE_STATE_OFF };

static struct {
	struct ui_widget *root;
	struct ui_widget *manuf_name;
	struct ui_widget *fw;
	struct ui_widget *height;
	struct ui_widget *weight;

	char fw_buf[32];
	char height_buf[32];
	char weight_buf[32];
} g_settings_page = {
	.fw_buf = "Biowatch 0.0.0",
	.height_buf = "Height:   170 cm",
	.weight_buf = "Weight:    70 kg",
};

static clock_field_desc_t g_clock_fields[] = {
	{ &g_clock_page.hour_edit, FIELD_TYPE_INT, &g_clock_page.hour, 1, 12, "%02d", "12" },
	{ &g_clock_page.minute_edit, FIELD_TYPE_INT, &g_clock_page.minute, 0, 59, "%02d", "00" },
	{ &g_clock_page.meridiem_edit, FIELD_TYPE_MERIDIEM, &g_clock_page.meridiem, 0, 1, NULL, "AM" },
	{ &g_clock_page.month_edit, FIELD_TYPE_INT, &g_clock_page.month, 1, 12, "%02d", "01" },
	{ &g_clock_page.day_edit, FIELD_TYPE_DAY, &g_clock_page.day, 1, 31, "%02d", "01" },
	{ &g_clock_page.year_edit, FIELD_TYPE_INT, &g_clock_page.year, 2024, 2099, "%04d", "2026" },
};
#define NUM_CLOCK_FIELDS (sizeof(g_clock_fields) / sizeof(g_clock_fields[0]))

static inline void request_draw(void)
{
	kernel_task_notify(g_task_ui_h, UI_DRAW_NTF, NOTIFY_ACTION_SET_BITS);
}

static inline void repeat_btn(enum ui_button btn, uint32_t ms)
{
	g_btn_timer[btn].ticks = ms;
	kernel_timer_start(&g_btn_timer[btn]);
}

static void btn_exti(void *user_data)
{
	enum ui_button btn = (enum ui_button)user_data;
	struct kernel_timer *timer = &g_btn_timer[btn];

	if (timer->active)
		return;

	KERNEL_ENTER_CRITICAL();
	if (gpio_read_level(g_btn_pin[btn]) == 0) {
		timer->ticks = BTN_DEBOUNCE;
		kernel_timer_start(timer);
	} else {
		kernel_timer_stop(timer);
	}
	KERNEL_EXIT_CRITICAL();
}

static void on_press(void *user_data)
{
	enum ui_button btn = (enum ui_button)user_data;

	if (gpio_read_level(g_btn_pin[btn]) != 0)
		return;

	kernel_task_notify(g_task_hap_h, HAPTICS_BUZZ_NTF, NOTIFY_ACTION_SET_BITS);

	switch (btn) {
	case UI_BTN_HOME:
		ui_change_page(ROOT_IDX_APP_HOME);
		ui_widget_set_active_child(g_app_page.header_stack, 0);
		request_draw();
		break;

	case UI_BTN_CLICK:
		ui_click_selection();
		struct ui_widget *sel = ui_get_selected();
		if (sel == g_clock_page.hour_edit || sel == g_clock_page.minute_edit ||
			sel == g_clock_page.day_edit || sel == g_clock_page.month_edit ||
			sel == g_clock_page.year_edit) {
			g_btn_timer[UI_BTN_CLICK].ticks = BTN_FAST_REPEAT_MS;
			kernel_timer_start(&g_btn_timer[UI_BTN_CLICK]);
		}
		request_draw();
		break;

	case UI_BTN_NEXT:
		ui_select_next();
		repeat_btn(UI_BTN_NEXT, BTN_REPEAT_MS);
		request_draw();
		break;

	default:
		break;
	}
}

static void rtc_wut_1hz_isr(void)
{
	kernel_task_notify(g_task_ui_h, UI_WUT_1HZ_NTF, NOTIFY_ACTION_SET_BITS);
}

static void task_ui_init(void)
{
	for (int btn = 0; btn < UI_BTN_COUNT; btn++) {
		g_btn_timer[btn].type = KERNEL_TIMER_ONE_SHOT;
		g_btn_timer[btn].ticks = BTN_DEBOUNCE;
		g_btn_timer[btn].callback = on_press;
		g_btn_timer[btn].user_data = (void *)btn;
	}

	rtc_enable_wut(1, rtc_wut_1hz_isr, 4);
}

static void build_startup_page(void)
{
	g_startup_page.root = ui_widget_create_col(0x00, 0, 1, 1, false);
	g_startup_page.logo = ui_widget_create_image(&logo_bmp, false, 4, false);
	g_startup_page.title = ui_widget_create_text("BIOWATCH", &tamzen12b, true, 1, false);

	ui_container_add_child(g_root_stack, g_startup_page.root);
	ui_container_add_child(g_startup_page.root, g_startup_page.logo);
	ui_container_add_child(g_startup_page.root, g_startup_page.title);
}

static void on_select_app(void *user_data)
{
	enum header_stack_idx app_id = (enum header_stack_idx)user_data;
	ui_widget_set_active_child(g_app_page.header_stack, app_id);
	request_draw();
}

static void on_click_app(void *user_data)
{
	enum header_stack_idx app_id = (enum header_stack_idx)user_data;
	ui_widget_set_active_child(g_root_stack, ROOT_IDX_CLOCK + app_id);
	ui_unselect();
	request_draw();
}

static uint8_t get_max_days(uint16_t year, uint16_t month)
{
	static const uint8_t days_per_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month < 1 || month > 12)
		return 31;
	if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
		return 29;
	return days_per_month[month - 1];
}

static uint8_t calculate_weekday(uint8_t yr_2digit, uint8_t mth, uint8_t dte)
{
	int y = 2000 + yr_2digit;
	int m = mth;
	int d = dte;
	if (m < 3) {
		m += 12;
		y -= 1;
	}
	int k = y % 100;
	int j = y / 100;
	int h = (d + (13 * (m + 1)) / 5 + k + (k / 4) + (j / 4) + (5 * j)) % 7;
	return (uint8_t)(((h + 5) % 7) + 1);
}

static void update_clock_header_text(void)
{
	bw_str_format(g_app_page.clock_buf, sizeof(g_app_page.clock_buf),
				  "%02u:%02u %s   %02u/%02u/%02u", g_clock_page.hour, g_clock_page.minute,
				  g_clock_page.meridiem ? "PM" : "AM", g_clock_page.month, g_clock_page.day,
				  g_clock_page.year % 100);

	if (g_app_page.clock_header) {
		ui_widget_update_text(g_app_page.clock_header, g_app_page.clock_buf);
		request_draw();
	}
}

static void build_app_page(void)
{
	g_app_page.root = ui_widget_create_col(0xFF, 0, 2, 1, false);
	g_app_page.header_stack = ui_widget_create_stack(0x00, 0, 2, false);

	update_clock_header_text();
	bw_str_format(g_app_page.act_buf, sizeof(g_app_page.act_buf), "0 steps    0 m");
	bw_str_format(g_app_page.vitals_buf, sizeof(g_app_page.vitals_buf),
				  "HR: -- bpm  SPO2: --.- %%");
	bw_str_format(g_app_page.weather_buf, sizeof(g_app_page.weather_buf),
				  "Temp: --.- C  Hum: --.- %%");

	g_app_page.clock_header =
		ui_widget_create_text(g_app_page.clock_buf, &tamzen9, false, 2, false);
	g_app_page.activity_header =
		ui_widget_create_text(g_app_page.act_buf, &tamzen9, false, 2, false);
	g_app_page.vitals_header =
		ui_widget_create_text(g_app_page.vitals_buf, &tamzen9, false, 2, false);
	g_app_page.weather_header =
		ui_widget_create_text(g_app_page.weather_buf, &tamzen9, false, 2, false);
	g_app_page.ble_header = ui_widget_create_text("Not Connected", &tamzen9, false, 1, false);
	g_app_page.settings_header =
		ui_widget_create_text(g_settings_page.fw_buf, &tamzen9, false, 1, false);
	g_app_page.app_grid = ui_widget_create_grid(0x00, 2, 2, 2, 3, 5, false);

	g_app_page.clock_app = ui_widget_create_image(&clock_bmp, false, 1, true);
	ui_widget_set_callbacks(g_app_page.clock_app, on_select_app, on_click_app,
							(void *)HEADER_IDX_CLOCK);

	g_app_page.steps_app = ui_widget_create_image(&steps_bmp, false, 1, true);
	ui_widget_set_callbacks(g_app_page.steps_app, on_select_app, on_click_app,
							(void *)HEADER_IDX_STEPS);

	g_app_page.vitals_app = ui_widget_create_image(&vitals_bmp, false, 1, true);
	ui_widget_set_callbacks(g_app_page.vitals_app, on_select_app, on_click_app,
							(void *)HEADER_IDX_VITALS);

	g_app_page.weather_app = ui_widget_create_image(&weather_bmp, false, 1, true);
	ui_widget_set_callbacks(g_app_page.weather_app, on_select_app, on_click_app,
							(void *)HEADER_IDX_WEATHER);

	g_app_page.ble_app = ui_widget_create_image(&ble_bmp, false, 1, true);
	ui_widget_set_callbacks(g_app_page.ble_app, on_select_app, on_click_app,
							(void *)HEADER_IDX_BLE);

	g_app_page.settings_app = ui_widget_create_image(&settings_bmp, false, 1, true);
	ui_widget_set_callbacks(g_app_page.settings_app, on_select_app, on_click_app,
							(void *)HEADER_IDX_SETTINGS);

	ui_container_add_child(g_root_stack, g_app_page.root);
	ui_container_add_child(g_app_page.root, g_app_page.header_stack);
	ui_container_add_child(g_app_page.header_stack, g_app_page.clock_header);
	ui_container_add_child(g_app_page.header_stack, g_app_page.activity_header);
	ui_container_add_child(g_app_page.header_stack, g_app_page.vitals_header);
	ui_container_add_child(g_app_page.header_stack, g_app_page.weather_header);
	ui_container_add_child(g_app_page.header_stack, g_app_page.ble_header);
	ui_container_add_child(g_app_page.header_stack, g_app_page.settings_header);
	ui_container_add_child(g_app_page.root, g_app_page.app_grid);
	ui_container_add_child(g_app_page.app_grid, g_app_page.clock_app);
	ui_container_add_child(g_app_page.app_grid, g_app_page.steps_app);
	ui_container_add_child(g_app_page.app_grid, g_app_page.vitals_app);
	ui_container_add_child(g_app_page.app_grid, g_app_page.weather_app);
	ui_container_add_child(g_app_page.app_grid, g_app_page.ble_app);
	ui_container_add_child(g_app_page.app_grid, g_app_page.settings_app);
}

static void sync_rtc_from_clock_val(void)
{
	bool pm = (g_clock_page.meridiem != 0);
	rtc_set_time((uint8_t)g_clock_page.hour, (uint8_t)g_clock_page.minute,
				 (uint8_t)g_clock_page.second, pm);

	uint8_t yr_2digit = (uint8_t)(g_clock_page.year % 100);
	uint8_t wd =
		calculate_weekday(yr_2digit, (uint8_t)g_clock_page.month, (uint8_t)g_clock_page.day);
	rtc_set_date(yr_2digit, (uint8_t)g_clock_page.month, (uint8_t)g_clock_page.day, wd);

	update_clock_header_text();
}

static void read_rtc_to_clock_val(void)
{
	uint8_t hr = 12, min = 0, sec = 0;
	bool pm = false;
	uint8_t yr = 26, mth = 1, dte = 1, wd = 1;

	rtc_get_time(&hr, &min, &sec, &pm);
	rtc_get_date(&yr, &mth, &dte, &wd);

	g_clock_page.hour = (hr == 0) ? 12 : hr;
	g_clock_page.minute = min;
	g_clock_page.second = sec;
	g_clock_page.meridiem = pm ? 1 : 0;
	g_clock_page.month = (mth >= 1 && mth <= 12) ? mth : 1;
	g_clock_page.day = (dte >= 1 && dte <= 31) ? dte : 1;
	g_clock_page.year = 2000 + yr;

	for (size_t i = 0; i < NUM_CLOCK_FIELDS; i++) {
		clock_field_desc_t *f = &g_clock_fields[i];
		if (f->type == FIELD_TYPE_MERIDIEM) {
			bw_str_format(f->buf, sizeof(f->buf), "%s", (*f->val) ? "PM" : "AM");
		} else {
			bw_str_format(f->buf, sizeof(f->buf), f->fmt, *f->val);
		}
		if (f->widget_ref && *f->widget_ref)
			ui_widget_update_text(*f->widget_ref, f->buf);
	}

	update_clock_header_text();
}

static void update_timer_display(void)
{
	bw_str_format(g_clock_page.timer_display_buf, sizeof(g_clock_page.timer_display_buf),
				  "%02u:%02u:%02u", g_clock_page.timer_h, g_clock_page.timer_m,
				  g_clock_page.timer_s);

	if (g_clock_page.timer_display) {
		ui_widget_update_text(g_clock_page.timer_display, g_clock_page.timer_display_buf);
		request_draw();
	}
}

static void on_clock_field_click(void *user_data)
{
	clock_field_desc_t *f = (clock_field_desc_t *)user_data;
	if (!f || !f->val || !f->widget_ref || !(*f->widget_ref))
		return;

	uint16_t max_val =
		(f->type == FIELD_TYPE_DAY) ? get_max_days(g_clock_page.year, g_clock_page.month) : f->max;

	(*f->val)++;
	if (*f->val > max_val) {
		*f->val = f->min;
	}

	if (f->type == FIELD_TYPE_MERIDIEM) {
		bw_str_format(f->buf, sizeof(f->buf), "%s", (*f->val) ? "PM" : "AM");
	} else {
		bw_str_format(f->buf, sizeof(f->buf), f->fmt, *f->val);
	}

	ui_widget_update_text(*f->widget_ref, f->buf);

	// Re-clamp day if month/year adjusted
	if (f->widget_ref == &g_clock_page.month_edit || f->widget_ref == &g_clock_page.year_edit) {
		uint8_t dynamic_max = get_max_days(g_clock_page.year, g_clock_page.month);
		if (g_clock_page.day > dynamic_max) {
			g_clock_page.day = dynamic_max;
			for (size_t i = 0; i < NUM_CLOCK_FIELDS; i++) {
				if (g_clock_fields[i].widget_ref == &g_clock_page.day_edit) {
					bw_str_format(g_clock_fields[i].buf, sizeof(g_clock_fields[i].buf),
								  g_clock_fields[i].fmt, g_clock_page.day);
					ui_widget_update_text(*g_clock_fields[i].widget_ref, g_clock_fields[i].buf);
					break;
				}
			}
		}
	}

	sync_rtc_from_clock_val();
	request_draw();
}

static void timer_display_on_click(void *user_data)
{
	(void)user_data;
	g_clock_page.timer_h = 0;
	g_clock_page.timer_m = 0;
	g_clock_page.timer_s = 0;
	update_timer_display();
}

static void timer_toggle_btn_on_click(void *user_data)
{
	(void)user_data;
	g_clock_page.timer_running = !g_clock_page.timer_running;
	ui_widget_update_text(g_clock_page.timer_toggle_btn_text,
						  g_clock_page.timer_running ? "STOP" : "START");
	request_draw();
}

static void build_clock_page(void)
{
	bw_str_format(g_clock_page.timer_display_buf, sizeof(g_clock_page.timer_display_buf),
				  "00:00:00");

	g_clock_page.root = ui_widget_create_col(0x00, 2, 2, 1, false);

	// Time row: HH : MM  AM/PM
	g_clock_page.time_edit_row = ui_widget_create_row(0x00, 2, 1, 1, false);
	g_clock_page.time_icon = ui_widget_create_image(&time_bmp, false, 2, false);
	g_clock_page.hour_edit =
		ui_widget_create_text(g_clock_fields[0].buf, &tamzen12b, true, 2, true);
	struct ui_widget *time_colon = ui_widget_create_text(":", &tamzen12b, true, 1, false);
	g_clock_page.minute_edit =
		ui_widget_create_text(g_clock_fields[1].buf, &tamzen12b, true, 2, true);
	g_clock_page.meridiem_edit =
		ui_widget_create_text(g_clock_fields[2].buf, &tamzen12b, true, 2, true);

	// Date row: MM / DD / YYYY
	g_clock_page.date_edit_row = ui_widget_create_row(0x00, 2, 1, 1, false);
	g_clock_page.date_icon = ui_widget_create_image(&calendar_bmp, false, 2, false);
	g_clock_page.month_edit =
		ui_widget_create_text(g_clock_fields[3].buf, &tamzen12b, true, 2, true);
	struct ui_widget *date_slash1 = ui_widget_create_text("/", &tamzen12b, true, 1, false);
	g_clock_page.day_edit = ui_widget_create_text(g_clock_fields[4].buf, &tamzen12b, true, 2, true);
	struct ui_widget *date_slash2 = ui_widget_create_text("/", &tamzen12b, true, 1, false);
	g_clock_page.year_edit =
		ui_widget_create_text(g_clock_fields[5].buf, &tamzen12b, true, 3, true);

	// Timer row: [ICON]  HH:MM:SS  [START/STOP]
	g_clock_page.timer_row = ui_widget_create_row(0x00, 2, 1, 1, false);
	g_clock_page.timer_icon = ui_widget_create_image(&timer_bmp, false, 2, false);
	g_clock_page.timer_display =
		ui_widget_create_text(g_clock_page.timer_display_buf, &tamzen12b, true, 5, true);
	g_clock_page.timer_toggle_btn =
		ui_widget_create_button("START", &tamzen9, &g_clock_page.timer_toggle_btn_text, 3);

	for (size_t i = 0; i < NUM_CLOCK_FIELDS; i++) {
		if (*g_clock_fields[i].widget_ref) {
			ui_widget_set_callbacks(*g_clock_fields[i].widget_ref, NULL, on_clock_field_click,
									&g_clock_fields[i]);
		}
	}

	ui_widget_set_callbacks(g_clock_page.timer_display, NULL, timer_display_on_click, NULL);
	ui_widget_set_callbacks(g_clock_page.timer_toggle_btn, NULL, timer_toggle_btn_on_click, NULL);

	ui_container_add_child(g_root_stack, g_clock_page.root);
	ui_container_add_child(g_clock_page.root, g_clock_page.time_edit_row);
	ui_container_add_child(g_clock_page.time_edit_row, g_clock_page.time_icon);
	ui_container_add_child(g_clock_page.time_edit_row, g_clock_page.hour_edit);
	ui_container_add_child(g_clock_page.time_edit_row, time_colon);
	ui_container_add_child(g_clock_page.time_edit_row, g_clock_page.minute_edit);
	ui_container_add_child(g_clock_page.time_edit_row, g_clock_page.meridiem_edit);

	ui_container_add_child(g_clock_page.root, g_clock_page.date_edit_row);
	ui_container_add_child(g_clock_page.date_edit_row, g_clock_page.date_icon);
	ui_container_add_child(g_clock_page.date_edit_row, g_clock_page.month_edit);
	ui_container_add_child(g_clock_page.date_edit_row, date_slash1);
	ui_container_add_child(g_clock_page.date_edit_row, g_clock_page.day_edit);
	ui_container_add_child(g_clock_page.date_edit_row, date_slash2);
	ui_container_add_child(g_clock_page.date_edit_row, g_clock_page.year_edit);

	ui_container_add_child(g_clock_page.root, g_clock_page.timer_row);
	ui_container_add_child(g_clock_page.timer_row, g_clock_page.timer_icon);
	ui_container_add_child(g_clock_page.timer_row, g_clock_page.timer_display);
	ui_container_add_child(g_clock_page.timer_row, g_clock_page.timer_toggle_btn);
}

static void build_act_page(void)
{
	bw_str_format(g_act_page.steps_buf, sizeof(g_act_page.steps_buf), "0 steps");
	bw_str_format(g_act_page.dur_buf, sizeof(g_act_page.dur_buf), "00:00");
	bw_str_format(g_act_page.dist_buf, sizeof(g_act_page.dist_buf), "0 m");
	bw_str_format(g_act_page.speed_buf, sizeof(g_act_page.speed_buf), "0.0 km/h");
	bw_str_format(g_act_page.cal_buf, sizeof(g_act_page.cal_buf), "0 kcal");

	g_act_page.root = ui_widget_create_col(0xFF, 0, 1, 1, false);
	g_act_page.act_grid = ui_widget_create_grid(0xFF, 0, 2, 2, 2, 2, false);
	g_act_page.steps = ui_widget_create_text(g_act_page.steps_buf, &tamzen9, false, 1, false);
	g_act_page.duration = ui_widget_create_text(g_act_page.dur_buf, &tamzen9, false, 1, false);
	g_act_page.distance = ui_widget_create_text(g_act_page.dist_buf, &tamzen9, false, 1, false);
	g_act_page.speed = ui_widget_create_text(g_act_page.speed_buf, &tamzen9, false, 1, false);
	g_act_page.calories = ui_widget_create_text(g_act_page.cal_buf, &tamzen9, false, 1, false);

	ui_container_add_child(g_root_stack, g_act_page.root);
	ui_container_add_child(g_act_page.root, g_act_page.act_grid);
	ui_container_add_child(g_act_page.act_grid, g_act_page.steps);
	ui_container_add_child(g_act_page.act_grid, g_act_page.duration);
	ui_container_add_child(g_act_page.act_grid, g_act_page.distance);
	ui_container_add_child(g_act_page.act_grid, g_act_page.speed);
	ui_container_add_child(g_act_page.root, g_act_page.calories);
}

static void on_bio_click(void *user_data)
{
	uint32_t ntf = (uint32_t)user_data;
	kernel_task_notify(g_task_vitals_h, ntf, NOTIFY_ACTION_SET_BITS);

	// Start loader animation on clicked vital
	if (ntf == VITALS_READ_HR_NTF) {
		g_vitals_page.measuring = VITALS_MEASURING_HR;
		bw_str_format(g_vitals_page.hr_buf, sizeof(g_vitals_page.hr_buf), "...");
		if (g_vitals_page.hr_text)
			ui_widget_update_text(g_vitals_page.hr_text, g_vitals_page.hr_buf);
	} else if (ntf == VITALS_READ_SPO2_NTF) {
		g_vitals_page.measuring = VITALS_MEASURING_SPO2;
		bw_str_format(g_vitals_page.spo2_buf, sizeof(g_vitals_page.spo2_buf), "...");
		if (g_vitals_page.spo2_text)
			ui_widget_update_text(g_vitals_page.spo2_text, g_vitals_page.spo2_buf);
	}

	request_draw();
}

static void build_vitals_page(void)
{
	bw_str_format(g_vitals_page.hr_buf, sizeof(g_vitals_page.hr_buf), "-- bpm");
	bw_str_format(g_vitals_page.spo2_buf, sizeof(g_vitals_page.spo2_buf), "--.- %%");

	g_vitals_page.root = ui_widget_create_col(0x00, 2, 2, 1, false);

	g_vitals_page.hr_btn = ui_widget_create_row(0xFF, 2, 0, 1, true);
	ui_widget_set_callbacks(g_vitals_page.hr_btn, NULL, on_bio_click, (void *)VITALS_READ_HR_NTF);
	g_vitals_page.hr_icon = ui_widget_create_image(&heart_rate_bmp, false, 1, false);
	g_vitals_page.hr_text =
		ui_widget_create_text(g_vitals_page.hr_buf, &tamzen12b, false, 1, false);

	g_vitals_page.spo2_btn = ui_widget_create_row(0xFF, 2, 0, 1, true);
	ui_widget_set_callbacks(g_vitals_page.spo2_btn, NULL, on_bio_click,
							(void *)VITALS_READ_SPO2_NTF);
	g_vitals_page.spo2_icon = ui_widget_create_image(&spo2_bmp, false, 1, false);
	g_vitals_page.spo2_text =
		ui_widget_create_text(g_vitals_page.spo2_buf, &tamzen12b, false, 1, false);

	ui_container_add_child(g_root_stack, g_vitals_page.root);
	ui_container_add_child(g_vitals_page.root, g_vitals_page.hr_btn);
	ui_container_add_child(g_vitals_page.hr_btn, g_vitals_page.hr_icon);
	ui_container_add_child(g_vitals_page.hr_btn, g_vitals_page.hr_text);
	ui_container_add_child(g_vitals_page.root, g_vitals_page.spo2_btn);
	ui_container_add_child(g_vitals_page.spo2_btn, g_vitals_page.spo2_icon);
	ui_container_add_child(g_vitals_page.spo2_btn, g_vitals_page.spo2_text);
}

static void build_weather_page(void)
{
	bw_str_format(g_weather_page.temp_buf, sizeof(g_weather_page.temp_buf), "Temp: --.- C");
	bw_str_format(g_weather_page.hum_buf, sizeof(g_weather_page.hum_buf), "Hum: --.- %%");
	bw_str_format(g_weather_page.lux_buf, sizeof(g_weather_page.lux_buf), "Lux: -- lx");

	g_weather_page.root = ui_widget_create_col(0xFF, 0, 2, 1, false);
	g_weather_page.temp =
		ui_widget_create_text(g_weather_page.temp_buf, &tamzen12b, false, 1, false);
	g_weather_page.humidity =
		ui_widget_create_text(g_weather_page.hum_buf, &tamzen12b, false, 1, false);
	g_weather_page.lux = ui_widget_create_text(g_weather_page.lux_buf, &tamzen12b, false, 1, false);

	ui_container_add_child(g_root_stack, g_weather_page.root);
	ui_container_add_child(g_weather_page.root, g_weather_page.temp);
	ui_container_add_child(g_weather_page.root, g_weather_page.humidity);
	ui_container_add_child(g_weather_page.root, g_weather_page.lux);
}

static void on_toggle_ble(void *user_data)
{
	(void)user_data;
	if (g_ble_page.state == BLE_STATE_OFF)
		kernel_task_notify(g_task_ble_h, BLE_START_NTF, NOTIFY_ACTION_SET_BITS);
	else
		kernel_task_notify(g_task_ble_h, BLE_STOP_NTF, NOTIFY_ACTION_SET_BITS);
}

static void build_ble_page(void)
{
	g_ble_page.root = ui_widget_create_col(0x00, 2, 2, 1, false);
	g_ble_page.conn_btn = ui_widget_create_row(0xFF, 2, 0, 1, true);
	ui_widget_set_callbacks(g_ble_page.conn_btn, NULL, on_toggle_ble, NULL);
	g_ble_page.conn_btn_text =
		ui_widget_create_text(g_ble_page.btn_buf, &tamzen12b, false, 1, false);
	g_ble_page.status = ui_widget_create_text(g_ble_page.status_buf, &tamzen12b, false, 2, false);

	ui_container_add_child(g_root_stack, g_ble_page.root);
	ui_container_add_child(g_ble_page.root, g_ble_page.conn_btn);
	ui_container_add_child(g_ble_page.conn_btn, g_ble_page.conn_btn_text);
	ui_container_add_child(g_ble_page.root, g_ble_page.status);
}

static void build_settings_page(void)
{
	g_settings_page.root = ui_widget_create_col(0x00, 2, 2, 1, false);
	g_settings_page.manuf_name =
		ui_widget_create_text(g_app_settings.manuf_name, &tamzen12b, true, 1, false);
	g_settings_page.fw = ui_widget_create_text(g_settings_page.fw_buf, &tamzen12b, true, 1, false);
	g_settings_page.height =
		ui_widget_create_text(g_settings_page.height_buf, &tamzen12b, true, 1, false);
	g_settings_page.weight =
		ui_widget_create_text(g_settings_page.weight_buf, &tamzen12b, true, 1, false);

	ui_container_add_child(g_root_stack, g_settings_page.root);
	ui_container_add_child(g_settings_page.root, g_settings_page.manuf_name);
	ui_container_add_child(g_settings_page.root, g_settings_page.fw);
	ui_container_add_child(g_settings_page.root, g_settings_page.height);
	ui_container_add_child(g_settings_page.root, g_settings_page.weight);
}

static void build_all_pages(void)
{
	g_root_stack = ui_widget_create_stack(0x00, 0, 1, false);

	build_startup_page();
	build_app_page();
	build_clock_page();
	build_act_page();
	build_vitals_page();
	build_weather_page();
	build_ble_page();
	build_settings_page();

	ui_build(g_root_stack);
}

static void update_act_display(void)
{
	struct act_record rec;
	task_act_get_latest_record(&rec);

	bw_str_format(g_act_page.steps_buf, sizeof(g_act_page.steps_buf), "%u steps", rec.steps);
	ui_widget_update_text(g_act_page.steps, g_act_page.steps_buf);

	uint32_t mins = rec.dur_ms / (1000 * 60);
	bw_str_format(g_act_page.dur_buf, sizeof(g_act_page.dur_buf), "%02u:%02u", mins / 60,
				  mins % 60);
	ui_widget_update_text(g_act_page.duration, g_act_page.dur_buf);

	uint32_t dist_m = (uint32_t)rec.distance_m;
	if (dist_m < 1000)
		bw_str_format(g_act_page.dist_buf, sizeof(g_act_page.dist_buf), "%u m", dist_m);
	else
		bw_str_format(g_act_page.dist_buf, sizeof(g_act_page.dist_buf), "%u.%02u km", dist_m / 1000,
					  (dist_m % 1000) / 10);
	ui_widget_update_text(g_act_page.distance, g_act_page.dist_buf);

	uint32_t speed_kmh = (uint32_t)(rec.speed_ms * 36);
	bw_str_format(g_act_page.speed_buf, sizeof(g_act_page.speed_buf), "%u.%01u km/h",
				  speed_kmh / 10, speed_kmh % 10);
	ui_widget_update_text(g_act_page.speed, g_act_page.speed_buf);

	bw_str_format(g_act_page.cal_buf, sizeof(g_act_page.cal_buf), "%u kcal",
				  (uint32_t)rec.calories_kcal);
	ui_widget_update_text(g_act_page.calories, g_act_page.cal_buf);

	bw_str_format(g_app_page.act_buf, sizeof(g_app_page.act_buf), "%s    %s", g_act_page.steps_buf,
				  g_act_page.dist_buf);
	ui_widget_update_text(g_app_page.activity_header, g_app_page.act_buf);

	request_draw();
}

static void update_vitals_spinner(void)
{
	if (g_vitals_page.measuring == VITALS_IDLE)
		return;

	static const char *spin_dots[] = { ".", "..", "...", "...." };
	static uint8_t spin_frame = 0;
	spin_frame = (spin_frame + 1) % 4;

	if (g_vitals_page.measuring == VITALS_MEASURING_HR && g_vitals_page.hr_text) {
		bw_str_format(g_vitals_page.hr_buf, sizeof(g_vitals_page.hr_buf), "%s",
					  spin_dots[spin_frame]);
		ui_widget_update_text(g_vitals_page.hr_text, g_vitals_page.hr_buf);
	} else if (g_vitals_page.measuring == VITALS_MEASURING_SPO2 && g_vitals_page.spo2_text) {
		bw_str_format(g_vitals_page.spo2_buf, sizeof(g_vitals_page.spo2_buf), "%s",
					  spin_dots[spin_frame]);
		ui_widget_update_text(g_vitals_page.spo2_text, g_vitals_page.spo2_buf);
	}
}

static void update_vitals_display(void)
{
	struct vitals_record rec;
	task_vitals_get_latest_record(&rec);

	if (rec.type == VITALS_TYPE_HR) {
		g_vitals_page.measuring = VITALS_IDLE;
		bw_str_format(g_vitals_page.hr_buf, sizeof(g_vitals_page.hr_buf), "%u bpm",
					  rec.value.hr_bpm);
		if (g_vitals_page.hr_text)
			ui_widget_update_text(g_vitals_page.hr_text, g_vitals_page.hr_buf);
	} else if (rec.type == VITALS_TYPE_SPO2) {
		g_vitals_page.measuring = VITALS_IDLE;
		int spo2x10 = (int)(rec.value.spo2_pct * 100.0f);
		bw_str_format(g_vitals_page.spo2_buf, sizeof(g_vitals_page.spo2_buf), "%d.%d %%",
					  spo2x10 / 10, spo2x10 % 10);
		BW_LOG("%s\n", g_vitals_page.spo2_buf);
		if (g_vitals_page.spo2_text)
			ui_widget_update_text(g_vitals_page.spo2_text, g_vitals_page.spo2_buf);
	}

	bw_str_format(g_app_page.vitals_buf, sizeof(g_app_page.vitals_buf), "HR: %s   SPO2: %s",
				  g_vitals_page.hr_buf, g_vitals_page.spo2_buf);
	ui_widget_update_text(g_app_page.vitals_header, g_app_page.vitals_buf);

	request_draw();
}

static void update_weather_display(void)
{
	struct env_record rec;
	task_env_get_latest_record(&rec);

	bw_str_format(g_weather_page.temp_buf, sizeof(g_weather_page.temp_buf), "Temp: %d.%d C",
				  rec.tempx100 / 100, (rec.tempx100 % 100) / 10);
	ui_widget_update_text(g_weather_page.temp, g_weather_page.temp_buf);

	bw_str_format(g_weather_page.hum_buf, sizeof(g_weather_page.hum_buf), "Hum: %d.%d %%",
				  rec.rhx100 / 100, (rec.rhx100 % 100) / 10);
	ui_widget_update_text(g_weather_page.humidity, g_weather_page.hum_buf);

	bw_str_format(g_weather_page.lux_buf, sizeof(g_weather_page.lux_buf), "Lux: %d.%d lx",
				  rec.luxx100 / 100, (rec.luxx100 % 100) / 10);
	ui_widget_update_text(g_weather_page.lux, g_weather_page.lux_buf);

	bw_str_format(g_app_page.weather_buf, sizeof(g_app_page.weather_buf), "%s  %s",
				  g_weather_page.temp_buf, g_weather_page.hum_buf);
	ui_widget_update_text(g_app_page.weather_header, g_app_page.weather_buf);

	uint8_t brightness = MIN((uint32_t)rec.luxx100 * UINT8_MAX / UINT16_MAX + 50, 255);
	display_set_brightness(brightness);

	request_draw();
}

static void update_ble_spinner(void)
{
	if (g_ble_page.state != BLE_STATE_ADVERTISING)
		return;

	static const char *spin_dots[] = { ".", "..", "...", "...." };
	static uint8_t spin_frame = 0;
	spin_frame = (spin_frame + 1) % 4;

	bw_str_format(g_ble_page.status_buf, sizeof(g_ble_page.status_buf), "Advertising%s",
				  spin_dots[spin_frame]);
	ui_widget_update_text(g_ble_page.status, g_ble_page.status_buf);
	ui_widget_update_text(g_app_page.ble_header, g_ble_page.status_buf);
}

static void update_settings_display(void)
{
	bw_str_format(g_settings_page.fw_buf, sizeof(g_settings_page.fw_buf), "Biowatch %s",
				  g_app_settings.fw_version);
	ui_widget_update_text(g_settings_page.fw, g_settings_page.fw_buf);

	bw_str_format(g_settings_page.height_buf, sizeof(g_settings_page.height_buf),
				  "Height:     %d cm", g_app_settings.height_cm);
	ui_widget_update_text(g_settings_page.height, g_settings_page.height_buf);

	bw_str_format(g_settings_page.weight_buf, sizeof(g_settings_page.weight_buf),
				  "Weight:      %d kg", g_app_settings.weight_kg);
	ui_widget_update_text(g_settings_page.weight, g_settings_page.weight_buf);

	request_draw();
}

void task_ui(void *user_data)
{
	(void)user_data;

	ui_init();
	task_ui_init();

	read_rtc_to_clock_val();

	build_all_pages();
	ui_widget_set_active_child(g_root_stack, ROOT_IDX_APP_HOME);
	ui_draw();
	kernel_task_delay(1000);

	while (1) {
		uint32_t ntf = 0;
		kernel_task_notify_wait(0, 0xFFFFFFFF, &ntf, MAX_TIMEOUT);

		if (ntf & UI_WUT_1HZ_NTF) {
			uint8_t hr, min, sec;
			bool pm;
			rtc_get_time(&hr, &min, &sec, &pm);

			// On next day start a new record
			if (hr == 12 && min == 0 && sec == 0 && !pm)
				kernel_task_notify(g_task_act_h, ACT_NEW_REC_NTF, NOTIFY_ACTION_SET_BITS);

			uint16_t new_hr = (hr == 0) ? 12 : hr;
			uint16_t new_meridiem = pm ? 1 : 0;

			if (g_clock_page.minute != min || g_clock_page.hour != new_hr ||
				g_clock_page.meridiem != new_meridiem) {
				read_rtc_to_clock_val();
			}

			if (g_clock_page.timer_running) {
				g_clock_page.timer_s++;
				if (g_clock_page.timer_s >= 60) {
					g_clock_page.timer_s = 0;
					g_clock_page.timer_m++;
					if (g_clock_page.timer_m >= 60) {
						g_clock_page.timer_m = 0;
						g_clock_page.timer_h = (g_clock_page.timer_h + 1) % 100;
					}
				}
				update_timer_display();
			}

			update_vitals_spinner();
			update_ble_spinner();
			request_draw();
		}

		if (ntf & UI_ACT_CHANGED_NTF)
			update_act_display();

		if (ntf & UI_VIT_CHANGED_NTF) {
			kernel_task_notify(g_task_hap_h, HAPTICS_VIB_NTF, NOTIFY_ACTION_SET_BITS);
			update_vitals_display();
		}

		if (ntf & UI_ENV_CHANGED_NTF)
			update_weather_display();

		if (ntf & UI_BLE_CONNECTED_NTF) {
			g_ble_page.state = BLE_STATE_CONNECTED;
			kernel_task_notify(g_task_hap_h, HAPTICS_VIB_NTF, NOTIFY_ACTION_SET_BITS);
			bw_str_format(g_ble_page.status_buf, sizeof(g_ble_page.status_buf), "%s", "Connected");
			ui_widget_update_text(g_ble_page.status, g_ble_page.status_buf);
			ui_widget_update_text(g_app_page.ble_header, g_ble_page.status_buf);
			bw_str_format(g_ble_page.btn_buf, sizeof(g_ble_page.btn_buf), "%s", "Disable BLE");
			ui_widget_update_text(g_ble_page.conn_btn_text, g_ble_page.btn_buf);
			request_draw();
		}

		if (ntf & UI_BLE_ADVERTISING_NTF) {
			g_ble_page.state = BLE_STATE_ADVERTISING;
			bw_str_format(g_ble_page.btn_buf, sizeof(g_ble_page.btn_buf), "%s", "Disable BLE");
			ui_widget_update_text(g_ble_page.conn_btn_text, g_ble_page.btn_buf);
			bw_str_format(g_ble_page.status_buf, sizeof(g_ble_page.status_buf), "%s",
						  "Advertising.");
			ui_widget_update_text(g_ble_page.status, g_ble_page.status_buf);
			ui_widget_update_text(g_app_page.ble_header, g_ble_page.status_buf);
			request_draw();
		}

		if (ntf & UI_BLE_DISCONNECTED_NTF) {
			g_ble_page.state = BLE_STATE_OFF;
			bw_str_format(g_ble_page.btn_buf, sizeof(g_ble_page.btn_buf), "%s", "Enable BLE");
			ui_widget_update_text(g_ble_page.conn_btn_text, g_ble_page.btn_buf);
			bw_str_format(g_ble_page.status_buf, sizeof(g_ble_page.status_buf), "%s",
						  "Not Connected");
			ui_widget_update_text(g_ble_page.status, g_ble_page.status_buf);
			ui_widget_update_text(g_app_page.ble_header, g_ble_page.status_buf);
			request_draw();
		}

		if (ntf & UI_SET_CHANGED_NTF)
			update_settings_display();

		if (ntf & UI_DRAW_NTF)
			ui_draw();
	}
}

struct exti_handle *ui_get_next_btn_handle(exti_callback_t *callback)
{
	*callback = btn_exti;
	g_btn_exti[UI_BTN_NEXT].user_data = (void *)UI_BTN_NEXT;
	return &g_btn_exti[UI_BTN_NEXT];
}

struct exti_handle *ui_get_click_btn_handle(exti_callback_t *callback)
{
	*callback = btn_exti;
	g_btn_exti[UI_BTN_CLICK].user_data = (void *)UI_BTN_CLICK;
	return &g_btn_exti[UI_BTN_CLICK];
}

struct exti_handle *ui_get_home_btn_handle(exti_callback_t *callback)
{
	*callback = btn_exti;
	g_btn_exti[UI_BTN_HOME].user_data = (void *)UI_BTN_HOME;
	return &g_btn_exti[UI_BTN_HOME];
}

#include "task_ble.h"
#include "app/settings.h"
#include "app/tasks/task_ui.h"
#include "kernel/sync/mqueue.h"
#include "kernel/task/task.h"
#include "kernel/timer.h"
#include "lib/status.h"
#include "subsys/ble/ble.h"
#include "subsys/ble/ble_sig_uuids.h"
#include "subsys/ble/svc/svcctl.h"
#include "task_act.h"
#include "task_vitals.h"
#include "task_env.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FAST_ADV_PERIOD (20 * 1000)
#define LP_ADV_PERIOD (5 * 60 * 1000)

#define BLE_SVC_ENVIRONMENT                             \
	{                                                   \
		0xcb, 0xcd, 0x45, 0x67, 0x12, 0x74, 0x46, 0x82, \
		0xa5, 0x0d, 0xff, 0xe8, 0x96, 0x66, 0xf6, 0xac  \
	}
#define BLE_CHAR_ENV_DATA                               \
	{                                                   \
		0xcc, 0x62, 0x6f, 0x98, 0x6b, 0xe6, 0x4e, 0xe4, \
		0x8f, 0x1b, 0x8c, 0xd9, 0xc8, 0x97, 0xc7, 0xba  \
	}

#define BLE_SVC_ACTIVITY                                \
	{                                                   \
		0xdd, 0xeb, 0x65, 0x17, 0x45, 0x16, 0x4a, 0x2d, \
		0x87, 0x4a, 0x5a, 0x88, 0x38, 0x02, 0x93, 0x04  \
	}
#define BLE_CHAR_ACT_DATA                               \
	{                                                   \
		0xdd, 0x61, 0x37, 0x73, 0xd7, 0x96, 0x49, 0xb0, \
		0xb1, 0x85, 0x0f, 0xd5, 0x03, 0x69, 0xe8, 0x2b  \
	}

#define BLE_SVC_VITALS_DATA                             \
	{                                                   \
		0x96, 0x43, 0x24, 0x94, 0x3f, 0x2f, 0x4e, 0xab, \
		0xa1, 0xb4, 0xac, 0xc1, 0x81, 0x35, 0x33, 0xf1  \
	}
#define BLE_CHAR_VITALS_DATA                            \
	{                                                   \
		0xbb, 0xb1, 0x92, 0xed, 0x21, 0xa5, 0x4e, 0xfc, \
		0xbc, 0xd4, 0x49, 0xde, 0x32, 0x63, 0xac, 0x58  \
	}

#define BLE_SVC_SETTINGS                                \
	{                                                   \
		0x99, 0x68, 0xf7, 0xd7, 0xd0, 0xbf, 0x4e, 0xff, \
		0xbb, 0xc6, 0xcb, 0x15, 0x1e, 0x04, 0x1f, 0xb1  \
	}
#define BLE_CHAR_WEIGHT_KG                              \
	{                                                   \
		0x7c, 0x27, 0xca, 0x38, 0x6e, 0x37, 0x44, 0x5b, \
		0x9a, 0x15, 0xd0, 0x48, 0xc2, 0x3e, 0xdb, 0xf2  \
	}

task_handle_t g_task_ble_h;
static struct kernel_timer g_adv_timer = { .type = KERNEL_TIMER_ONE_SHOT, .user_data = NULL };
static bool g_ble_user_enabled = false;

enum notification_status { NOTIFICATION_DISABLED, NOTIFICATION_ENABLED };

static struct {
	uint16_t handle;
	uint16_t manuf_name;
	uint16_t firmware_ver;
} g_dev_info_svc;

static struct {
	uint16_t handle;
	uint16_t env_data;
	enum notification_status ntf_status;
} g_env_svc;

static struct {
	uint16_t handle;
	uint16_t act_data;
	enum notification_status ntf_status;
} g_act_svc;

static struct {
	uint16_t handle;
	uint16_t vitals_data;
	enum notification_status ntf_status;
} g_vitals_svc;

static struct {
	uint16_t handle;
	uint16_t height;
	uint16_t weight;
} g_sett_svc;

static void on_notification_changed(const struct ble_char_evt *evt, void *ctx)
{
	enum notification_status *ntf_status = (enum notification_status *)ctx;
	if (evt->type == BLE_CHAR_EVT_NOTIFY_ENABLED) {
		*ntf_status = NOTIFICATION_ENABLED;
		kernel_task_notify(g_task_ble_h, BLE_DRAIN_QUEUE_NTF, NOTIFY_ACTION_SET_BITS);
	} else if (evt->type == BLE_CHAR_EVT_NOTIFY_DISABLED) {
		*ntf_status = NOTIFICATION_DISABLED;
	}
}

static void on_settings_update(const struct ble_char_evt *evt, void *ctx)
{
	(void)evt;
	(void)ctx;
	BW_LOG("Settings updated\n");
}

static void build_gatt_db(void)
{
	ble_svcctl_add_svc(BLE_UUID16(BLE_SVC_DEVICE_INFORMATION), BLE_SVC_TYPE_PRIMARY,
					   BLE_SVC_ATTR_RECORDS(2, 0), &g_dev_info_svc.handle);
	ble_svcctl_add_char(g_dev_info_svc.handle, BLE_UUID16(BLE_CHAR_MANUFACTURER_NAME_STRING),
						strlen(g_app_settings.manuf_name), BLE_CHAR_PROP_READ,
						BLE_CHAR_EVT_DONT_NOTIFY, true, NULL, NULL, &g_dev_info_svc.manuf_name);
	ble_svcctl_update_char(g_dev_info_svc.handle, g_dev_info_svc.manuf_name, 0,
						   strlen(g_app_settings.manuf_name),
						   (const uint8_t *)g_app_settings.manuf_name);
	ble_svcctl_add_char(g_dev_info_svc.handle, BLE_UUID16(BLE_CHAR_FIRMWARE_REVISION_STRING),
						strlen(g_app_settings.fw_version), BLE_CHAR_PROP_READ,
						BLE_CHAR_EVT_DONT_NOTIFY, true, NULL, NULL, &g_dev_info_svc.firmware_ver);
	ble_svcctl_update_char(g_dev_info_svc.handle, g_dev_info_svc.firmware_ver, 0,
						   strlen(g_app_settings.fw_version),
						   (const uint8_t *)g_app_settings.fw_version);

	ble_svcctl_add_svc(BLE_UUID128(BLE_SVC_ACTIVITY), BLE_SVC_TYPE_PRIMARY,
					   BLE_SVC_ATTR_RECORDS(1, 1), &g_act_svc.handle);
	ble_svcctl_add_char(g_act_svc.handle, BLE_UUID128(BLE_CHAR_ACT_DATA), sizeof(struct act_record),
						BLE_CHAR_PROP_READ | BLE_CHAR_PROP_NOTIFY, BLE_CHAR_EVT_DONT_NOTIFY, false,
						on_notification_changed, &g_act_svc.ntf_status, &g_act_svc.act_data);

	ble_svcctl_add_svc(BLE_UUID128(BLE_SVC_VITALS_DATA), BLE_SVC_TYPE_PRIMARY,
					   BLE_SVC_ATTR_RECORDS(1, 1), &g_vitals_svc.handle);
	ble_svcctl_add_char(g_vitals_svc.handle, BLE_UUID128(BLE_CHAR_VITALS_DATA),
						sizeof(struct vitals_record), BLE_CHAR_PROP_READ | BLE_CHAR_PROP_NOTIFY,
						BLE_CHAR_EVT_DONT_NOTIFY, false, on_notification_changed,
						&g_vitals_svc.ntf_status, &g_vitals_svc.vitals_data);

	ble_svcctl_add_svc(BLE_UUID128(BLE_SVC_ENVIRONMENT), BLE_SVC_TYPE_PRIMARY,
					   BLE_SVC_ATTR_RECORDS(1, 1), &g_env_svc.handle);
	ble_svcctl_add_char(g_env_svc.handle, BLE_UUID128(BLE_CHAR_ENV_DATA), sizeof(struct env_record),
						BLE_CHAR_PROP_READ | BLE_CHAR_PROP_NOTIFY, BLE_CHAR_EVT_DONT_NOTIFY, false,
						on_notification_changed, &g_env_svc.ntf_status, &g_env_svc.env_data);

	ble_svcctl_add_svc(BLE_UUID128(BLE_SVC_SETTINGS), BLE_SVC_TYPE_PRIMARY,
					   BLE_SVC_ATTR_RECORDS(2, 0), &g_sett_svc.handle);
	ble_svcctl_add_char(g_sett_svc.handle, BLE_UUID16(BLE_CHAR_HEIGHT), 2, BLE_CHAR_PROP_WRITE,
						BLE_CHAR_EVT_NOTIFY_ATTRIBUTE_WRITE, false, on_settings_update,
						&g_app_settings.height_cm, &g_sett_svc.height);
	ble_svcctl_add_char(g_sett_svc.handle, BLE_UUID128(BLE_CHAR_WEIGHT_KG), 2, BLE_CHAR_PROP_WRITE,
						BLE_CHAR_EVT_NOTIFY_ATTRIBUTE_WRITE, false, on_settings_update,
						&g_app_settings.weight_kg, &g_sett_svc.weight);
}

static void adv_timer_fast_expired(void *arg)
{
	(void)arg;
	kernel_task_notify(g_task_ble_h, BLE_START_LP_ADV_NTF, NOTIFY_ACTION_SET_BITS);
}

static void adv_timer_lp_expired(void *arg)
{
	(void)arg;
	kernel_task_notify(g_task_ui_h, BLE_STOP_NTF, NOTIFY_ACTION_SET_BITS);
}

static void connection_evt_cb(void)
{
	kernel_timer_stop(&g_adv_timer);
	kernel_task_notify(g_task_ui_h, UI_BLE_CONNECTED_NTF, NOTIFY_ACTION_SET_BITS);
}

static void disconnection_evt_cb(void)
{
	kernel_task_notify(g_task_ui_h, UI_BLE_DISCONNECTED_NTF, NOTIFY_ACTION_SET_BITS);

	if (g_ble_user_enabled)
		kernel_task_notify(g_task_ble_h, BLE_START_NTF, NOTIFY_ACTION_SET_BITS);
}

void task_ble(void *user_data)
{
	(void)user_data;

	ble_svcctl_set_ad_svc_uuid(BLE_UUID16(BLE_SVC_DEVICE_INFORMATION));
	ble_svcctl_register_init(build_gatt_db);

	struct ble_conf conf = { .name = "BioWatch",
							 .appearance = BLE_APPEARANCE_WATCH_SPORTS,
							 .io_capability = BLE_IO_CAPABILITY_NO_INPUT_NO_OUTPUT,
							 .secure_support = BLE_SECURE_MANDATORY,
							 .mitm_protection = false,
							 .bonding_mode = true,
							 .connection_evt_cb = connection_evt_cb,
							 .disconnection_evt_cb = disconnection_evt_cb };
	ble_init(&conf);

	kernel_timer_register(&g_adv_timer);

	while (1) {
		uint32_t ntf;
		kernel_task_notify_wait(0, UINT32_MAX, &ntf, MAX_TIMEOUT);

		if (ntf & BLE_START_NTF) {
			g_ble_user_enabled = true;
			g_adv_timer.ticks = FAST_ADV_PERIOD;
			g_adv_timer.callback = adv_timer_fast_expired;
			ble_adv_start(BLE_CONN_STATUS_FAST_ADV);
			kernel_timer_start(&g_adv_timer);
			kernel_task_notify(g_task_ui_h, UI_BLE_ADVERTISING_NTF, NOTIFY_ACTION_SET_BITS);
		} else if (ntf & BLE_START_LP_ADV_NTF) {
			g_adv_timer.ticks = LP_ADV_PERIOD;
			g_adv_timer.callback = adv_timer_lp_expired;
			ble_adv_start(BLE_CONN_STATUS_LP_ADV);
			kernel_timer_start(&g_adv_timer);
		}

		if (ntf & BLE_STOP_NTF) {
			g_ble_user_enabled = false;
			enum ble_conn_status conn_status = ble_get_conn_status();

			switch (conn_status) {
			case BLE_CONN_STATUS_CONNECTED_SERVER:
			case BLE_CONN_STATUS_CONNECTED_CLIENT:
				ble_terminate();
				break;
			case BLE_CONN_STATUS_FAST_ADV:
			case BLE_CONN_STATUS_LP_ADV:
				ble_adv_stop();
				kernel_timer_stop(&g_adv_timer);
				kernel_task_notify(g_task_ui_h, UI_BLE_DISCONNECTED_NTF, NOTIFY_ACTION_SET_BITS);
				break;
			case BLE_CONN_STATUS_IDLE:
			default:
				kernel_task_notify(g_task_ui_h, UI_BLE_DISCONNECTED_NTF, NOTIFY_ACTION_SET_BITS);
				break;
			}
		}

		if (ntf & BLE_DRAIN_QUEUE_NTF) {
			if (g_act_svc.ntf_status == NOTIFICATION_ENABLED) {
				struct act_record act_rec;
				while (kernel_mqueue_receive(&g_act_mqueue, (void *)&act_rec, 0) == STATUS_OK)
					ble_svcctl_update_char(g_act_svc.handle, g_act_svc.act_data, 0,
										   sizeof(struct act_record), (void *)&act_rec);
				task_act_get_latest_record(&act_rec);
				ble_svcctl_update_char(g_act_svc.handle, g_act_svc.act_data, 0,
									   sizeof(struct act_record), (void *)&act_rec);
			}

			if (g_vitals_svc.ntf_status == NOTIFICATION_ENABLED) {
				struct vitals_record vit_rec;
				while (kernel_mqueue_receive(&g_vitals_mqueue, (void *)&vit_rec, 0) == STATUS_OK)
					ble_svcctl_update_char(g_vitals_svc.handle, g_vitals_svc.vitals_data, 0,
										   sizeof(struct vitals_record), (void *)&vit_rec);
			}

			if (g_env_svc.ntf_status == NOTIFICATION_ENABLED) {
				struct env_record env_rec;
				while (kernel_mqueue_receive(&g_env_mqueue, (void *)&env_rec, 0) == STATUS_OK)
					ble_svcctl_update_char(g_env_svc.handle, g_env_svc.env_data, 0,
										   sizeof(struct env_record), (void *)&env_rec);
			}
		}

		if (g_act_svc.ntf_status == NOTIFICATION_ENABLED && ntf & BLE_ACT_CHANGED_NTF) {
			struct act_record rec;
			task_act_get_latest_record(&rec);
			ble_svcctl_update_char(g_act_svc.handle, g_act_svc.act_data, 0,
								   sizeof(struct act_record), (void *)&rec);
		}

		if (g_vitals_svc.ntf_status == NOTIFICATION_ENABLED && ntf & BLE_VIT_CHANGED_NTF) {
			struct vitals_record rec;
			task_vitals_get_latest_record(&rec);
			ble_svcctl_update_char(g_vitals_svc.handle, g_vitals_svc.vitals_data, 0,
								   sizeof(struct vitals_record), (void *)&rec);
		}

		if (g_env_svc.ntf_status == NOTIFICATION_ENABLED && ntf & BLE_ENV_CHANGED_NTF) {
			struct env_record rec;
			task_env_get_latest_record(&rec);
			ble_svcctl_update_char(g_env_svc.handle, g_env_svc.env_data, 0,
								   sizeof(struct env_record), (void *)&rec);
		}
	}
}

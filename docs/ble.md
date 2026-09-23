# BLE — GATT Service Layout

This is the application-level GATT database, built in `app/tasks/task_ble.c` on top of `biowatch-core`'s transport + svcctl framework (see [biowatch-core/docs/ble.md](https://github.com/Zeta-Chrome/biowatch-core/blob/main/docs/ble.md) for how `ble_svcctl_add_svc`/`add_char` work). This doc covers *what* services exist, not the mechanism.

## Services

| Service | UUID form | Characteristics | Properties |
|---|---|---|---|
| Device Information | 16-bit (standard) | Manufacturer Name, Firmware Revision | Read |
| Activity | 128-bit (custom) | Activity Data (`struct act_record`) | Read, Notify |
| Vitals | 128-bit (custom) | Vitals Data (`struct vitals_record`) | Read, Notify |
| Environment | 128-bit (custom) | Environment Data (`struct env_record`) | Read, Notify |
| Settings | 128-bit (custom) | Time, Height, Weight | Write |

Records are sent as their raw C struct over the wire (`sizeof(struct act_record)` etc. as the characteristic length) — the mobile app decodes the same struct layout on its side (see `biowatch-app`'s `blemanager`/`bleuuids`).

## Data path in

Each measurement task pushes its latest record to its message queue (see [architecture.md](architecture.md)); `task_ble` drains those queues and calls `ble_svcctl_update_char()` to update the characteristic value, which triggers a GATT notify to any subscribed central. Notification state itself (subscribed/not) is tracked per-characteristic via `on_notification_changed()`, registered as the event callback on the Activity/Vitals/Environment characteristics — it flips `ntf_status` on `BLE_CHAR_EVT_NOTIFY_ENABLED`/`DISABLED` so the task only bothers sending updates to centrals that actually asked for them.

## Data path out (settings)

The Settings service is write-only from the central's side:
- **Time** (`on_time_update`) — 4-byte write, forwarded to the RTC.
- **Height / Weight** (`on_settings_update`) — 2-byte writes into `g_app_settings`, consumed by `task_act`'s Park & Shin stride-length model and calorie estimate (see [tasks.md](tasks.md#task_act--activity)).

Both callbacks are registered with `BLE_CHAR_EVT_NOTIFY_ATTRIBUTE_WRITE`, so svcctl only invokes them on an actual write, not on every attribute access.

## Advertising

The Device Information service UUID is set as the advertised service UUID (`ble_svcctl_set_ad_svc_uuid`), so a central can identify a BioWatch device before connecting, without needing to know the custom 128-bit service UUIDs up front.

## Mobile app counterpart

[biowatch-app](https://github.com/Zeta-Chrome/biowatch-app) (Qt6/QML) implements the central side: `blemanager.cpp` handles scanning/connection/subscription, `repository.cpp` decodes the incoming records, and `bleuuids.h` mirrors the UUIDs defined here — those two lists have to stay in sync by hand, since there's currently no shared/generated header between the firmware and app repos.

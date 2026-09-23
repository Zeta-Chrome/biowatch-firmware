# Architecture

## Boot sequence

`app/main.c`:

1. `peripheral_init()` — brings up every peripheral used by the app: EXTI lines for IPCC/HSEM (BLE wakeup from CPU2), OLED I2C+DMA, oximeter EXTI + I2C+DMA, IMU chip-select GPIO + SPI+DMA + EXTI, the three button EXTIs, and RTC (12-hour format). Every bus/driver is initialized in interrupt/DMA mode — nothing here is a blocking call.
2. `kernel_init()` / `kernel_start()` — hands off to the RTOS; `tasks_create()` (see below) spins up the six application tasks and the scheduler never returns.

## Tasks

Six tasks, defined in `app/tasks/tasks.h`, each with its own priority and stack:

| Task | Priority | Stack (words) | Role |
|---|---|---|---|
| `task_act` | 0 (lowest) | 128 | Step counting, distance, speed, calories |
| `task_ui` | 1 | 256 | Display rendering, button handling, screen timeout |
| `task_vitals` | 2 | 256 | Heart rate + SpO2 |
| `task_env` | 3 | 128 | Temperature, humidity, ambient light |
| `task_ble` | 4 | 256 | Advertising/connection state machine, GATT notifications |
| `task_haptics` | 5 (highest) | 64 | Vibration motor feedback |

Idle task (priority below all of the above, implicit) is where the RTOS's tickless low-power logic runs — see [biowatch-core/docs/lpm.md](https://github.com/Zeta-Chrome/biowatch-core/blob/main/docs/lpm.md).

Lower numeric priority = scheduled first; `task_act` is intentionally the lowest-priority task since step/activity processing can tolerate the most latency, while `task_haptics` is highest since a haptic pulse has to feel immediate.

## Inter-task signaling

The whole application is event/notification driven — no task ever polls another. Two mechanisms are used, both from `biowatch-core`'s kernel:

- **Task notifications** (`kernel_task_notify` / `_wait`) — each task defines its own bitmask of notification flags in its header, e.g. `task_vitals.h`: `VITALS_READ_HR_NTF`, `VITALS_READ_SPO2_NTF`; `task_ui.h`: `UI_DRAW_NTF`, `UI_BLE_CONNECTED_NTF`, `UI_DISPLAY_OFF_NTF`, etc. A sensor ISR or another task sets the relevant bit and the target task's `kernel_task_notify_wait()` call unblocks.
- **Shared event group** (`struct event g_app_evt`, declared in `tasks.h`) — cross-cutting flags that more than one task cares about, e.g. `IMU_RDY_EVT` (IMU has a fresh sample) and `DISPLAY_ON_EVT` (display is currently on, so environment/UI tasks know whether to keep polling the LDR).
- **Message queues** (`g_act_mqueue`, `g_vitals_mqueue`, `g_env_mqueue`) — each measurement task exposes a `struct *_record` and a `task_*_get_latest_record()` accessor; the BLE task pulls the latest record off these queues to push out as a GATT notification, and the UI task pulls them to redraw the relevant screen.

This keeps every task's own header self-describing: reading `task_ui.h`'s notification bit list tells you exactly what can wake the UI task and why, without having to trace call sites.

## Data flow, end to end (example: heart rate)

```
MAX3010x IRQ (data ready)
   → oxim EXTI callback → kernel_task_notify(task_vitals, VITALS_READ_HR_NTF)
   → task_vitals: read FIFO over I2C (DMA) → NLMS motion-noise removal (using IMU samples) →
     autocorrelation peak → bpm → push struct vitals_record to g_vitals_mqueue
   → kernel_task_notify(task_ui, UI_VIT_CHANGED_NTF)   → UI redraws HR widget
   → kernel_task_notify(task_ble, BLE_ACT_CHANGED_NTF) → BLE task drains queue, sends GATT notify
```

Every stage after the initial IRQ is a task waking on a notification/queue — the CPU is back asleep between each arrow.

## Settings (`app/settings.c`)

`struct g_app_settings` holds user/device configuration (height, weight, manufacturer/firmware strings, display timeout, etc.) written from BLE (`on_settings_update`, `on_time_update` — see [docs/ble.md](ble.md)) and read by the measurement tasks (e.g. height feeds the Park & Shin stride-length estimate in `task_act`) and by the UI task (display timeout).

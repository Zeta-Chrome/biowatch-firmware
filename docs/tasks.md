# Tasks

Detailed behaviour of each `app/tasks/*` task. See [architecture.md](architecture.md) for how they signal each other.

## `task_act` — Activity

**Input:** BMI160 accelerometer/gyro samples (SPI, DMA, woken by `IMU_RDY_EVT`).

Pipeline:
1. Step detection from the BMI160 stream.
2. **Stride length** — estimated using the **Park & Shin** algorithm (step-frequency/acceleration-based stride estimation), combined with the user's configured height (`app/settings.c`) as the model's height parameter.
3. **Distance** — accumulated steps × estimated stride length.
4. **Duration/speed** — inter-step timing gives step duration, from which average speed is derived.
5. **Calories** — derived from distance/speed and user weight/height.

Output: `struct act_record { timestamp, dur_ms, distance_m, speed_ms, calories_kcal, steps }`, pushed to `g_act_mqueue`, read via `task_act_get_latest_record()`. Runs at the lowest task priority (`PRIO_ACT = 0`) — activity math can tolerate the most scheduling latency of any task on the watch.

## `task_vitals` — Heart rate & SpO2

**Input:** MAX3010x PPG front end (I2C, DMA, EXTI data-ready) + BMI160 samples for motion reference.

- **Heart rate:** raw PPG is cleaned with an **NLMS (Normalized Least Mean Squares) adaptive filter**, using IMU data as the motion-noise reference to cancel motion artefacts from the optical signal. **Autocorrelation** is then run on the cleaned signal; the lag at the highest correlation peak gives the beat period, converted to BPM.
- **SpO2:** simple **ratio-of-ratios** approach — AC(RMS)/DC(mean) is computed for both the red and IR LED channels, the ratio of those two values is fed into a **polynomial calibration equation** to produce %SpO2.

Two independent notification bits (`VITALS_READ_HR_NTF`, `VITALS_READ_SPO2_NTF`) let the task service either measurement on its own trigger rather than always computing both together. Output: `struct vitals_record` (tagged union of `hr_bpm` / `spo2_pct` by `enum vitals_type`) → `g_vitals_mqueue`.

## `task_env` — Environment

**Input:** temp/humidity sensor (I2C) + LDR (ADC).

- Ambient temperature/humidity read and pushed as a BLE notification **once a minute** — this data doesn't need to be fresher than that, so the read cadence is deliberately kept low to save power.
- LDR (display-brightness sensing) is read **every 2 seconds, but only while `DISPLAY_ON_EVT` is set** — if the display is off there's nothing to adjust brightness on, so the read (and its associated ADC wake-up) is skipped entirely, removing that wake source from the sleep budget whenever the screen is off.

Output: `struct env_record { rhx100, tempx100, luxx100 }` → `g_env_mqueue`.

## `task_ui` — UI

Drives the custom static UI toolkit in `ui/` (containers, widget selection/focus, no dynamic allocation) and the OLED driver. Responsibilities, all notification-driven (see the full bit list in `task_ui.h`):
- Render on `UI_DRAW_NTF`; redraw specific widgets on the corresponding `*_CHANGED_NTF` (activity/vitals/environment/BLE state/settings).
- Clock face tick (`UI_UPDATE_CLOCK_NTF`) from RTC.
- **Screen timeout:** after a configurable idle period the display is turned off (`UI_DISPLAY_OFF_NTF`) to save power — this both stops the OLED refresh and, via `DISPLAY_ON_EVT`, tells `task_env` to stop polling the LDR. Any button press (`UI_DISPLAY_ON_NTF`) turns it back on.
- Reads the three button EXTI lines directly (`ui_get_next_btn_handle`, `_click_btn_handle`, `_home_btn_handle`) — active-low, pulled up, next/click on a shared EXTI9_5 line, home on its own EXTI2 line.

## `task_ble` — BLE

Owns the connection state machine end to end (`BLE_START_NTF` → advertising → `BLE_START_LP_ADV_NTF` for low-power advertising → connected), builds and serves the GATT database (see [ble.md](ble.md)), and drains the activity/vitals/environment message queues to push GATT notifications (`BLE_ACT_CHANGED_NTF`, and equivalent triggers for vitals/env) plus a generic `BLE_DRAIN_QUEUE_NTF` for batched sends. `BLE_STOP_NTF` tears advertising/connection down (e.g. on user request from settings).

## `task_haptics`

Smallest stack (64 words) and highest priority (`PRIO_HAP = 5`) of the six — a haptic pulse needs to preempt everything else to feel instantaneous, and driving the vibration motor is a short, simple operation that doesn't need much stack.

## Idle task

Not application code — this is where `biowatch-core`'s LPM idle hook runs, deciding sleep depth between wakeups. See [biowatch-core/docs/lpm.md](https://github.com/Zeta-Chrome/biowatch-core/blob/main/docs/lpm.md).

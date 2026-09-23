# Drivers

Sensor and display drivers under `drivers/`. Bus-level drivers (I2C/SPI/DMA/ADC/EXTI/RTC) live in `core/drivers/`; these are the per-chip drivers built on top of them. Pin assignments live in `boards/biowatch/pins.h`, board bring-up in `boards/biowatch/bsp.c` (`peripheral_init()`).

## IMU — BMI160 (`drivers/sensor/imu/`)

- Bus: SPI (`spi_init_dma`), chip-select as a plain GPIO output, plus a dedicated EXTI line (falling edge, pull-up) for data-ready interrupts.
- Configurable output data rate (`enum imu_odr`, 25–1600 Hz) and step-detector sensitivity mode (`IMU_STEP_MODE_NORMAL/SENSITIVE/ROBUST`) and no-motion sensitivity (`enum imu_nomo`).
- Produces `struct acc_sample` (g) and `struct gyr_sample` (deg/s).
- Feeds two consumers: `task_act` (step/stride/distance) and `task_vitals` (motion reference for PPG noise cancellation) — see [tasks.md](tasks.md).

## Oximeter — MAX30102 (`drivers/sensor/oxim/`)

- Bus: I2C (shared bus instance with the hygrometer — see below), plus EXTI (falling edge) for FIFO-ready interrupts.
- Two operating modes (`OXIM_MODE_HR`, `OXIM_MODE_SPO2`), configurable sample averaging (1–32) and sample rate (50–400 Hz).
- Buffers up to `MAX_HR_SAMPLES` (128) / `MAX_SPO2_SAMPLES` (512) samples for the NLMS + autocorrelation (HR) and AC/DC ratio (SpO2) pipelines in `task_vitals`.

## Hygrometer / temperature (`drivers/sensor/hygro/`)

- Bus: I2C — **shares the physical I2C peripheral with the oximeter** (`bsp.c` copies the oximeter's `i2c_handle` into the hygrometer's handle, then calls `i2c_bus_init()` once), since both sensors sit on the same board-level I2C bus. `core/drivers/i2c/i2c_bus.c` provides the arbitration so both drivers can issue transactions without stepping on each other.
- Read on a slow cadence from `task_env` (once a minute for the BLE-facing reading).

## LDR / ambient light

- ADC channel, read from `task_env` every 2 s while the display is on, skipped entirely while it's off (see [tasks.md](tasks.md#task_env--environment)) — feeds `display_set_brightness()`.

## Display — OLED (`drivers/display/`)

- Bus: I2C + DMA. 128×64 monochrome (`DISPLAY_SCREEN_W/H`).
- Frame-buffer style API: `display_fill_rect`, `display_draw_bitmap`, `display_region_invert` write into a local buffer; `display_flush()` pushes it to the panel over I2C/DMA.
- Power control separated from the draw API (`display_power_on/off`, plus `display_normal`/`display_inverse` and `display_set_brightness`) so `task_ui`'s screen-timeout logic can cut panel power independently of whatever's currently in the frame buffer.
- Fonts (`assets/fonts/tamzen9`, `tamzen12b`) and bitmaps (`assets/bitmaps/`) are compiled in as flat C arrays rather than loaded from a filesystem.

## Board bring-up (`boards/biowatch/bsp.c`)

`peripheral_init()` is the single place that ties pins (`pins.h`) to peripheral instances and interrupt priorities. Every bus is initialized in DMA mode and every external event (BLE IPCC/HSEM lines, oximeter/IMU data-ready, three buttons) is wired to EXTI rather than polled — see [biowatch-core/docs/lpm.md](https://github.com/Zeta-Chrome/biowatch-core/blob/main/docs/lpm.md) for why that matters for the sleep budget. IRQ priorities are set so that time-critical lines (BLE wakeups at priority 4) don't get starved by lower-urgency sensor IRQs (priority 6), while buttons sit in between (priority 5).

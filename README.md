# biowatch-firmware

**BioWatch** — a health-monitoring smartwatch built on the **STM32WB55** dual-core wireless MCU, running a custom RTOS and a fully interrupt-driven driver stack. No STM32 HAL/LL/CubeMX code — every peripheral is driven directly at the register level, via [biowatch-core](https://github.com/Zeta-Chrome/biowatch-core).

## Hardware

| Sensor | Bus | Measures |
|---|---|---|
| BMI160 | SPI | Steps, motion, activity |
| MAX3010x | I2C | Heart rate, SpO2 (PPG) |
| HDT/HYGRO (temp+humidity) | I2C | Ambient temperature / humidity |
| LDR | ADC | Ambient light → auto display brightness |
| OLED | I2C | Display |
| 3 buttons (next / click / home) | EXTI | UI navigation |

Companion mobile app: [biowatch-app](https://github.com/Zeta-Chrome/biowatch-app) (Qt6/QML), talks to the watch over BLE.

## Repository layout

```
core/               biowatch-core submodule (RTOS, drivers, BLE transport — see its own docs/)
boards/biowatch/    Pin map (pins.h) + board bring-up (bsp.c) — the one file that knows the physical wiring
drivers/
  ├─ display/         OLED command/frame-buffer driver
  └─ sensor/
      ├─ imu/          BMI160
      ├─ oxim/         MAX3010x (HR/SpO2 front end)
      └─ hygro/        Temp/humidity sensor
ui/                 Custom static UI toolkit (containers, widgets, selection/focus, screen timeout)
assets/             Fonts + bitmaps compiled in as C arrays
app/
  ├─ main.c          Boot, peripheral_init(), kernel_init()/kernel_start()
  ├─ settings.c       Persisted user/device settings (height, weight, name, BLE state, etc.)
  └─ tasks/           One file per RTOS task — see docs/tasks.md
Makefile            Builds app + core into BioWatch.elf/.hex/.bin
```

## Building

```sh
git submodule update --init          # pulls in core/
make DEBUG=1 LOGGER=rtt              # or DEBUG=0 for release, LOGGER=uart
make flash_ble                       # one-time: flash ST's BLE stack + FUS to CPU2
make flash                           # flash the application to CPU1
make monitor                         # RTT (or UART) log console
```

Release build fits comfortably inside the STM32WB55's flash/RAM budget:

```
   text    data     bss     dec     hex  filename
  37968    1548   30696   70212   11244  build/release/BioWatch.elf
```

`make check_core` compiles `core/` in isolation (no app include paths) to catch any accidental dependency of the platform layer on application code.

## Documentation

| Doc | Covers |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Task list, boot sequence, event/notification wiring between tasks |
| [docs/tasks.md](docs/tasks.md) | What each task does: activity, vitals, environment, UI, BLE, haptics |
| [docs/drivers.md](docs/drivers.md) | Sensor/display drivers, peripheral bring-up, power behaviour |
| [docs/ble.md](docs/ble.md) | GATT service layout built on top of `core`'s svcctl framework |

For the RTOS/kernel, low-power modes, and BLE transport that this firmware is built on, see the [biowatch-core docs](https://github.com/Zeta-Chrome/biowatch-core/tree/main/docs).

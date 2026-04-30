MAKEFLAGS += --no-builtin-rules
.DELETE_ON_ERROR:

# Project
TARGET := BioWatch

# Tools
PREFIX := arm-none-eabi-
CC     := $(PREFIX)gcc
CP     := $(PREFIX)objcopy
SZ     := $(PREFIX)size

# MCU
MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# Arguments
DEBUG     ?= 1
LOGGER    ?= rtt
METHOD    ?= stlink
RTT_PORT  ?= 9001
UART_BAUD ?= 9600

ifeq ($(DEBUG), 1)
BUILD_DIR := build/debug
OPT       := -Og
DBGFLAGS  := -g3 -gdwarf-2
C_DEFS    := -DSTM32WB55xx -DDEBUG
else
BUILD_DIR := build/release
OPT       := -O2
DBGFLAGS  :=
C_DEFS    := -DSTM32WB55xx
endif

ifeq ($(LOGGER), rtt)
C_DEFS += -DRTT_LOGGER
else ifeq ($(LOGGER), uart)
C_DEFS += -DUART_LOGGER
else
$(error LOGGER must be rtt or uart)
endif

ifeq ($(METHOD), stlink)
else ifeq ($(METHOD), dfu)
ifeq ($(LOGGER), rtt)
$(error RTT requires STLink. Use LOGGER=uart with METHOD=dfu)
endif
else
$(error METHOD must be stlink or dfu)
endif

# Core submodule
CORE_DIR     := core
CORE_BUILD   := $(CORE_DIR)/build/$(if $(filter 1,$(DEBUG)),debug,release)
CORE_LIB     := $(CORE_BUILD)/libbwcore.a
CORE_STARTUP := $(CORE_BUILD)/startup/startup_stm32wb55xx_cm4.o \
                $(CORE_BUILD)/startup/vectors.o
LDSCRIPT     := $(CORE_DIR)/startup/stm32wb55xx_flash_cm4.ld

# Flags
CFLAGS := $(MCU) $(C_DEFS) $(OPT) $(DBGFLAGS)
CFLAGS += -Wall -Wextra -Wshadow
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -MMD -MP

LDFLAGS := $(MCU) --specs=nano.specs --specs=nosys.specs
LDFLAGS += -T$(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
LDFLAGS += -Wl,--gc-sections,--print-memory-usage
LDFLAGS += -L$(CORE_BUILD) -lbwcore -lc -lm -lnosys

# Includes
C_INCLUDES := \
    -I. \
    -Ibsp \
    -Idrivers \
    -I$(CORE_DIR) \
    -I$(CORE_DIR)/cmsis \
    -I$(CORE_DIR)/device

# Sources
C_SOURCES := $(shell find . -type f -name "*.c" \
                 -not -path "./core/*" \
                 -not -path "./$(BUILD_DIR)/*")

# Objects
OBJECTS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SOURCES))

# OpenOCD
OCD     := openocd -f openocd.cfg
OCD_RTT := -c "rtt_start $(RTT_PORT)"

# UART
UART_PORT := $(firstword $(wildcard /dev/ttyACM*) $(wildcard /dev/ttyUSB*))

# BLE stack
BLE_STACK_DIR  ?= $(HOME)/stm32wb_ble
BLE_STACK_BIN  ?= $(BLE_STACK_DIR)/stm32wb5x_BLE_Stack_full_fw.bin
BLE_STACK_ADDR := 0x080C0000

# Rules
.PHONY: all core flash monitor server debug flash_ble flash_all erase clean clean_all compiledb

all: $(BUILD_DIR)/$(TARGET).elf \
     $(BUILD_DIR)/$(TARGET).hex \
     $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "✓ Build complete!"
	@$(SZ) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(CORE_STARTUP) $(CORE_LIB) $(LDSCRIPT) Makefile
	@echo "  LD    $@"
	@$(CC) $(OBJECTS) $(CORE_STARTUP) $(CORE_LIB) $(LDFLAGS) -o $@

$(CORE_LIB) $(CORE_STARTUP): core

core:
	@$(MAKE) -C $(CORE_DIR) DEBUG=$(DEBUG) LOGGER=$(LOGGER)

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	@$(CP) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	@$(CP) -O binary -S $< $@

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) -c $(CFLAGS) $(C_INCLUDES) -MF"$(@:.o=.d)" -Wa,-a,-ad,-alms=$(@:.o=.lst) $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

# Flash M4
flash: all
ifeq ($(METHOD), stlink)
	@echo "Flashing via STLink..."
	@$(OCD) -c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"
else
	@echo "Flashing via DFU..."
	@STM32_Programmer_CLI -c port=USB1 -d $(BUILD_DIR)/$(TARGET).elf -v
endif

# Monitor
monitor:
ifeq ($(LOGGER), rtt)
	@echo "Starting RTT monitor (Ctrl+C to stop)..."
	@$(OCD) \
		-c "init" \
		-c "reset run" \
		$(OCD_RTT) & OCD_PID=$$!; \
		trap "kill $$OCD_PID 2>/dev/null; wait $$OCD_PID 2>/dev/null" INT TERM EXIT; \
		until nc -z localhost $(RTT_PORT) 2>/dev/null; do sleep 0.1; done; \
		nc localhost $(RTT_PORT); \
		kill $$OCD_PID 2>/dev/null; \
		wait $$OCD_PID 2>/dev/null
else
	@[ -n "$(UART_PORT)" ] || { echo "No serial port found"; exit 1; }
	@echo "Opening $(UART_PORT) @ $(UART_BAUD) baud"
	@$(OCD) -c "init; reset halt; exit" 2>/dev/null; \
		picocom -b $(UART_BAUD) $(UART_PORT) & PICOCOM_PID=$$!; \
		trap "kill $$PICOCOM_PID 2>/dev/null" EXIT; \
		sleep 0.3; \
		$(OCD) -c "init; resume; exit" 2>/dev/null; \
		wait $$PICOCOM_PID
endif

# Server — foreground OpenOCD for debug sessions
server:
ifeq ($(METHOD), stlink)
	@echo "OpenOCD server running on :3333 (GDB) and :$(RTT_PORT) (RTT)"
	@echo "Ctrl+C to stop"
	@$(OCD) \
		-c "init" \
		-c "reset run" \
		$(OCD_RTT)
else
	@echo "Server only supported with STLink"
endif

# Debug — attach GDB to running server (make server first)
debug:
ifeq ($(METHOD), stlink)
	@$(PREFIX)gdb \
		-ex "set remotetimeout 10" \
		-ex "target extended-remote :3333" \
		-ex "monitor reset halt" \
		-ex "break main" \
		-ex "continue" \
		$(BUILD_DIR)/$(TARGET).elf
else
	@echo "GDB only supported with STLink"
endif

# Flash M0+
flash_ble:
	@[ -f "$(BLE_STACK_BIN)" ] || { \
		echo "BLE stack not found: $(BLE_STACK_BIN)"; \
		echo "Set BLE_STACK_DIR=/path/to/binaries"; \
		exit 1; }
	@echo "Flashing BLE stack, do not disconnect..."
	@STM32_Programmer_CLI -c port=SWD freq=1000 reset=SWrst \
		-fwupgrade $(BLE_STACK_BIN) $(BLE_STACK_ADDR) firstinstall=1
	@echo "BLE stack flashed"

# Flash full chip
flash_all: flash_ble flash

erase:
	@$(OCD) -c "init; reset halt; stm32wbx mass_erase 0; reset run; exit"

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned $(BUILD_DIR)"

clean_all: clean
	@$(MAKE) -C $(CORE_DIR) clean

compiledb:
	@make clean_all
	@bear -- $(MAKE) -C $(CORE_DIR) DEBUG=$(DEBUG) LOGGER=$(LOGGER)
	@bear --append -- $(MAKE) DEBUG=$(DEBUG) LOGGER=$(LOGGER)
	@echo "compile_commands.json updated"

-include $(wildcard $(BUILD_DIR)/**/*.d)

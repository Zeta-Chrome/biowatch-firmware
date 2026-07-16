MAKEFLAGS += --no-builtin-rules
.DELETE_ON_ERROR:

# Project
TARGET := BioWatch

# Tools
PREFIX := arm-none-eabi-
CC     := $(PREFIX)gcc
AS     := $(PREFIX)gcc -x assembler-with-cpp
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

ifeq ($(METHOD), stlink)
else ifeq ($(METHOD), dfu)
ifeq ($(LOGGER), rtt)
$(error RTT requires STLink. Use LOGGER=uart with METHOD=dfu)
endif
else
$(error METHOD must be stlink or dfu)
endif

# Core submodule
include core/core.mk

# Linker script
LDSCRIPT := $(CORE_LDSCRIPT)

# Build dir, Options, flags and defs
BUILD_ROOT := build
ifeq ($(DEBUG), 1)
BUILD_DIR  := $(BUILD_ROOT)/debug
OPT        := -Og
DBGFLAGS   := -g3 -gdwarf-2
C_DEFS     := -DDEBUG
else
BUILD_DIR := $(BUILD_ROOT)/release
OPT       := -O2
DBGFLAGS  :=
C_DEFS    :=
endif

ifeq ($(LOGGER), rtt)
C_DEFS += -DRTT_LOGGER
else ifeq ($(LOGGER), uart)
C_DEFS += -DUART_LOGGER
else
$(error LOGGER must be rtt or uart)
endif
C_DEFS := $(C_DEFS) $(CORE_DEFS)

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
APP_INCS := -I. -Iboards
INCS := $(APP_INCS) $(addprefix -I, $(CORE_INCS))

# Sources
APP_C_SRCS := $(shell find . -type f -name "*.c" -not -path "./core/*" -not -path "./$(BUILD_DIR)/*")
C_SRCS := $(APP_C_SRCS) $(CORE_C_SRCS)
ASM_SRCS := $(CORE_ASM_SRCS)

# Objects
C_OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst %.s, $(BUILD_DIR)/%.o, $(ASM_SRCS))
OBJS := $(C_OBJS) $(ASM_OBJS)

# STM32_Programmer_CLI
STM32_PRG := ${STM32_PRG_PATH}/STM32_Programmer_CLI

# OpenOCD
OCD     := openocd -f openocd.cfg
OCD_RTT := -c "rtt_start $(RTT_PORT)"

# UART
UART_PORT := $(firstword $(wildcard /dev/ttyACM*) $(wildcard /dev/ttyUSB*))

# BLE stack
BLE_STACK_BIN  := $(CORE_BLE_STACK_BIN)
BLE_STACK_ADDR := 0x080D0000

# Rules
.PHONY: all flash flash_ble flash_all monitor server debug erase clean compiledb check_core

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "✓ Build complete!"
	@$(SZ) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	@$(CP) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	@$(CP) -O binary -S $< $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT) Makefile | $(BUILD_DIR)
	@echo "  LD    $@"
	@$(CC) $(OBJS) $(CORE_LIBS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) -c $(CFLAGS) $(INCS) -MF"$(@:.o=.d)" -Wa,-a,-ad,-alms=$(@:.o=.lst) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	@$(AS) -c $(MCU) $(OPT) $(DBGFLAGS) -MMD -MP -MF"$(@:.o=.d)" $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

# Flash M4
flash: all
ifeq ($(METHOD), stlink)
	@echo "Flashing via STLink..."
	@${STM32_PRG} -c port=SWD mode=UR reset=HWrst -d $(BUILD_DIR)/$(TARGET).elf -v -rst
else
	@echo "Flashing via DFU..."
	@${STM32_PRG} -c port=USB1 -d $(BUILD_DIR)/$(TARGET).elf -v
endif

# Flash M0+
flash_ble:
	@[ -f "$(BLE_STACK_BIN)" ] || { \
		echo "BLE stack not found: $(BLE_STACK_BIN)"; \
		echo "Set BLE_STACK_DIR=/path/to/binaries"; \
		exit 1; }
	@echo "Flashing BLE stack, do not disconnect..."
	@${STM32_PRG} -c port=SWD mode=UR freq=100 reset=HWrst -startfus
	@${STM32_PRG} -c port=SWD mode=UR freq=100 reset=HWrst -fwupgrade $(BLE_STACK_BIN) $(BLE_STACK_ADDR) firstinstall=0
	@${STM32_PRG} -c port=swd mode=UR -ob nSWboot0=1 nboot1=1 nboot0=1
	@echo "BLE stack flashed"

# Flash full chip
flash_all: flash_ble flash

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
		$(OCD_RTT) & OCD_PID=$$!; \
		trap "kill $$OCD_PID 2>/dev/null; wait $$OCD_PID 2>/dev/null" INT TERM EXIT; \
		until nc -z localhost $(RTT_PORT) 2>/dev/null; do sleep 0.1; done; \
		nc localhost $(RTT_PORT) & NC_PID=$$!; \
		wait $$OCD_PID
else
	@echo "Server only supported with STLink"
endif

# Debug — attach GDB to running server (make server first)
debug:
ifeq ($(METHOD), stlink)
	@$(PREFIX)gdb -tui\
		-ex "set remotetimeout 10" \
		-ex "target extended-remote :3333" \
		-ex "monitor reset halt" \
		-ex "break main" \
		-ex "continue" \
		$(BUILD_DIR)/$(TARGET).elf
else
	@echo "GDB only supported with STLink"
endif

recover:
	@${STM32_PRG} -c port=SWD mode=UR reset=HWrst freq=100 -ob RDP=0xAA PCROP_RDP=0
	sleep 2
	@${STM32_PRG} -c port=SWD mode=UR reset=HWrst freq=100 -w32 0x5800040C 0x00008000
	@${STM32_PRG} -c port=SWD mode=UR reset=HWrst freq=100 -ob displ

erase:
	@${STM32_PRG} -c port=SWD mode=UR reset=HWrst freq=100 -e all

clean:
	@rm -rf $(BUILD_ROOT)
	@echo "Cleaned $(BUILD_ROOT)"

compiledb:
	@make clean_all
	@bear -- $(MAKE) -C $(CORE_DIR) DEBUG=$(DEBUG) LOGGER=$(LOGGER)
	@bear --append -- $(MAKE) DEBUG=$(DEBUG) LOGGER=$(LOGGER)
	@echo "compile_commands.json updated"

CHECK_DIR := build_core
check_core:
	@echo "Compiling core in isolation (no app include paths)..."
	@mkdir -p $(CHECK_DIR)
	@for src in $(CORE_SRCS); do \
	  out=$(CHECK_DIR)/$$(echo $$src | sed 's/\.c$$/.o/'); \
	  mkdir -p $$(dirname $$out); \
	  $(CC) -c $(MCU) $(CORE_DEFS) -Wall -Wextra \
	    $(addprefix -I,$(CORE_INC)) $$src -o $$out || exit 1; \
	done
	@for src in $(CORE_ASM); do \
	  out=$(CHECK_DIR)/$$(echo $$src | sed 's/\.s$$/.o/'); \
	  mkdir -p $$(dirname $$out); \
	  $(AS) -c $(MCU) $$src -o $$out || exit 1; \
	done
	@echo "core compiles standalone — no leaked app dependency."
	@rm -rf $(CHECK_DIR)

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

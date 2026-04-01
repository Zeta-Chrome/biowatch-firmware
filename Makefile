MAKEFLAGS += --no-builtin-rules
.DELETE_ON_ERROR:

TARGET := BioWatch

PREFIX := arm-none-eabi-
CC     := $(PREFIX)gcc
AS     := $(PREFIX)gcc -x assembler-with-cpp
CP     := $(PREFIX)objcopy
SZ     := $(PREFIX)size
HEX    := $(CP) -O ihex
BIN    := $(CP) -O binary -S

CPU       = -mcpu=cortex-m4
FPU       = -mfpu=fpv4-sp-d16
FLOAT_ABI = -mfloat-abi=hard
MCU       = $(CPU) -mthumb $(FPU) $(FLOAT_ABI)

DEBUG ?= 1
C_DEFS = -DSTM32WB55xx
ifeq ($(DEBUG), 1)
BUILD_DIR := build/debug
C_DEFS  += -DDEBUG
OPT      = -Og
DBGFLAGS = -g3 -gdwarf-2
else
BUILD_DIR := build/release
OPT      = -O2
DBGFLAGS =
endif

# Core submodule 
CORE_DIR   := core
CORE_BUILD := $(CORE_DIR)/build/$(if $(filter 1,$(DEBUG)),debug,release)
CORE_LIB   := $(CORE_BUILD)/libbwcore.a
CORE_STARTUP_C   := $(CORE_BUILD)/startup/vectors.o
CORE_STARTUP_ASM := $(CORE_BUILD)/startup/startup.o
LDSCRIPT   := $(CORE_DIR)/startup/stm32wb55xx_flash_cm4.ld

CFLAGS  = $(MCU) $(C_DEFS) $(OPT) $(DBGFLAGS)
CFLAGS += -Wall -Wextra -Wshadow
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
ASFLAGS = $(MCU) $(OPT) $(DBGFLAGS)

C_INCLUDES := \
    -I. \
    -Ibsp \
    -Idrivers \
    -I$(CORE_DIR) \
    -I$(CORE_DIR)/cmsis \
    -I$(CORE_DIR)/device \
    -I$(CORE_DIR)/hal \
    -I$(CORE_DIR)/utils

LIBS    = -lc -lm -lnosys
LIBDIR  = -L$(CORE_BUILD)
LDFLAGS = $(MCU) --specs=nano.specs --specs=nosys.specs
LDFLAGS += -T$(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--print-memory-usage
LDFLAGS += $(LIBDIR) -lbwcore $(LIBS)

C_SOURCES := $(shell find . -type f -name "*.c" \
                 -not -path "./core/*" \
                 -not -path "./$(BUILD_DIR)/*")

OBJECTS  := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
OBJECTS  += $(CORE_STARTUP_C)
OBJECTS  += $(CORE_STARTUP_ASM)

# Rules
all: core $(BUILD_DIR)/$(TARGET).elf \
         $(BUILD_DIR)/$(TARGET).hex \
         $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "✓ Build complete!"
	@$(SZ) $(BUILD_DIR)/$(TARGET).elf

core:
	@$(MAKE) -C $(CORE_DIR) DEBUG=$(DEBUG)

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(CORE_LIB) Makefile
	@echo "  LD    $@"
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	@echo "  HEX   $@"
	@$(HEX) $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	@echo "  BIN   $@"
	@$(BIN) $< $@

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) -c $(CFLAGS) $(C_INCLUDES) -Wa,-a,-ad,-alms=$(@:.o=.lst) $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

# Flash
METHOD ?= stlink
ifeq ($(METHOD), stlink)
FLASH_FLAGS := port=SWD freq=4000 reset=SWrst -rst
else ifeq ($(METHOD), dfu)
FLASH_FLAGS := port=USB1
else
$(error Invalid METHOD '$(METHOD)'. Valid options are: stlink, dfu)
endif

flash: all
	@echo "Flashing via $(METHOD)..."
	@STM32_Programmer_CLI -c $(FLASH_FLAGS) -d $(BUILD_DIR)/$(TARGET).elf -v
	@echo "Flash complete!"

erase:
	@STM32_Programmer_CLI -c $(FLASH_FLAGS) -e all
	@echo "Erase complete!"

# Monitor
SERIAL_PORT := $(firstword $(wildcard /dev/ttyACM*) $(wildcard /dev/ttyUSB*))
BAUD ?= 9600

monitor:
	@if [ -z "$(SERIAL_PORT)" ]; then \
		echo "No serial port found"; exit 1; \
	fi
	@echo "Opening $(SERIAL_PORT) @ $(BAUD) baud"
	@sudo picocom -b $(BAUD) $(SERIAL_PORT)

flash_monitor: flash
	$(MAKE) monitor

# Clean
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

clean_all:
	@rm -rf build/
	@$(MAKE) -C $(CORE_DIR) clean_all
	@echo "Clean all complete!"

compiledb:
	@bear -- $(MAKE) DEBUG=1
	@echo "✓ compile_commands.json updated"

server:
	@openocd -f interface/stlink.cfg -f target/stm32wbx.cfg

debug: all
	@$(PREFIX)gdb \
		-ex "target extended-remote :3333" \
		-ex "monitor reset halt" \
		-ex "load" \
		$(BUILD_DIR)/$(TARGET).elf

-include $(wildcard $(BUILD_DIR)/**/*.d)

.PHONY: all core flash erase monitor flash_monitor clean clean_all compiledb server debug

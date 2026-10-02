# Optional GCC build (arm-none-eabi-gcc). The main project is IAR EWARM:
# open EWARM/SolderStation.eww. This Makefile is handy for CI and quick checks.
#
#   make            - build build/SolderStation.elf/.hex/.bin
#   make clean

TARGET   = SolderStation
BUILD    = build

PREFIX  ?= arm-none-eabi-
CC       = $(PREFIX)gcc
AS       = $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

SRCS = $(wildcard Core/Src/*.c)
ASMS = gcc/startup_stm32f401xc.s

INCS = -ICore/Inc \
       -IDrivers/CMSIS/Include \
       -IDrivers/CMSIS/Device/ST/STM32F4xx/Include

DEFS = -DSTM32F401xC -DHSE_VALUE=25000000

MCU  = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

CFLAGS  = $(MCU) $(DEFS) $(INCS) -Os -g3 -std=c11 \
          -Wall -Wextra -Wno-unused-parameter -Wshadow \
          -Wmissing-prototypes -Wstrict-prototypes \
          -ffunction-sections -fdata-sections -fsingle-precision-constant
LDFLAGS = $(MCU) -specs=nano.specs -specs=nosys.specs -Tgcc/stm32f401xc.ld \
          -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage -lm

OBJS = $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o))) \
       $(addprefix $(BUILD)/,$(notdir $(ASMS:.s=.o)))

vpath %.c $(sort $(dir $(SRCS)))
vpath %.s $(sort $(dir $(ASMS)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	$(CC) -c $(CFLAGS) -MMD -MP $< -o $@

$(BUILD)/%.o: %.s Makefile | $(BUILD)
	$(AS) -c $(MCU) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)

.PHONY: all clean

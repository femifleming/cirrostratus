# Cirrostratus - alternative firmware for the Monsoon.
#   make -f monsoon/makefile          build build/monsoon/monsoon.bin
#   make -f monsoon/makefile wav      firmware update as audio (bootloader)
#   make -f monsoon/makefile upload   flash over SWD (ST-Link + OpenOCD)

F_CRYSTAL      = 8000000L
F_CPU          = 168000000L
SYSCLOCK       = SYSCLK_FREQ_168MHz
FAMILY         = f4xx

APPLICATION_LARGE = TRUE
BOOTLOADER        = monsoon_bootloader

UPLOAD_COMMAND  = upload_jtag

TARGET         = cirrostratus
PACKAGES       = monsoon \
		monsoon/drivers \
		monsoon/dsp \
		monsoon/dsp/modes \
		monsoon/dsp/cmsis \
		stmlib/utils \
		stmlib/system
RESOURCES      =

TOOLCHAIN_PATH ?= $(dir $(shell which arm-none-eabi-gcc))../

include stmlib/makefile.inc

# Our own linker script (keeps libm, stops before the save-slot sectors).
LINKER_SCRIPT = monsoon/monsoon.ld
CCFLAGS += -fno-math-errno -Wno-unused-local-typedefs

# Firmware update as a WAV for the Clouds/Monsoon audio bootloader.
wav: $(TARGET_BIN)
	python3 tools/qpsk_encoder.py -s 48000 -b 12000 -c 6000 -p 256 \
		-o $(BUILD_DIR)cirrostratus.wav $(TARGET_BIN)

# Link libm after the objects (stmlib's rule puts libraries first).
$(TARGET_ELF): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET_ELF) $(OBJS) -lm

# ---- Flashing over SWD (ST-Link + OpenOCD: brew install open-ocd) --------
OPENOCD_CMD = openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# Just the application (keeps the bootloader, settings and save slots).
flash: $(TARGET_BIN)
	$(OPENOCD_CMD) -c "program $(TARGET_BIN) 0x08008000 verify reset exit"

# Bootloader + application, for a blank chip (also wipes the settings).
combo: $(COMBO_BIN)
flash_all: $(COMBO_BIN)
	$(OPENOCD_CMD) -c "program $(COMBO_BIN) 0x08000000 verify reset exit"

.PHONY: flash flash_all combo

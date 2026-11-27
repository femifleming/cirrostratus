# Cirrostratus - alternative firmware for the Typhoon (STM32F427 + WM8731).
# Same DSP as the Monsoon build (monsoon/dsp), Typhoon panel code in typhoon/.
#   make -f typhoon/makefile          build/cirrostratus_typhoon/cirrostratus_typhoon.bin
#   make -f typhoon/makefile wav      firmware update as audio (bootloader)
#   make -f typhoon/makefile flash    flash over SWD (ST-Link + OpenOCD)
#   make -f typhoon/bootloader/makefile && make -f typhoon/makefile combo
#                                     bootloader + application, for a blank chip
#
# The chip is an STM32F427, built with the F405 device define (as the stock
# SuperParasites code does for every peripheral used here); the image stays
# in flash bank 1, laid out exactly like the Monsoon's.

F_CRYSTAL      = 8000000L
F_CPU          = 168000000L
SYSCLOCK       = SYSCLK_FREQ_168MHz
FAMILY         = f4xx

APPLICATION_LARGE = TRUE
BOOTLOADER        = typhoon_bootloader

UPLOAD_COMMAND  = upload_jtag

TARGET         = cirrostratus_typhoon
PACKAGES       = typhoon \
		typhoon/drivers \
		monsoon/dsp \
		monsoon/dsp/modes \
		monsoon/dsp/cmsis \
		stmlib/utils \
		stmlib/system
RESOURCES      =

TOOLCHAIN_PATH ?= $(dir $(shell which arm-none-eabi-gcc))../

include stmlib/makefile.inc

# Our own linker script (keeps libm, stops before the save-slot sectors).
LINKER_SCRIPT = typhoon/typhoon.ld
CCFLAGS += -fno-math-errno -Wno-unused-local-typedefs

# Firmware update as a WAV for the stock (SuperParasites / Clouds) audio
# bootloader: same parameters as the Monsoon's.
wav: $(TARGET_BIN)
	python3 tools/qpsk_encoder.py -s 48000 -b 12000 -c 6000 -p 256 \
		-o $(BUILD_DIR)cirrostratus-typhoon.wav $(TARGET_BIN)

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

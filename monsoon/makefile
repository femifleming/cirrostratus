# Monsoon HiFi firmware.
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

TARGET         = monsoon
PACKAGES       = monsoon \
		monsoon/drivers \
		monsoon/dsp \
		monsoon/dsp/modes \
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
		-o $(BUILD_DIR)monsoon_hifi.wav $(TARGET_BIN)

# Link libm after the objects (stmlib's rule puts libraries first).
$(TARGET_ELF): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET_ELF) $(OBJS) -lm

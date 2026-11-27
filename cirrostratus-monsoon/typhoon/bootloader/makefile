# Audio bootloader for the Typhoon (the Clouds/Monsoon one with the Typhoon's
# drivers). Only needed for a module that has no bootloader yet (blank chip).
#   make -f typhoon/bootloader/makefile

F_CRYSTAL      = 8000000L
F_CPU          = 168000000L
SYSCLOCK       = SYSCLK_FREQ_168MHz
FAMILY         = f4xx

TARGET         = typhoon_bootloader
PACKAGES       = typhoon/bootloader \
		typhoon/drivers \
		stm_audio_bootloader/qpsk \
		stmlib/utils \
		stmlib/system
RESOURCES      =

TOOLCHAIN_PATH ?= $(dir $(shell which arm-none-eabi-gcc))../

include stmlib/makefile.inc

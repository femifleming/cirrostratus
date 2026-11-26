# Audio bootloader (the Clouds/Monsoon one, unchanged apart from paths).
# Only needed for a module that has no bootloader yet.
#   make -f monsoon/bootloader/makefile

F_CRYSTAL      = 8000000L
F_CPU          = 168000000L
SYSCLOCK       = SYSCLK_FREQ_168MHz
FAMILY         = f4xx

TARGET         = monsoon_bootloader
PACKAGES       = monsoon/bootloader \
		monsoon/drivers \
		stm_audio_bootloader/qpsk \
		stmlib/utils \
		stmlib/system
RESOURCES      =

TOOLCHAIN_PATH ?= $(dir $(shell which arm-none-eabi-gcc))../

include stmlib/makefile.inc

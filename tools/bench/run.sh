#!/bin/sh
# Builds the benchmark image and runs it on QEMU's STM32F405 model.
set -e
cd "$(dirname "$0")/../.."
OUT=${BENCH_OUT:-build/bench}
mkdir -p $OUT
FLAGS="-O2 -g -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
  -fno-math-errno -fno-use-cxa-atexit -fdata-sections -ffunction-sections -fshort-enums -I. $BENCH_DEFS"
arm-none-eabi-gcc $FLAGS -c tools/bench/startup.c -o $OUT/startup.o
arm-none-eabi-gcc $FLAGS -w -Istmlib/third_party/STM/CMSIS/CM3_f4xx -c monsoon/dsp/cmsis/cmsis_fft.c -o $OUT/cmsis_fft.o
for f in tools/bench/bench_main.cc monsoon/runtime.cc monsoon/dsp/*.cc monsoon/dsp/modes/*.cc; do
  [ -f "$f" ] || continue
  arm-none-eabi-g++ $FLAGS -std=gnu++14 -fno-exceptions -fno-rtti -c "$f" \
    -o "$OUT/$(basename "$f").o"
done
arm-none-eabi-g++ $FLAGS -nostartfiles -Wl,--gc-sections -T tools/bench/bench.ld \
  $OUT/*.o -lm -lc -lnosys -o $OUT/bench.elf
arm-none-eabi-size $OUT/bench.elf
timeout 3000 qemu-system-arm -M netduinoplus2 -display none -serial none \
  -semihosting-config enable=on,target=native -icount shift=0,sleep=off \
  -kernel $OUT/bench.elf

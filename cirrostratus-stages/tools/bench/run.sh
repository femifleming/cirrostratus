#!/bin/sh
# Cirrostratus Stages CPU benchmark on QEMU's Cortex-M4F (netduinoplus2 model).
set -e
cd "$(dirname "$0")/../.."
OUT=build/bench
mkdir -p $OUT
FLAGS="-O2 -g -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
  -fno-use-cxa-atexit -fdata-sections -ffunction-sections -fshort-enums \
  -funroll-loops -finline-functions-called-once -fno-move-loop-invariants -I. $BENCH_DEFS"
arm-none-eabi-gcc $FLAGS -c tools/bench/startup.c -o $OUT/startup.o
for f in tools/bench/bench_main.cc stages/dobbel.cc stages/dobbel_bank.cc stages/resources.cc stmlib/utils/random.cc; do
  arm-none-eabi-g++ $FLAGS -std=c++11 -fno-exceptions -fno-rtti -c "$f" -o "$OUT/$(basename "$f").o"
done
arm-none-eabi-g++ $FLAGS -nostartfiles -Wl,--gc-sections -T tools/bench/bench.ld \
  $OUT/*.o -lc -lnosys -o $OUT/bench.elf
timeout 600 qemu-system-arm -M netduinoplus2 -display none -serial none \
  -semihosting-config enable=on,target=native -icount shift=0,sleep=off \
  -kernel $OUT/bench.elf

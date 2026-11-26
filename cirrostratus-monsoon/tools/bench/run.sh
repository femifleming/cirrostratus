#!/bin/sh
# Builds a benchmark image and runs it on QEMU's STM32F405 model.
#   tools/bench/run.sh                    CPU per mode (bench_main.cc)
#   BENCH_MAIN=verify_arm tools/bench/run.sh   every mode/option checked
#                                         (verify.sh compares with the Mac)
# The DSP is compiled with exactly the firmware's flags (stmlib/makefile.inc
# + monsoon/makefile: -funroll-loops, -fno-move-loop-invariants, the
# STM32F4/ARM_MATH_CM4 defines ...), so what runs here is what the module
# runs.
set -e
cd "$(dirname "$0")/../.."
OUT=${BENCH_OUT:-build/bench}
MAIN=${BENCH_MAIN:-bench_main}
mkdir -p $OUT
rm -f $OUT/*.o
FLAGS="-g -O2 -Wall -Werror -Wno-unused-local-typedefs -Wno-strict-aliasing \
  -fasm -finline -finline-functions-called-once -fdata-sections \
  -ffunction-sections -fshort-enums -fno-move-loop-invariants \
  -fno-unwind-tables -I. -DGCC_ARMCM4 -DSTM32F40_41xxx -DSTM32F4XX \
  -DARM_MATH_CM4 -D__FPU_PRESENT -DAPPLICATION -DF_CPU=168000000L \
  -DF_CRYSTAL=8000000L -DHSE_VALUE=8000000L -DUSE_STDPERIPH_DRIVER \
  -DSYSCLK_FREQ_168MHz=168000000L -Istmlib/third_party/STM \
  -Istmlib/third_party/STM/CMSIS/CM3_f4xx \
  -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
  -mthumb-interwork -funroll-loops -fno-math-errno $BENCH_DEFS"
CXXFLAGS="-fno-exceptions -fno-rtti -fno-use-cxa-atexit"
# Harness files (not firmware): the firmware flags minus -Werror.
HFLAGS=$(echo "$FLAGS" | sed 's/-Werror//')
arm-none-eabi-gcc $HFLAGS -std=c99 -c tools/bench/startup.c -o $OUT/startup.o
arm-none-eabi-gcc $FLAGS -std=c99 -w -c monsoon/dsp/cmsis/cmsis_fft.c -o $OUT/cmsis_fft.o
arm-none-eabi-g++ $HFLAGS $CXXFLAGS -c tools/bench/$MAIN.cc -o $OUT/$MAIN.o
for f in monsoon/runtime.cc monsoon/dsp/*.cc monsoon/dsp/modes/*.cc; do
  [ -f "$f" ] || continue
  arm-none-eabi-g++ $FLAGS $CXXFLAGS -c "$f" -o "$OUT/$(basename "$f").o"
done
arm-none-eabi-g++ $FLAGS -nostartfiles -Wl,--gc-sections -T tools/bench/bench.ld \
  $OUT/*.o -lm -lc -lnosys -o $OUT/$MAIN.elf
arm-none-eabi-size $OUT/$MAIN.elf
timeout ${BENCH_TIMEOUT:-3000} qemu-system-arm -M netduinoplus2 -display none -serial none \
  -semihosting-config enable=on,target=native -icount shift=0,sleep=off \
  -kernel $OUT/$MAIN.elf

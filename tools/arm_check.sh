#!/bin/sh
# Compiles every DSP file with the firmware's compiler and warning flags
# (-Wall -Werror, Cortex-M4F). Usage: tools/arm_check.sh [files...]
cd "$(dirname "$0")/.." || exit 1
OUT=$(mktemp -d)
FILES=${*:-$(ls monsoon/dsp/*.cc monsoon/dsp/modes/*.cc 2>/dev/null)}
status=0
for f in $FILES; do
  arm-none-eabi-g++ -c -O2 -std=gnu++14 -Wall -Werror -Wno-unused-local-typedefs \
    -Wno-strict-aliasing -fno-exceptions -fno-rtti -fno-math-errno \
    -fdata-sections -ffunction-sections -fshort-enums \
    -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
    -I. "$f" -o "$OUT/$(basename "$f").o" || status=1
done
rm -rf "$OUT"
[ $status = 0 ] && echo "arm-none-eabi-g++: all files compile cleanly"
exit $status

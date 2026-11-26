#!/bin/sh
# In-depth check of every mode/option on QEMU's STM32F405 (the ARM build,
# firmware flags) against the same run on the Mac. ~10 minutes.
#   tools/bench/verify.sh [blocks]   (default 1500 = 1 s per run)
set -e
cd "$(dirname "$0")/../.."
BLOCKS=${1:-1500}
make -C host verify >/dev/null
./host/build/verify $BLOCKS > build/verify_host.txt
BENCH_MAIN=verify_arm BENCH_OUT=build/verify BENCH_DEFS="-DVERIFY_BLOCKS=$BLOCKS" \
  sh tools/bench/run.sh > build/verify_arm.txt 2>&1
python3 tools/bench/verify_compare.py build/verify_arm.txt build/verify_host.txt

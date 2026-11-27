#!/bin/bash
# Instruction-level profile of one layout (six segments of one type and
# flavour) on QEMU's Cortex-M4F: every executed instruction's address, then
# a histogram by source line and by function.
#   tools/bench/profile.sh <type 0-5> <flavour 0-7> [top N]
set -e
cd "$(dirname "$0")/../.."
T=$1; F=$2; N=${3:-25}
OUT=build/bench; mkdir -p $OUT
BENCH_DEFS="-DBENCH_TYPE=$T -DBENCH_FLAVOUR=$F" bash tools/bench/run.sh > $OUT/profile_run.txt 2>&1 || true
cat $OUT/profile_run.txt | grep insns
rm -f $OUT/trace.log
timeout 900 qemu-system-arm -M netduinoplus2 -display none -serial none \
  -semihosting-config enable=on,target=native -accel tcg,one-insn-per-tb=on \
  -d exec,nochain -D $OUT/trace.log -kernel $OUT/bench.elf > /dev/null 2>&1 || true
python3 - "$OUT" "$N" <<'PY'
import collections, re, subprocess, sys
out, n = sys.argv[1], int(sys.argv[2])
pcs = collections.Counter()
with open(out + "/trace.log", errors="ignore") as f:
    for ln in f:
        m = re.search(r"\[[0-9a-f]+/([0-9a-f]+)/", ln)
        if m:
            pcs[int(m.group(1), 16)] += 1
total = sum(pcs.values())
addrs = list(pcs)
res = subprocess.run(["arm-none-eabi-addr2line", "-f", "-C", "-e", out + "/bench.elf"] +
                     ["%x" % a for a in addrs], capture_output=True, text=True).stdout.split("\n")
by_line, by_fn = collections.Counter(), collections.Counter()
for i, a in enumerate(addrs):
    fn, loc = res[2 * i], res[2 * i + 1]
    loc = re.sub(r".*/", "", loc)
    by_line[loc] += pcs[a]
    by_fn[re.sub(r"\(.*", "", fn)] += pcs[a]
print("functions:")
for k, v in by_fn.most_common(12):
    print("  %5.1f %%  %s" % (100.0 * v / total, k))
# the function each instruction really ran in (inlined helpers counted in
# their caller), and the inlined helpers by caller
import bisect
syms = []
for l in subprocess.run(["arm-none-eabi-nm", "-C", "-n", out + "/bench.elf"],
                        capture_output=True, text=True).stdout.split("\n"):
    p = l.split(" ", 2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), re.sub(r"\(.*", "", p[2])))
syms.sort()
starts = [a for a, _ in syms]
owner, helper = collections.Counter(), collections.Counter()
for i, a in enumerate(addrs):
    o = syms[bisect.bisect_right(starts, a) - 1][1]
    owner[o] += pcs[a]
    fn = re.sub(r"\(.*", "", res[2 * i])
    if fn != o:
        helper["%s in %s" % (fn, o)] += pcs[a]
print("real functions:")
for k, v in owner.most_common(8):
    print("  %5.1f %%  %s" % (100.0 * v / total, k))
print("inlined helpers, by caller:")
for k, v in helper.most_common(10):
    print("  %5.1f %%  %s" % (100.0 * v / total, k))
print("lines:")
for k, v in by_line.most_common(n):
    print("  %5.1f %%  %s" % (100.0 * v / total, k))
PY
rm -f $OUT/trace.log

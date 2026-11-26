#!/usr/bin/env python3
"""Compares the verify run on QEMU (ARM build, firmware flags) with the same
run on the Mac. Usage: verify_compare.py arm.txt host.txt

Checks, for every mode / option / run:
  - the mode starts (init=1) on both
  - no clipped samples
  - fully dry is the input (within dither: dev <= 2 LSB)
  - ARM and Mac agree: same level within 1 dB (bit-exact is reported but not
    required: the ARM build fuses multiply-adds and uses its own libm)
  - CPU (instructions per frame; the module has 3500 cycles): a sustained
    mean above 2200 or any block above 2400 is a problem; a block above
    2150 is a note (the real module glitched around there in RESONATOR; one
    late block = the engine sheds the mode for one block)
"""
import math, re, sys

BUDGET = 2150
LINE = re.compile(r"^V (\S+) (\d+) (\S+) (.*)$")

def load(path):
    rows, fault = {}, False
    for line in open(path, errors="replace"):
        if line.startswith("FAULT"):
            fault = True
        m = LINE.match(line.strip())
        if m:
            kv = dict(x.split("=") for x in m.group(4).split())
            rows[(m.group(1), int(m.group(2)), m.group(3))] = {k: int(v) for k, v in kv.items()}
    return rows, fault

def db(a, b):
    return 20 * math.log10(max(a, 1) / max(b, 1))

arm, arm_fault = load(sys.argv[1])
host, _ = load(sys.argv[2])
problems, notes = [], []
exact = 0
if arm_fault:
    problems.append("the ARM build took a CPU fault")
missing = sorted(set(host) - set(arm))
if missing:
    problems.append(f"{len(missing)} runs missing on ARM, first {missing[0]}")
modes = sorted({k[0] for k in host}, key=lambda m: [k for k in host].index((m, 0, "typical")))
print(f"{'mode':<11} {'opt':>3}  {'typical dB':>10} {'heavy dB':>9} {'ARM-Mac':>8} {'dry dev':>7} {'mean':>5} {'worst blk':>9}")
for key in sorted(arm, key=lambda k: (list(host).index(k) if k in host else 0)):
    a = arm[key]
    h = host.get(key)
    name, opt, run = key
    if not a["init"]:
        problems.append(f"{name} {opt} {run}: did not start on ARM")
    if a["clips"]:
        problems.append(f"{name} {opt} {run}: {a['clips']} clipped samples")
    if run == "dry" and a["dev"] > 2:
        problems.append(f"{name} {opt}: dry output differs from the input by {a['dev']} LSB")
    if h:
        d = db(a["rms"], h["rms"])
        if a["hash"] == h["hash"] and a["rms"] == h["rms"]:
            exact += 1
        if abs(d) > 1.0:
            problems.append(f"{name} {opt} {run}: ARM {d:+.2f} dB from the Mac")
        a["vs"] = d
    if a.get("mean", 0) > 2200:
        problems.append(f"{name} {opt} {run}: mean {a['mean']} insns/frame (> 2200)")
    if a["worst"] > 2400:
        problems.append(f"{name} {opt} {run}: slowest block {a['worst']} insns/frame at block {a.get('at')}")
    elif a["worst"] > BUDGET:
        notes.append(f"{name} {opt} {run}: one block of {a['worst']} insns/frame at block {a.get('at')} (mean {a.get('mean')})")
for (name, opt, run), a in arm.items():
    if run != "typical":
        continue
    t = a; hv = arm.get((name, opt, "heavy"), {}); dr = arm.get((name, opt, "dry"), {})
    lvl = lambda r: 20 * math.log10(max(r.get("rms", 1), 1) / 1000 / 32768)
    worst = max(t["worst"], hv.get("worst", 0), dr.get("worst", 0))
    vs = max(abs(t.get("vs", 0)), abs(hv.get("vs", 0)), abs(dr.get("vs", 0)))
    mean = max(t.get("mean", 0), hv.get("mean", 0))
    print(f"{name:<11} {opt:>3}  {lvl(t):>10.1f} {lvl(hv):>9.1f} {vs:>8.2f} {dr.get('dev', -1):>7} {mean:>5} {worst:>9}")
print(f"\n{len(arm)} runs on ARM, {exact} bit-identical to the Mac")
if notes:
    print("notes:")
    for n in notes:
        print("  " + n)
if problems:
    print("PROBLEMS:")
    for p in problems:
        print("  " + p)
    sys.exit(1)
print("all checks passed")

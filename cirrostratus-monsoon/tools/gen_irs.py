#!/usr/bin/env python3
"""Synthesises FALTUNG's 8 impulse responses and writes them as int16
partition spectra (monsoon/dsp/modes/faltung_irs.cc), plus the first block of
IR 0 in the time domain for the host tests (host/faltung_ir_ref.h).

Everything is generated here from noise, delays and resonances - no
recorded or third-party impulse responses.
"""
import os

import numpy as np

FS = 48000
B = 256
N = 2 * B
MAX_PARTS = 32
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rng = np.random.default_rng(1234)


def onepole_lp(x, fc):
    a = 1 - np.exp(-2 * np.pi * fc / FS)
    y = np.empty_like(x)
    s = 0.0
    for i, v in enumerate(x):
        s += a * (v - s)
        y[i] = s
    return y


def biquad(x, b, a):
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def peak(x, fc, q, db):
    A = 10 ** (db / 40)
    w = 2 * np.pi * fc / FS
    al = np.sin(w) / (2 * q)
    b = np.array([1 + al * A, -2 * np.cos(w), 1 - al * A])
    a = np.array([1 + al / A, -2 * np.cos(w), 1 - al / A])
    return biquad(x, b / a[0], a / a[0])


def highpass(x, fc):
    w = 2 * np.pi * fc / FS
    al = np.sin(w) / (2 * 0.7071)
    c = np.cos(w)
    b = np.array([(1 + c) / 2, -(1 + c), (1 + c) / 2])
    a = np.array([1 + al, -2 * c, 1 - al])
    return biquad(x, b / a[0], a / a[0])


def decaying_noise(n, t60, bright_hz, dark_hz, seed):
    """Diffuse tail: noise whose highs decay faster than its lows."""
    r = np.random.default_rng(seed)
    t = np.arange(n) / FS
    x = r.standard_normal(n)
    lo = onepole_lp(x, dark_hz)
    hi = x - lo
    env_lo = 10 ** (-3 * t / t60)
    env_hi = 10 ** (-3 * t / (t60 * 0.45))
    hi = onepole_lp(hi, bright_hz)
    return lo * env_lo + hi * env_hi


def taps(n, delays_ms, gains, lp_hz=None):
    x = np.zeros(n)
    for d, g in zip(delays_ms, gains):
        i = int(d * FS / 1000)
        if i < n:
            x[i] += g
    if lp_hz:
        x = onepole_lp(x, lp_hz)
    return x


def fade_end(x, ms=20):
    k = int(ms * FS / 1000)
    x[-k:] *= np.cos(np.linspace(0, np.pi / 2, k)) ** 2
    return x


def modes(n, freqs, t60s, amps, seed):
    r = np.random.default_rng(seed)
    t = np.arange(n) / FS
    x = np.zeros(n)
    for f, t60, a in zip(freqs, t60s, amps):
        x += a * np.sin(2 * np.pi * f * t + r.uniform(0, 2 * np.pi)) * \
            10 ** (-3 * t / t60)
    return x


def room(ch):
    n = 8192
    er_d = np.sort(rng.uniform(2, 22, 10))
    er = taps(n, er_d, rng.choice([-1, 1], 10) * 0.5 * np.exp(-er_d / 25), 9000)
    tail = decaying_noise(n, 0.38, 9000, 2500, 10 + ch) * 0.35
    k = int(0.012 * FS)
    tail[:k] *= np.linspace(0, 1, k)
    x = er + tail
    x[0] += 0.8
    return fade_end(x)


def hall(ch):
    n = 8192
    pre = int(0.022 * FS)
    er_d = np.sort(rng.uniform(22, 80, 18))
    er = taps(n, er_d, rng.choice([-1, 1], 18) * 0.4 * np.exp(-(er_d - 22) / 60), 6000)
    tail = decaying_noise(n, 1.9, 7000, 1500, 20 + ch) * 0.4
    build = np.clip((np.arange(n) - pre) / (0.06 * FS), 0, 1) ** 2
    x = er + tail * build
    x[0] += 0.3
    return fade_end(x, 40)


def plate(ch):
    n = 8192
    x = decaying_noise(n, 1.4, 14000, 5000, 30 + ch)
    x[:64] += np.hanning(128)[64:] * 2.0
    return fade_end(x, 40)


def spring(ch):
    n = 8192
    x = np.zeros(n)
    # Every echo is a chirp: in a spring the low frequencies travel slower.
    period = int((0.031 + 0.004 * ch) * FS)
    t = np.arange(int(0.025 * FS)) / FS
    f0, f1 = 4500.0, 300.0
    chirp = np.sin(2 * np.pi * (f0 * t + (f1 - f0) * t ** 2 / (2 * t[-1]))) * \
        np.hanning(len(t))
    for k in range(8):
        i = 40 + k * period
        if i + len(chirp) < n:
            x[i:i + len(chirp)] += chirp * (0.72 ** k) * (-1) ** k
    x += decaying_noise(n, 0.9, 5000, 1500, 40 + ch) * 0.08
    return fade_end(onepole_lp(x, 5000))


def cabinet(ch):
    # A 2x12 guitar cabinet: low resonance, presence peak, cone break-up,
    # steep fall above 5 kHz.
    n = 1024
    x = np.zeros(n)
    x[0] = 1.0
    for fc, q, g in [(110, 2.0, 2.5), (2300, 3.0, 1.8), (900, 1.5, -0.6),
                     (3400, 4.0, 1.2)]:
        x = peak(x, fc + 20 * ch, q, g)
    x = onepole_lp(onepole_lp(onepole_lp(x, 5200), 5200), 6000)
    x = highpass(x, 75)
    return fade_end(x, 5)


def body(ch):
    # A wooden instrument body: air mode, top-plate modes, wood damping.
    n = 4096
    f = [102, 205, 280, 395, 460, 540, 680, 820, 1050, 1300, 1700, 2200]
    f = [v * (1 + 0.01 * ch) for v in f]
    t60 = [0.35, 0.25, 0.22, 0.18, 0.16, 0.14, 0.12, 0.1, 0.08, 0.07, 0.05, 0.04]
    a = [1.0, 0.8, 0.7, 0.6, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25, 0.2, 0.15]
    x = modes(n, f, t60, a, 60 + ch) * 0.3
    x[0] += 0.5
    return fade_end(x, 10)


def metal(ch):
    # A thin metal sheet: many inharmonic plate modes, long and shimmering.
    n = 8192
    r = np.random.default_rng(70 + ch)
    m = np.arange(1, 9)
    ratios = sorted({(a * a + 1.37 * b * b) for a in m for b in m})[:60]
    f = 140.0 * np.array(ratios) / ratios[0]
    f = f[f < 12000]
    t60 = 3.0 / (1 + f / 4000)
    a = r.uniform(0.3, 1.0, len(f)) / np.sqrt(1 + f / 1500)
    x = modes(n, f, t60, a, 71 + ch) * 0.25
    x[0] += 0.2
    return fade_end(x, 40)


def cathedral(ch):
    # Dense, dark, slowly swelling diffuse field (the start of a huge space).
    n = 8192
    tail = decaying_noise(n, 4.0, 4000, 900, 80 + ch)
    swell = np.clip(np.arange(n) / (0.09 * FS), 0, 1) ** 1.5
    x = onepole_lp(tail * swell, 3500)
    er = taps(n, np.sort(rng.uniform(35, 120, 10)),
              rng.choice([-1, 1], 10) * 0.15, 3000)
    x = x + er
    x[0] += 0.1
    return fade_end(x, 50)


IRS = [("room", room), ("hall", hall), ("plate", plate), ("spring", spring),
       ("cabinet", cabinet), ("body", body), ("metal", metal),
       ("cathedral", cathedral)]


def main():
    data = []
    parts_c = []
    irs_c = []
    ref = None
    for idx, (name, fn) in enumerate(IRS):
        ir = np.stack([fn(0), fn(1)])[:, :MAX_PARTS * B]
        # Unit energy (both channels together), then -6 dB, like CONVOLVE.
        ir = ir / np.sqrt(np.sum(ir ** 2)) * 0.5
        # Drop trailing partitions that are 90 dB below the whole IR.
        e_total = np.sum(ir ** 2)
        nparts = int(np.ceil(ir.shape[1] / B))
        while nparts > 1 and np.sum(ir[:, (nparts - 1) * B:] ** 2) < e_total * 1e-9:
            nparts -= 1
        peak_gain = float(np.max(np.abs(np.fft.rfft(ir, n=1 << 16, axis=1))))
        # Strongly resonant IRs (body, metal) would make tones jump out by
        # 15-20 dB: cap the peak gain at +9.5 dB.
        if peak_gain > 3.0:
            ir *= 3.0 / peak_gain
            peak_gain = 3.0
        if idx == 0:
            ref = ir[0, :B].copy()
        names = []
        for ch in range(2):
            pname = "kParts%d_%d" % (idx, ch)
            names.append(pname)
            entries = []
            for q in range(nparts):
                blk = np.zeros(N)
                seg = ir[ch, q * B:(q + 1) * B]
                blk[:len(seg)] = seg
                X = np.fft.rfft(blk)          # 257 bins, textbook sign
                p = np.abs(X) ** 2
                e = max(np.sum(p), 1e-30)
                above = np.cumsum(p[::-1])[::-1]  # energy from bin k upward
                bins = 256
                for k in range(16, 257):
                    if above[k] < e * 1e-6:
                        bins = k
                        break
                # Bandwidth budget: full band for the first 10 ms, 10.5 kHz up
                # to 43 ms, 5.3 kHz for the tail (real tails are dark; this
                # keeps the library around 120 KB of flash).
                cap = 256 if q < 2 else (112 if q < 8 else 56)
                bins = min(bins, cap)
                vre = X.real[:bins].copy()
                vim = -X.imag[:bins].copy()   # ShyFFT convention
                vim[0] = X.real[256] if bins == 256 else 0.0  # Nyquist slot
                pk = max(np.max(np.abs(vre)), np.max(np.abs(vim)), 1e-20)
                scale = pk / 32767.0
                offset = len(data)
                data.extend(np.round(vre / scale).astype(int).tolist())
                data.extend(np.round(vim / scale).astype(int).tolist())
                entries.append((offset, bins, scale))
            parts_c.append("static const FaltungPartition %s[%d] = {\n%s\n};\n" % (
                pname, nparts,
                "\n".join("  { %d, %d, 0, %.9ef }," % e for e in entries)))
        irs_c.append('  { "%s", %d, 0, %.6ff, { %s, %s } },' % (
            name, nparts, peak_gain, names[0], names[1]))
        print("%-10s %2d partitions (%3.0f ms), peak gain %.2f" % (
            name, nparts, nparts * B / FS * 1000, peak_gain))

    out = ["// Generated by tools/gen_irs.py - do not edit.\n",
           '#include "monsoon/dsp/modes/faltung_irs.h"\n',
           "namespace monsoon {\n",
           "const int16_t kFaltungData[%d] = {" % len(data)]
    for i in range(0, len(data), 16):
        out.append("  " + ", ".join(str(v) for v in data[i:i + 16]) + ",")
    out.append("};\n")
    out.extend(parts_c)
    out.append("const FaltungIr kFaltungIrs[kNumFaltungIrs] = {")
    out.extend(irs_c)
    out.append("};\n\n}  // namespace monsoon\n")
    with open(os.path.join(ROOT, "monsoon/dsp/modes/faltung_irs.cc"), "w") as f:
        f.write("\n".join(out))
    with open(os.path.join(ROOT, "host/faltung_ir_ref.h"), "w") as f:
        f.write("// Generated by tools/gen_irs.py: first 256 samples of IR 0, left.\n")
        f.write("static const float kFaltungRef0[256] = {\n")
        for i in range(0, B, 8):
            f.write("  " + ", ".join("%.9ef" % v for v in ref[i:i + 8]) + ",\n")
        f.write("};\n")
    print("spectrum data: %d int16 = %.1f KB" % (len(data), len(data) * 2 / 1024))


if __name__ == "__main__":
    main()

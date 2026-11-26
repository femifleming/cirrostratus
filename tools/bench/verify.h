// Every mode, every option, through the firmware's own audio path
// (Engine::Process: int16 in, dither, int16 out), on input that is the same
// bit for bit everywhere (integer oscillators + LCG noise). The same code
// runs on QEMU's STM32F405 (tools/bench/verify.sh: the ARM build, compiled
// with the firmware's flags) and on the Mac (host/verify_main.cc); the two
// reports are compared by tools/bench/verify_compare.py.
//
// Per mode/option, three runs:
//   typical  knobs mid, WET 0.7, FREEZE on for the last third, a TRIG, knob
//            noise like a real module's ADC
//   heavy    everything up, PITCH -12
//   dry      WET 0, FBK/RVB 0, mono bass off: the output must be the input
//            (within dither: 2 LSB)
//   sweep    16 corners of the panel (every slider at each end, TUNE
//            +-24, BAL/FBK/RVB at their ends): mean = the costliest corner
// One line each:
//   V <mode> <option> <run> init=<0|1> peak=<int16> rms=<x1000 of 1 LSB>
//     clips=<n> dev=<max |out - in| on the dry run> worst=<insns/frame of
//     the slowest block, ARM only> at=<its block> mean=<insns/frame>
//     hash=<crc of the last block>

#ifndef MONSOON_TOOLS_BENCH_VERIFY_H_
#define MONSOON_TOOLS_BENCH_VERIFY_H_

#include <stdint.h>
#include <string.h>

#include "monsoon/dsp/engine.h"

namespace verify {

using namespace monsoon;

struct Out {
  virtual void Print(const char* s) = 0;
  void Num(uint32_t v) {
    char buf[12];
    int i = 11;
    buf[i] = 0;
    do { buf[--i] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    Print(&buf[i]);
  }
};

// Input: three integer triangle/square voices + noise, stereo, ~-12 dBFS.
class Source {
 public:
  void Reset() { n_ = 0; rng_ = 12345; }
  void Fill(ShortFrame* f, size_t count) {
    for (size_t i = 0; i < count; ++i, ++n_) {
      rng_ = rng_ * 1664525u + 1013904223u;
      const int32_t noise = static_cast<int32_t>(rng_ >> 22) - 512;  // +-512
      const int32_t a = Tri(n_, 218);   // ~220 Hz
      const int32_t b = Tri(n_, 145);   // ~331 Hz
      const int32_t c = ((n_ / 97) & 1) ? 1500 : -1500;  // ~247 Hz square
      // A slow swell so envelopes and dynamics move.
      const int32_t env = static_cast<int32_t>((n_ >> 7) & 255);
      int32_t l = (a * 3 + c) * (64 + env) / 320 + noise;
      int32_t r = (b * 3 - c) * (64 + (255 - env)) / 320 + noise;
      f[i].l = static_cast<int16_t>(l);
      f[i].r = static_cast<int16_t>(r);
    }
  }
 private:
  static int32_t Tri(uint32_t n, uint32_t period) {
    const int32_t p = static_cast<int32_t>(n % period);
    const int32_t h = static_cast<int32_t>(period / 2);
    const int32_t v = p < h ? p : static_cast<int32_t>(period) - p;
    return (v * 2 - h) * 4000 / h;  // +-4000
  }
  uint32_t n_ = 0;
  uint32_t rng_ = 12345;
};

static uint32_t Crc(const uint8_t* p, size_t n, uint32_t c = 0xffffffffu) {
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return c;
}

enum Run { RUN_TYPICAL, RUN_HEAVY, RUN_DRY, RUN_SWEEP, RUN_LAST };

// RUN_SWEEP: 16 corners of the panel, each held for 1/16 of the run: the
// four sliders at 0 or 1 (all 16 combinations), TUNE -24/+24, BAL, FBK and
// RVB alternating ends. Reports the costliest corner's mean.
static void Corner(int s, Controls* c) {
  c->position = (s & 1) ? 1.0f : 0.0f;
  c->size = (s & 2) ? 1.0f : 0.0f;
  c->density = (s & 4) ? 1.0f : 0.0f;
  c->texture = (s & 8) ? 1.0f : 0.0f;
  c->pitch = (s & 1) ? 24.0f : -24.0f;
  c->bal = (s & 2) ? 1.0f : 0.0f;
  c->fbk = (s & 4) ? 0.95f : 0.0f;
  c->rvb = (s & 8) ? 1.0f : 0.0f;
  c->blend = 0.8f;
}

// `ticks`: a cycle-ish clock (ARM: SysTick under -icount, 168 per 1000
// instructions); NULL on the host.
typedef uint64_t (*TicksFn)();

inline void RunAll(Engine& engine, void* main_mem, size_t main_size,
                   void* ccm_mem, size_t ccm_size, Out& out, TicksFn ticks,
                   int blocks) {
  static const char* kRun[RUN_LAST] = { "typical", "heavy", "dry", "sweep" };
  ShortFrame in[kBlockSize], o[kBlockSize];
  Source src;
  for (int m = MODE_FIRST; m < MODE_LAST; ++m) {
#ifdef VERIFY_ONLY
    if (m != VERIFY_ONLY) continue;  // BENCH_DEFS="-DVERIFY_ONLY=MODE_CHLOE"
#endif
    const int options = engine.num_options(static_cast<ModeId>(m));
    for (int opt = 0; opt < options; ++opt) {
      for (int run = 0; run < RUN_LAST; ++run) {
        // Every run from a freshly started engine, like a power-up.
        engine.Init(main_mem, main_size, ccm_mem, ccm_size);
        engine.set_mono_bass(run != RUN_DRY, true);
        Controls c;
        memset(&c, 0, sizeof(c));
        c.blend = 0.7f;
        engine.RequestMode(static_cast<ModeId>(m), static_cast<uint8_t>(opt));
        memset(in, 0, sizeof(in));
        for (int k = 0; k < 600 && engine.switching(); ++k) {
          engine.Process(c, in, o, kBlockSize);
          for (int p = 0; p < 4; ++p) engine.Prepare();
        }
        const bool init_ok = !engine.init_failed() &&
            engine.mode() == static_cast<ModeId>(m);
#ifndef VERIFY_SIZE
#define VERIFY_SIZE 0.6f
#endif
        c.position = 0.4f; c.size = VERIFY_SIZE; c.density = 0.6f;
        c.texture = 0.6f; c.pitch = 7.0f; c.blend = 0.7f; c.bal = 0.6f;
        c.fbk = 0.4f; c.rvb = 0.5f;
        if (run == RUN_HEAVY) {
          c.position = 0.8f; c.density = 1.0f; c.size = 0.95f;
          c.texture = 0.9f; c.fbk = 0.9f; c.rvb = 0.9f; c.pitch = -12.0f;
          c.bal = 0.95f;
        } else if (run == RUN_DRY) {
          c.blend = 0.0f; c.fbk = 0.0f; c.rvb = 0.0f;
        }
        c.option = static_cast<uint8_t>(opt);
        src.Reset();
        uint64_t sumsq = 0;
        uint32_t peak = 0, clips = 0, dev = 0, worst = 0, worst_at = 0, hash = 0;
        uint64_t total = 0, seg_total = 0, seg_worst = 0;
        int seg_blocks = 0;
        for (int b = 0; b < blocks; ++b) {
          src.Fill(in, kBlockSize);
          Controls k = c;
          const int seg_len = blocks / 16 > 0 ? blocks / 16 : 1;
          if (run == RUN_SWEEP) Corner((b / seg_len) & 15, &k);
          // Knob noise as the module's ADC gives it.
          const float j = (b & 1) ? 0.001f : -0.001f;
          k.position += j; k.size += j; k.density += j; k.texture += j;
          k.bal += j;
          if (run != RUN_DRY) { k.fbk += j; k.rvb += j; k.blend += j; }
          k.pitch += (b & 1) ? 0.03f : -0.03f;
          k.freeze = run != RUN_DRY && run != RUN_SWEEP && b > blocks * 2 / 3;
          // (No TRIG in the sweep: in TAPE it stops the tape, which would
          // leave the later corners unmeasured.)
          k.trigger = run != RUN_SWEEP && b == blocks / 3;
          k.gate = run != RUN_SWEEP && b >= blocks / 3 && b < blocks / 2;
          const uint64_t t0 = ticks ? ticks() : 0;
          engine.Process(k, in, o, kBlockSize);
          const uint64_t t1 = ticks ? ticks() : 0;
          for (int p = 0; p < 4; ++p) engine.Prepare();
          if (ticks) {
            const uint32_t per_frame = static_cast<uint32_t>(
                (t1 - t0) * 1000ull / 168ull / kBlockSize);
            // The first blocks after a mode starts carry one-off work, under
            // the engine's fade-in (15 blocks): timed from block 32 on.
            if (b >= 32 && per_frame > worst) { worst = per_frame; worst_at = b; }
            total += t1 - t0;
            if (run == RUN_SWEEP) {
              // Each corner's mean, once its knobs have settled (skip the
              // first quarter of the corner).
              if ((b % seg_len) >= seg_len / 4) { seg_total += t1 - t0; ++seg_blocks; }
              if ((b % seg_len) == seg_len - 1 && seg_blocks) {
                if (seg_total / seg_blocks > seg_worst) seg_worst = seg_total / seg_blocks;
#ifdef VERIFY_SEGMENTS
                out.Print("  corner "); out.Num(static_cast<uint32_t>(b / seg_len));
                out.Print(" mean "); out.Num(static_cast<uint32_t>(
                    seg_total / seg_blocks * 1000ull / 168ull / kBlockSize));
                out.Print("\n");
#endif
                seg_total = 0; seg_blocks = 0;
              }
            }
          }
          for (size_t i = 0; i < kBlockSize; ++i) {
            const int32_t l = o[i].l, r = o[i].r;
            const uint32_t al = static_cast<uint32_t>(l < 0 ? -l : l);
            const uint32_t ar = static_cast<uint32_t>(r < 0 ? -r : r);
            if (al > peak) peak = al;
            if (ar > peak) peak = ar;
            if (al >= 32767) ++clips;
            if (ar >= 32767) ++clips;
            sumsq += static_cast<uint64_t>(l * l) + static_cast<uint64_t>(r * r);
            if (run == RUN_DRY && b >= 16) {
              const int32_t dl = l - in[i].l, dr = r - in[i].r;
              const uint32_t d = static_cast<uint32_t>(
                  (dl < 0 ? -dl : dl) > (dr < 0 ? -dr : dr)
                  ? (dl < 0 ? -dl : dl) : (dr < 0 ? -dr : dr));
              if (d > dev) dev = d;
            }
          }
          if (b == blocks - 1) {
            hash = Crc(reinterpret_cast<const uint8_t*>(o), sizeof(o));
          }
        }
        // rms in thousandths of an LSB, via an integer square root.
        const uint64_t frames = static_cast<uint64_t>(blocks) * kBlockSize * 2;
        uint64_t ms = sumsq * 1000ull / frames * 1000ull;
        uint64_t x = 0, bit = 1ull << 62;
        while (bit > ms) bit >>= 2;
        while (bit) {
          if (ms >= x + bit) { ms -= x + bit; x = (x >> 1) + bit; }
          else x >>= 1;
          bit >>= 2;
        }
        out.Print("V ");
        out.Print(Engine::mode_name(static_cast<ModeId>(m)));
        out.Print(" "); out.Num(static_cast<uint32_t>(opt));
        out.Print(" "); out.Print(kRun[run]);
        out.Print(" init="); out.Num(init_ok ? 1 : 0);
        out.Print(" peak="); out.Num(peak);
        out.Print(" rms="); out.Num(static_cast<uint32_t>(x));
        out.Print(" clips="); out.Num(clips);
        out.Print(" dev="); out.Num(dev);
        out.Print(" worst="); out.Num(worst);
        out.Print(" at="); out.Num(worst_at);
        out.Print(" mean="); out.Num(static_cast<uint32_t>(run == RUN_SWEEP
            ? seg_worst * 1000ull / 168ull / kBlockSize
            : total * 1000ull / 168ull / (static_cast<uint64_t>(blocks) * kBlockSize)));
        out.Print(" hash="); out.Num(hash);
        out.Print("\n");
      }
    }
  }
  out.Print("verify done\n");
}

}  // namespace verify

#endif  // MONSOON_TOOLS_BENCH_VERIFY_H_

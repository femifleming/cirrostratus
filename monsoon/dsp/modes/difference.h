// Mode 5: DIFFERENCE - difference tones (after the Oneirophone).
//
// The input is turned into an analytic signal (Hilbert transform) and
// frequency-shifted (single sideband) into high copies: one at f_a and one
// at f_a + delta_k per pair. Every partial p of the input lands at p + f_a
// and p + f_a + delta_k, so all of them differ by exactly delta_k and the
// ear hears delta_k - a melody that is not in the signal.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Left: glide, the heard note slides to each new PITCH
//        (20 ms -> 2 s). Right: dichotic, the f_a copies move to the left
//        ear and the f_a + delta copies to the right: on headphones the
//        difference tone gives way to binaural beating / binaural pitch.
//   FBK  spiral: the shifted signal recirculates through a 50 ms delay and
//        climbs by delta on every pass (rising echoes; on held sounds a
//        ladder f_a, f_a + delta, f_a + 2 delta ... that makes the phantom
//        note stronger). Level-compensated, < 0.85 per pass.
//   RVB  the engine's FDN reverb (the pairs are dry sines with no space of
//        their own; both tones reverberate, so the phantom note survives).

#ifndef MONSOON_DSP_MODES_DIFFERENCE_H_
#define MONSOON_DSP_MODES_DIFFERENCE_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

// 90-degree phase splitter: two chains of four 2nd-order allpass sections
// (Olli Niemitalo's coefficients), quadrature within ~0.7 deg from
// ~20 Hz to ~20 kHz at 44.1-48 kHz.
class HilbertPair {
 public:
  void Init();
  // Returns the in-phase (re) and quadrature (im) parts.
  inline void Process(float x, float* re, float* im) {
    float a = x;
    for (int i = 0; i < 4; ++i) a = Section(&sa_[i], ka_[i], a);
    float b = x;
    for (int i = 0; i < 4; ++i) b = Section(&sb_[i], kb_[i], b);
    *re = a_delay_;
    *im = b;
    a_delay_ = a;
  }

 private:
  struct State { float x1, x2, y1, y2; };
  static inline float Section(State* s, float k, float x) {
    float y = k * (x + s->y2) - s->x2;
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
  }
  static const float ka_[4];
  static const float kb_[4];
  State sa_[4], sb_[4];
  float a_delay_;
};

class DifferenceMode : public Mode {
 public:
  static const int kMaxPairs = 6;

  const char* name() const { return "difference"; }
  bool Init(Arena* arena) __attribute__((cold));  // runs once: small
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  float reverb_amount(const Controls& c) const { return c.rvb; }

  static const size_t kSpiralSize = 4096;    // per line, power of two
  static const size_t kSpiralDelay = 2400;   // 50 ms

 private:
  // Per-mode knobs (kept out of line: flash is tight).
  float Knobs(const Controls& c, float bal, size_t n) __attribute__((noinline));
  void Spiral(uint32_t wp, uint32_t rp, float rot_c, float rot_s, float comp,
              float* re, float* im) __attribute__((noinline));

  HilbertPair hilbert_[2];
  Svf band_[2][2];          // input low-pass (two cascaded = 4-pole)
  Svf square_lp_[2];        // keeps the quadratic term to the low band
  DcBlocker dc_[2];
  Envelope raw_env_, band_env_;
  float phase_[kMaxPairs + 1];  // 0 = f_a, 1.. = f_a + delta_k
  float pair_gain_[kMaxPairs];
  float freeze_mix_;
  float held_level_;
  float agc_;
  bool frozen_;
  float glide_;             // BAL left: current delta (Hz)
  float* spiral_[2];        // FBK: complex delay lines, (re, im) pairs
  uint32_t spiral_pos_;
  float spiral_lp_[2][2];
  float spiral_gain_;       // smoothed FBK
  bool spiral_clean_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_DIFFERENCE_H_

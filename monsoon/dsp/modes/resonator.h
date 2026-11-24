// Mode 4 - RESONATOR: eight sympathetic strings with a jawari bridge.
//
// Each string is a Karplus-Strong loop: a float delay line read with
// Hermite interpolation, a one-pole loop filter (its group delay is
// subtracted from the delay so tuning stays exact), a per-period loss and
// a bridge whose contact point moves with the string's displacement (the
// jawari buzz of a sitar or tanpura, modelled as amplitude-dependent delay
// modulation). The input excites every string; TRIG plucks them all.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Left: detune, the strings spread up to +/-25 cents
//        from their tuning (beating, a chorus of strings). Right: stretch,
//        intervals widen up to +10 % of their size in cents (an octave
//        becomes ~+120 cents wide: piano-like, then bell-like).
//   FBK  sympathetic coupling: energy passes from string to string
//        (an orthogonal, energy-preserving mix of their outputs), so
//        strings tuned close together trade it back and forth (slow
//        beating, blooming) and a note played into one wakes the others.
//   RVB  body: the strings sound through a resonant wooden box (two
//        diffusing allpasses into a damped ~8.5 ms resonance, tail up to
//        ~0.35 s), dry -> half dry, half body.

#ifndef MONSOON_DSP_MODES_RESONATOR_H_
#define MONSOON_DSP_MODES_RESONATOR_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class ResonatorMode : public Mode {
 public:
  static const int kNumStrings = 8;
  static const size_t kLineSize = 2048;   // power of two, >= 48000 / 25 Hz
  static const size_t kLineMask = kLineSize - 1;

  const char* name() const { return "resonator"; }
  bool Init(Arena* arena) __attribute__((cold));  // runs once: small
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  // Test hooks.
  float string_frequency(int i) const { return frequency_[i]; }

 private:
  struct String {
    float delay;        // current loop delay in samples (smoothed)
    float lp;           // loop filter state
    float last;         // last output (drives the jawari)
    float env;          // slow amplitude envelope (scales the bridge)
    float rattle_lp;    // state of the rattle high-pass
    int32_t burst;      // remaining samples of a pluck burst
  };

  void ComputeTargets(const Controls& c);
  void Body(float rvb, FloatFrame* out, size_t n) __attribute__((noinline));

  float* line_[kNumStrings];
  String string_[kNumStrings];
  float frequency_[kNumStrings];
  float target_delay_[kNumStrings];
  float gain_[kNumStrings];
  size_t write_;
  float lp_coef_;
  float pickup_;        // pickup position as a fraction of the string
  float jawari_;
  float input_gain_;
  bool frozen_;
  bool first_block_;
  float cache_[6];
  Random rng_;
  DcBlocker dc_[2];

  // RVB body, per channel: two allpasses into a damped feedback delay.
  static const size_t kBodySize = 4096;   // floats per channel, power of 2
  float* body_[2];
  uint32_t body_pos_;
  float body_lp_[2];
  float body_mix_;      // smoothed RVB
  bool body_clean_;     // buffers are zero (RVB was fully off)
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_RESONATOR_H_

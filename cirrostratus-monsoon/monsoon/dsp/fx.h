// Output-section effects (the feedback loop and the FDN are services a
// mode may map a knob to; MonoBass and Limiter run on the main out):
//  - ResonanceTamer: Quell-style adaptive notches for the feedback loop.
//  - MonoBass: LR4 crossover, bass below 60 Hz summed to mono.
//  - Fdn: 8-line modulated feedback delay network reverb (int16 lines).
//  - Limiter: stereo peak limiter + soft clip.

#ifndef MONSOON_DSP_FX_H_
#define MONSOON_DSP_FX_H_

#include "monsoon/dsp/core.h"

namespace monsoon {

// ---------------------------------------------------------------------------
// One adaptive notch (constrained IIR notch with normalised LMS tracking).
// It locks onto the strongest narrow peak in the signal.
class AdaptiveNotch {
 public:
  void Init(float initial_hz, float rho) {
    c_ = 2.0f * cosf(kTwoPi * initial_hz / kSampleRate);
    rho_ = rho;
    s1_ = s2_ = 0.0f;
    power_ = 1e-4f;
  }
  inline float Process(float x, float mu) {
    // All-pole part with poles at rho * e^{+/- j w}.
    float s = x + rho_ * c_ * s1_ - rho_ * rho_ * s2_;
    // FIR zeros on the unit circle.
    float e = s - c_ * s1_ + s2_;
    power_ += 0.001f * (s1_ * s1_ - power_);
    c_ += mu * e * s1_ / (power_ + 1e-6f);
    if (c_ > 1.999f) c_ = 1.999f;
    if (c_ < -1.999f) c_ = -1.999f;
    s2_ = s1_;
    s1_ = s;
    // Unity gain away from the notch: the pole/zero pair gives
    // (1 + rho^2)/2 ... compensate roughly.
    return e * gain_comp();
  }
  float frequency_hz() const {
    float c = Clamp(c_ * 0.5f, -1.0f, 1.0f);
    return acosf(c) * kSampleRate / kTwoPi;
  }
 private:
  inline float gain_comp() const { return 0.5f * (1.0f + rho_ * rho_); }
  float c_, rho_, s1_, s2_, power_;
};

// Two cascaded notches whose depth follows how "peaky" the loop gets:
// broadband material passes untouched, a ringing resonance gets cut.
class ResonanceTamer {
 public:
  void Init() {
    notch_[0].Init(800.0f, 0.985f);
    notch_[1].Init(2500.0f, 0.985f);
    in_power_ = out_power_ = 1e-6f;
    depth_ = 0.0f;
  }
  // amount 0..1 (how hard to tame), x is the mono feedback signal.
  // Returns the correction to subtract from each channel.
  inline float Process(float x, float amount) {
    float y1 = notch_[0].Process(x, 0.0004f);
    float y2 = notch_[1].Process(y1, 0.0004f);
    in_power_ += 0.0005f * (x * x - in_power_);
    out_power_ += 0.0005f * (y2 * y2 - out_power_);
    // If the notches remove a big share of the energy, the signal is
    // dominated by resonances: increase the cut.
    float removed = 1.0f - out_power_ / (in_power_ + 1e-9f);
    float target = Clamp((removed - 0.15f) * 2.0f, 0.0f, 1.0f) * amount;
    depth_ += 0.001f * (target - depth_);
    return (x - y2) * depth_;
  }
  float depth() const { return depth_; }
 private:
  AdaptiveNotch notch_[2];
  float in_power_, out_power_, depth_;
};

// ---------------------------------------------------------------------------
// Mono bass for the main out: a Linkwitz-Riley 4th-order crossover. Below
// the crossover L and R are summed, (L+R)/2 to both sides; above it they
// pass untouched. Same result as HP4(L) + LP4((L+R)/2), but done in
// mid/side with three 2nd-order sections instead of six:
//   mid  -> LR4 allpass (= LP4 + HP4, the crossover's flat-magnitude sum)
//   side -> LR4 high-pass (two Butterworth high-passes)
// Toggling crossfades over ~20 ms; fully off it costs nothing.
class MonoBass {
 public:
  void Init(float hz) {
    const float g = tanf(kPi * hz / kSampleRate);
    const float k = 1.41421356f;  // Butterworth, Q = 1/sqrt(2)
    k_ = k;
    a1_ = 1.0f / (1.0f + g * (g + k));
    a2_ = g * a1_;
    a3_ = g * a2_;
    Reset();
    mix_ = 1.0f;
    on_ = true;
  }
  void Reset() { for (int i = 0; i < 6; ++i) s_[i] = 0.0f; }
  // `immediate` skips the crossfade (start-up, tests).
  void set_on(bool on, bool immediate = false) {
    on_ = on;
    if (immediate) {
      mix_ = on ? 1.0f : 0.0f;
      Reset();
    }
  }
  bool on() const { return on_; }

  void Process(FloatFrame* io, size_t n) {
    if (!on_ && mix_ <= 0.0f) return;
    if (on_ && mix_ <= 0.0f) Reset();  // coming back from bypass
    const float inc = on_ ? 1.0f / 960.0f : -1.0f / 960.0f;
    float ic1 = s_[0], ic2 = s_[1], ic3 = s_[2], ic4 = s_[3];
    float ic5 = s_[4], ic6 = s_[5];
    const float k = k_, a1 = a1_, a2 = a2_, a3 = a3_;
    float mix = mix_;
    for (size_t i = 0; i < n; ++i) {
      const float l = io[i].l, r = io[i].r;
      const float m = 0.5f * (l + r);
      const float s = 0.5f * (l - r);
      // Mid: allpass x - 2k*bp.
      float v1 = a1 * ic1 + a2 * (m - ic2);
      float v2 = ic2 + a2 * ic1 + a3 * (m - ic2);
      ic1 = 2.0f * v1 - ic1;
      ic2 = 2.0f * v2 - ic2;
      const float ap = m - 2.0f * k * v1;
      // Side: two high-passes x - k*bp - lp.
      v1 = a1 * ic3 + a2 * (s - ic4);
      v2 = ic4 + a2 * ic3 + a3 * (s - ic4);
      ic3 = 2.0f * v1 - ic3;
      ic4 = 2.0f * v2 - ic4;
      const float h1 = s - k * v1 - v2;
      v1 = a1 * ic5 + a2 * (h1 - ic6);
      v2 = ic6 + a2 * ic5 + a3 * (h1 - ic6);
      ic5 = 2.0f * v1 - ic5;
      ic6 = 2.0f * v2 - ic6;
      const float h2 = h1 - k * v1 - v2;
      if (mix >= 1.0f && inc > 0.0f) {
        io[i].l = ap + h2;
        io[i].r = ap - h2;
      } else {
        mix = Clamp(mix + inc, 0.0f, 1.0f);
        io[i].l = l + (ap + h2 - l) * mix;
        io[i].r = r + (ap - h2 - r) * mix;
      }
    }
    s_[0] = ic1; s_[1] = ic2; s_[2] = ic3; s_[3] = ic4;
    s_[4] = ic5; s_[5] = ic6;
    mix_ = mix;
  }

 private:
  float k_, a1_, a2_, a3_;
  float s_[6];
  float mix_;
  bool on_;
};

// ---------------------------------------------------------------------------
// Feedback delay network reverb with 16-bit delay lines.
class Fdn {
 public:
  static const int kLines = 8;
  static const size_t kBufferSize = 16384;  // int16, power of two
  static size_t MemoryBytes() { return kBufferSize * sizeof(int16_t); }

  bool Init(Arena* arena);
  // amount 0..1 sets both send level and decay time; freeze = infinite.
  // Reads `in`, adds the reverb tail to `out` (they may be the same).
  void Process(float amount, bool freeze, const FloatFrame* in,
               FloatFrame* out, size_t n);
  void Clear();
  // True once the send has faded out; Process then returns at once.
  bool idle() const { return amount_smoothed_ < 0.002f; }

 private:
  // All eight lines share one ring buffer and one write pointer (the
  // Clouds FxEngine layout): line i lives at offset base_[i].
  int16_t* buffer_;
  uint32_t write_ptr_;
  uint32_t base_[kLines];
  float lp_[kLines];
  float lfo_phase_[2];
  float amount_smoothed_;
  Allpass<160> in_ap_[4];
  DcBlocker dc_[2];
};

// ---------------------------------------------------------------------------
class Limiter {
 public:
  void Init() { gain_ = 1.0f; }
  inline void Process(float* l, float* r) {
    float peak = fabsf(*l) > fabsf(*r) ? fabsf(*l) : fabsf(*r);
    const float threshold = 0.89f;  // -1 dBFS
    float target = peak > threshold ? threshold / peak : 1.0f;
    // Instant attack, ~120 ms release.
    if (target < gain_) gain_ = target;
    else gain_ += 0.00017f * (target - gain_);
    *l = Knee(*l * gain_);
    *r = Knee(*r * gain_);
  }
  float gain() const { return gain_; }
 private:
  // Transparent below 0.8, smooth saturation to 1.0 above.
  static inline float Knee(float x) {
    float a = fabsf(x);
    if (a <= 0.8f) return x;
    float y = 0.8f + 0.2f * SoftClip((a - 0.8f) * 5.0f);
    return x < 0.0f ? -y : y;
  }
  float gain_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_FX_H_

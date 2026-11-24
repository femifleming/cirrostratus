// Output-section effects shared by every mode:
//  - ResonanceTamer: Quell-style adaptive notches for the feedback loop.
//  - Spread: mid/side width + mono-compatible decorrelation + tiny Haas.
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
class Spread {
 public:
  void Init() {
    ap0_.Init(37, 0.6f);
    ap1_.Init(113, -0.55f);
    ap2_.Init(241, 0.5f);
    haas_pos_ = 0;
    memset(haas_, 0, sizeof(haas_));
  }
  // spread 0 = mono, 0.5 = untouched, 1 = extra wide.
  inline void Process(float spread, float* l, float* r) {
    float m = 0.5f * (*l + *r);
    float s = 0.5f * (*l - *r);
    float width = spread * 2.0f;
    float extra = spread > 0.5f ? (spread - 0.5f) * 2.0f : 0.0f;
    if (width > 1.0f) width = 1.0f + (width - 1.0f) * 0.6f;
    s *= width;
    // Mono-compatible decorrelation: added to the side only, so L+R keeps
    // exactly the original mid.
    float d = ap2_.Process(ap1_.Process(ap0_.Process(m)));
    s += d * extra * 0.45f;
    float outl = m + s;
    float outr = m - s;
    // Haas: delay R by up to ~0.35 ms at the very top of the range.
    haas_[haas_pos_] = outr;
    size_t delay = static_cast<size_t>(extra * extra * 16.0f);
    size_t rd = (haas_pos_ + kHaas - delay) % kHaas;
    *l = outl;
    *r = haas_[rd];
    haas_pos_ = (haas_pos_ + 1) % kHaas;
  }
 private:
  static const size_t kHaas = 32;
  Allpass<64> ap0_;
  Allpass<128> ap1_;
  Allpass<256> ap2_;
  float haas_[kHaas];
  size_t haas_pos_;
};

// ---------------------------------------------------------------------------
// Feedback delay network reverb with 16-bit delay lines.
class Fdn {
 public:
  static const int kLines = 8;
  static size_t MemoryBytes();

  bool Init(Arena* arena);
  // amount 0..1 sets both send level and decay time; freeze = infinite.
  void Process(float amount, bool freeze, FloatFrame* io, size_t n);
  void Clear();

 private:
  inline float ReadLine(int i, float delay) const;
  int16_t* line_[kLines];
  size_t length_[kLines];
  size_t write_[kLines];
  float lp_[kLines];
  float gain_[kLines];
  float damp_coef_;
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

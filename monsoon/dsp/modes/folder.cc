// Mode 25 - FOLDER (see folder.h).

#include "monsoon/dsp/modes/folder.h"

namespace monsoon {

static const int kCenter = 23;   // 47-tap half-band

bool FolderMode::Init(Arena* arena) {
  for (int ch = 0; ch < 2; ++ch) {
    up_[ch] = arena->Allocate<float>(kRing);
    down_[ch] = arena->Allocate<float>(kRing);
    if (!up_[ch] || !down_[ch]) return false;
    lpg_[ch].Init();
    dc_[ch].Init();
  }
  up_w_ = down_w_ = 0;
  memset(&k_, 0, sizeof(k_));
  // Blackman-windowed half-band (odd taps only), unity DC gain.
  float sum = 0.0f;
  for (int k = 0; k < 12; ++k) {
    int off = 2 * k + 1;
    float x = 0.5f * kPi * off;
    float n = static_cast<float>(kCenter + off);
    float w = 0.42f - 0.5f * cosf(kTwoPi * n / 46.0f) +
        0.08f * cosf(2.0f * kTwoPi * n / 46.0f);
    halfband_[k] = 0.5f * (sinf(x) / x) * w;
    sum += 2.0f * halfband_[k];
  }
  for (int k = 0; k < 12; ++k) halfband_[k] *= 0.5f / sum;
  phase_ = 0.0f;
  gain_ = 1.0f;
  bias_ = 0.0f;
  vactrol_ = 0.0f;
  lpg_gain_ = 0.0f;
  frozen_ = false;
  env_.Init(0.002f, 0.03f);
  return true;
}

inline float FolderMode::Fold(float x) const {
  // sin(pi/2 (g x + bias)); FastSin takes cycles.
  return FastSin(0.25f * (gain_ * x + bias_));
}

void FolderMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  const float dt = static_cast<float>(n) / kSampleRate;

  const float pos = Clamp(c.position, 0.0f, 1.0f);
  gain_ = 1.0f + 9.0f * pos * pos;
  bias_ = (Clamp(c.size, 0.0f, 1.0f) - 0.5f) * 1.2f;
  const float sine_level = (c.option & 1) ? 0.0f : 0.7f;
  const float inc = Clamp(130.81f * SemitonesToRatio(c.pitch), 8.0f, 8000.0f) /
      kSampleRate;

  // Envelope of the input (block peak of the follower).
  float env = 0.0f;
  for (size_t s = 0; s < n; ++s) {
    float e = env_.Process(0.5f * (in[s].l + in[s].r));
    if (e > env) env = e;
  }

  // Vactrol: fast attack, slow and non-linear release.
  if (c.trigger) vactrol_ = 1.0f;
  float target = frozen_ ? 1.0f : Clamp(env * 4.0f, 0.0f, 1.0f);
  if (target > vactrol_) {
    vactrol_ += (target - vactrol_) * (1.0f - expf(-dt / 0.004f));
  } else {
    float decay = 0.03f * powf(100.0f, Clamp(c.texture, 0.0f, 1.0f));
    float tau = decay * (0.5f + (1.0f - vactrol_));
    vactrol_ += (target - vactrol_) * (1.0f - expf(-dt / tau));
  }

  // LPG mode.
  const float d = Clamp(c.density, 0.0f, 1.0f);
  const float lp_amt = Clamp(2.0f - 2.0f * d, 0.0f, 1.0f);
  const float vca_amt = Clamp(2.0f * d, 0.0f, 1.0f);
  const float cut = vactrol_ * lp_amt + (1.0f - lp_amt);
  const float fc = 20.0f * powf(1000.0f, cut);
  SetLpg(fc / kSampleRate);
  lpg_gain_ = Clamp(lpg_gain_, 0.0f, 1.0f);
  const float new_gain = (1.0f - vca_amt) + vca_amt * vactrol_;
  // Ramp the VCA gain across the block (no zipper noise).
  const float gain_inc = (new_gain - lpg_gain_) / static_cast<float>(n);

  // BAL (cross-modulation) and FBK (folder feedback) act before the
  // folder.
  const bool pre = Knobs(c, n);

  for (size_t s = 0; s < n; ++s) {
    phase_ += inc;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    const float carrier = FastSin(phase_);
    const float sine = carrier * sine_level;
    float x[2] = { in[s].l + sine, in[s].r + sine };
    if (pre) PreFold(x, in[s], carrier, sine_level);
    float y[2];
    for (int ch = 0; ch < 2; ++ch) {
      float* up = up_[ch];
      up[up_w_] = x[ch];
      // Interpolate: the in-between sample, then the delayed original.
      float even = 0.0f;
      for (int q = 0; q < 12; ++q) {
        even += halfband_[q] *
            (up[(up_w_ + kRing - 12 - q) & (kRing - 1)] +
             up[(up_w_ + kRing - 11 + q) & (kRing - 1)]);
      }
      even *= 2.0f;
      const float odd = up[(up_w_ + kRing - 11) & (kRing - 1)];

      float* dn = down_[ch];
      dn[down_w_] = Fold(even);
      dn[(down_w_ + 1) & (kRing - 1)] = Fold(odd);
      const size_t newest = (down_w_ + 1) & (kRing - 1);
      const size_t center = (newest + kRing - kCenter) & (kRing - 1);
      float acc = 0.5f * dn[center];
      for (int q = 0; q < 12; ++q) {
        size_t off = 2 * q + 1;
        acc += halfband_[q] * (dn[(center + off) & (kRing - 1)] +
                               dn[(center + kRing - off) & (kRing - 1)]);
      }
      y[ch] = acc;
      k_.fold_y[ch] = acc;
    }
    up_w_ = (up_w_ + 1) & (kRing - 1);
    down_w_ = (down_w_ + 2) & (kRing - 1);

    lpg_gain_ += gain_inc;
    out[s].l = dc_[0].Process(lpg_[0].Lp(y[0]) * lpg_gain_ * 0.8f);
    out[s].r = dc_[1].Process(lpg_[1].Lp(y[1]) * lpg_gain_ * 0.8f);
  }
}

// Out of line and compiled for size (the flash is nearly full).
void FolderMode::SetLpg(float f) {
  for (int ch = 0; ch < 2; ++ch) lpg_[ch].set(f, 0.6f);
}

// Block-rate part of BAL and FBK: the ramps (snapped to their targets at
// every block, so noon / zero is exact). Returns whether PreFold runs.
bool FolderMode::Knobs(const Controls& c, size_t n) {
  const float inv_n = 1.0f / static_cast<float>(n);
  const float bal = KnobBal(c.bal);
  const float fb = 1.5f * (c.fbk > 1.0f ? 1.0f : c.fbk);
  k_.value[0] = k_.target[0];
  k_.value[1] = k_.target[1];
  k_.inc[0] = (bal - k_.value[0]) * inv_n;
  k_.inc[1] = (fb - k_.value[1]) * inv_n;
  k_.target[0] = bal;
  k_.target[1] = fb;
  return bal != 0.0f || fb > 0.0f || k_.value[0] != 0.0f ||
      k_.value[1] > 0.0f;
}

// The folder's input with BAL and FBK, per sample (replaces in + sine).
// BAL left: the input bends the sine's phase (up to +/-0.75 cycles at
// full scale). Right: the sine (at PITCH, also in option 1) rings the
// input (all ring at full). FBK: the last folder output is added back
// (up to 1.5x).
void FolderMode::PreFold(float* x, const FloatFrame& in, float carrier,
                         float level) {
  const float bal = (k_.value[0] += k_.inc[0]);
  const float fb = (k_.value[1] += k_.inc[1]);
  float l = in.l, r = in.r;
  float sine = carrier;
  if (bal < 0.0f) {
    // The phase-modulated sine, as a parabola (its small harmonics vanish
    // in the folding): with q = 2 frac(phase) - 1 in [-1, 1),
    // sin(2 pi phase) = -sin(pi q) ~ -4 q (1 - |q|).
    float q = phase_ - 0.375f * bal * (l + r);
    q -= static_cast<float>(static_cast<int32_t>(q));   // (-1, 1)
    q = q < 0.0f ? 2.0f * q + 1.0f : 2.0f * q - 1.0f;
    sine = -4.0f * q * (1.0f - fabsf(q));
  } else {
    const float rm = 1.0f + bal * (1.41f * carrier - 1.0f);
    l *= rm;
    r *= rm;
  }
  sine *= level;
  x[0] = l + sine + fb * k_.fold_y[0];
  x[1] = r + sine + fb * k_.fold_y[1];
}

}  // namespace monsoon

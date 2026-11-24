// Mode 5: DIFFERENCE (see difference.h).

#include "monsoon/dsp/modes/difference.h"

namespace monsoon {

const float HilbertPair::ka_[4] = {
  0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
  0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f
};
const float HilbertPair::kb_[4] = {
  0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
  0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f
};

void HilbertPair::Init() {
  memset(sa_, 0, sizeof(sa_));
  memset(sb_, 0, sizeof(sb_));
  a_delay_ = 0.0f;
}

static const float kPairRatios[DifferenceMode::kMaxPairs] = {
  1.0f, 1.5f, 1.25f, 2.0f, 1.75f, 1.125f
};

bool DifferenceMode::Init(Arena* arena) {
  (void)arena;
  for (int ch = 0; ch < 2; ++ch) {
    hilbert_[ch].Init();
    band_[ch][0].Init();
    band_[ch][1].Init();
    square_lp_[ch].Init();
    square_lp_[ch].set(3000.0f / kSampleRate, 0.707f);
    dc_[ch].Init(0.999f);
  }
  raw_env_.Init(0.01f, 0.3f);
  band_env_.Init(0.01f, 0.3f);
  memset(phase_, 0, sizeof(phase_));
  memset(pair_gain_, 0, sizeof(pair_gain_));
  pair_gain_[0] = 1.0f;
  freeze_mix_ = 0.0f;
  held_level_ = 0.0f;
  agc_ = 1.0f;
  frozen_ = false;
  return true;
}

void DifferenceMode::Process(const Controls& c, const FloatFrame* in,
                             FloatFrame* out, size_t n) {
  // --- Block-rate parameters.
  const float delta = Clamp(130.81f * SemitonesToRatio(c.pitch), 16.0f, 4000.0f);
  const float fa = 2000.0f * exp2f(2.0f * Clamp(c.position, 0.0f, 1.0f));
  const float cutoff = 200.0f * exp2f(5.32f * Clamp(c.size, 0.0f, 1.0f));
  for (int ch = 0; ch < 2; ++ch) {
    band_[ch][0].set(cutoff / kSampleRate, 0.541f);
    band_[ch][1].set(cutoff / kSampleRate, 1.307f);
  }
  const float num = 1.0f + Clamp(c.density, 0.0f, 1.0f) * (kMaxPairs - 1);
  float target_gain[kMaxPairs];
  float total = 1.0f;  // the shared f_a copy
  for (int k = 0; k < kMaxPairs; ++k) {
    target_gain[k] = Clamp(num - k, 0.0f, 1.0f);
    total += target_gain[k];
  }
  const float norm = 0.9f / sqrtf(total);
  const float purity = Clamp(c.texture, 0.0f, 1.0f);
  const float square_gain = purity * purity * 6.0f;

  float inc[kMaxPairs + 1];
  inc[0] = fa / kSampleRate;
  for (int k = 0; k < kMaxPairs; ++k) {
    inc[k + 1] = (fa + delta * kPairRatios[k]) / kSampleRate;
  }

  // Freeze: replace the analytic input by a held constant (a "partial" at
  // 0 Hz), which the shifters turn into pure sine pairs.
  if (c.freeze && !frozen_) {
    held_level_ = Clamp(raw_env_.value() * 1.4f, 0.12f, 0.7f);
  }
  frozen_ = c.freeze;
  const float freeze_target = frozen_ ? 1.0f : 0.0f;

  for (size_t i = 0; i < n; ++i) {
    freeze_mix_ += 0.002f * (freeze_target - freeze_mix_);
    for (int k = 0; k < kMaxPairs; ++k) {
      pair_gain_[k] += 0.002f * (target_gain[k] - pair_gain_[k]);
    }

    // Oscillator phasors (cos, sin) for each shift.
    float co[kMaxPairs + 1], si[kMaxPairs + 1];
    for (int k = 0; k <= kMaxPairs; ++k) {
      phase_[k] += inc[k];
      if (phase_[k] >= 1.0f) phase_[k] -= 1.0f;
      si[k] = FastSin(phase_[k]);
      co[k] = FastCos(phase_[k]);
    }

    float mono = 0.5f * (in[i].l + in[i].r);
    raw_env_.Process(mono);
    float outs[2];
    for (int ch = 0; ch < 2; ++ch) {
      float x = ch == 0 ? in[i].l : in[i].r;
      x = band_[ch][1].Lp(band_[ch][0].Lp(x));
      if (ch == 0) band_env_.Process(x);
      x *= agc_;
      float re, im;
      hilbert_[ch].Process(x, &re, &im);
      re = re * (1.0f - freeze_mix_) + held_level_ * freeze_mix_;
      im = im * (1.0f - freeze_mix_);
      // Upper-sideband shift (sign matches this Hilbert pair: im lags re).
      float s = re * co[0] + im * si[0];
      for (int k = 0; k < kMaxPairs; ++k) {
        s += pair_gain_[k] * (re * co[k + 1] + im * si[k + 1]);
      }
      s *= norm;
      // Purity: a quadratic term makes the difference tone physical.
      float sq = square_lp_[ch].Lp(s * s) * square_gain;
      outs[ch] = dc_[ch].Process(s + sq);
    }
    out[i].l = outs[0];
    out[i].r = outs[1];
  }

  // Automatic gain: keep the pairs at the input's loudness even when SIZE
  // band-limits the input heavily.
  float target = raw_env_.value() / (band_env_.value() + 1e-4f);
  target = Clamp(target, 1.0f, 12.0f);
  agc_ += 0.05f * (target - agc_);
}

}  // namespace monsoon

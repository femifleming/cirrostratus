// Mode 21: ENSEMBLE (see ensemble.h).

#include "monsoon/dsp/modes/ensemble.h"

namespace monsoon {

namespace {

struct Character {
  float weight[EnsembleMode::kTaps];
  float slow;   // share of the slow depth
  float fast;   // fast depth in ms at DENSITY 1
  float dry;
};

const Character kChorus = { { 1.0f, 0.6f, 0.0f }, 1.0f, 0.0f, 0.7f };
const Character kEnsemble = { { 1.0f, 1.0f, 1.0f }, 1.0f, 0.35f, 0.45f };
const Character kVibrato = { { 1.0f, 0.0f, 0.0f }, 0.0f, 1.5f, 0.0f };

const float kSlowDepthMs = 3.5f;

}  // namespace

bool EnsembleMode::Init(Arena* arena) {
  for (int ch = 0; ch < 2; ++ch) {
    line_[ch] = arena->Allocate<float>(kLineSize);
    if (!line_[ch]) return false;
    bbd_[ch][0].Init();
    bbd_[ch][1].Init();
  }
  write_ = 0;
  slow_phase_ = fast_phase_ = 0.0f;
  base_delay_ = 480.0f;
  frozen_ = false;
  return true;
}

inline float EnsembleMode::Tap(int ch, float delay) const {
  float pos = static_cast<float>(write_) - delay + kLineSize;
  int32_t i = static_cast<int32_t>(pos);
  float t = pos - static_cast<float>(i);
  const float* l = line_[ch];
  const size_t m = kLineSize - 1;
  return Hermite(l[(i - 1) & m], l[i & m], l[(i + 1) & m], l[(i + 2) & m], t);
}

void EnsembleMode::Process(const Controls& c, const FloatFrame* in,
                           FloatFrame* out, size_t n) {
  if (c.trigger) slow_phase_ = fast_phase_ = 0.0f;
  frozen_ = c.freeze;

  // Character morph.
  float p = Clamp(c.position, 0.0f, 1.0f);
  const Character& a = p < 0.5f ? kChorus : kEnsemble;
  const Character& b = p < 0.5f ? kEnsemble : kVibrato;
  float t = p < 0.5f ? p * 2.0f : (p - 0.5f) * 2.0f;
  float w[kTaps];
  float norm = 0.0f;
  for (int k = 0; k < kTaps; ++k) {
    w[k] = Crossfade(a.weight[k], b.weight[k], t);
    norm += w[k] * w[k];
  }
  norm = 0.9f / sqrtf(norm + 1e-6f);
  for (int k = 0; k < kTaps; ++k) w[k] *= norm;
  const float dry = Crossfade(a.dry, b.dry, t);
  const float wet = sqrtf(1.0f - dry * dry);

  // Depths in samples.
  const float density = Clamp(c.density, 0.0f, 1.0f);
  const float ms = kSampleRate * 0.001f;
  const float slow_depth = Crossfade(a.slow, b.slow, t) * kSlowDepthMs * density * ms;
  const float fast_depth = Crossfade(a.fast, b.fast, t) * density * ms;

  float base_target = (5.0f + 10.0f * Clamp(c.size, 0.0f, 1.0f)) * ms;
  float base_start = base_delay_;
  base_delay_ += 0.05f * (base_target - base_delay_);
  const float base_inc = (base_delay_ - base_start) / static_cast<float>(n);

  const float rate = SemitonesToRatio(c.pitch);
  const float slow_inc = frozen_ ? 0.0f : 0.6f * rate / kSampleRate;
  const float fast_inc = frozen_ ? 0.0f : 6.0f * rate / kSampleRate;

  // Bucket-brigade darkness.
  const float texture = Clamp(c.texture, 0.0f, 1.0f);
  const float cutoff = 20000.0f * powf(3500.0f / 20000.0f, texture);
  for (int ch = 0; ch < 2; ++ch) {
    bbd_[ch][0].set(cutoff / kSampleRate, 0.541f);
    bbd_[ch][1].set(cutoff / kSampleRate, 1.307f);
  }
  const bool dark = texture > 0.01f;
  const float drive = 1.0f + 1.5f * texture;
  const float inv_drive = 1.0f / drive;

  float base = base_start;
  for (size_t s = 0; s < n; ++s) {
    base += base_inc;
    line_[0][write_] = in[s].l;
    line_[1][write_] = in[s].r;
    slow_phase_ += slow_inc;
    if (slow_phase_ >= 1.0f) slow_phase_ -= 1.0f;
    fast_phase_ += fast_inc;
    if (fast_phase_ >= 1.0f) fast_phase_ -= 1.0f;

    float y[2];
    for (int ch = 0; ch < 2; ++ch) {
      float sum = 0.0f;
      for (int k = 0; k < kTaps; ++k) {
        if (w[k] < 1e-4f) continue;
        float offset = k * (1.0f / 3.0f) + ch * (1.0f / 6.0f);
        float d = base +
            slow_depth * 0.5f * (1.0f + FastSin(slow_phase_ + offset)) +
            fast_depth * 0.5f * (1.0f + FastSin(fast_phase_ + offset));
        sum += w[k] * Tap(ch, d);
      }
      if (dark) {
        sum = bbd_[ch][1].Lp(bbd_[ch][0].Lp(sum));
        sum = SoftClip(sum * drive) * inv_drive;
      }
      y[ch] = sum;
    }
    out[s].l = dry * in[s].l + wet * y[0];
    out[s].r = dry * in[s].r + wet * y[1];
    write_ = (write_ + 1) & (kLineSize - 1);
  }
}

}  // namespace monsoon

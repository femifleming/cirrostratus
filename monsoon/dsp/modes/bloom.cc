// Mode 27 - BLOOM (see bloom.h).

#include "monsoon/dsp/modes/bloom.h"

namespace monsoon {

namespace {
const size_t kApLengths[2][BloomMode::kStages] = {
  { 113, 263, 449, 677 },
  { 127, 281, 467, 701 },
};
const float kMinWindow = 0.05f * kSampleRate;
const float kMaxWindow = 0.28f * kSampleRate;
}  // namespace

float BloomMode::WindowSamples(float size) {
  return kMinWindow + Clamp(size, 0.0f, 1.0f) * (kMaxWindow - kMinWindow);
}

bool BloomMode::Init(Arena* arena) {
  size_t ap_floats = 0;
  for (int ch = 0; ch < 2; ++ch) {
    for (int s = 0; s < kStages; ++s) ap_floats += kApLengths[ch][s];
  }
  const size_t reserve = ap_floats * sizeof(float) + 256;
  // Room for 2 W of delay plus a margin for the Hermite reads.
  if (!buffer_.Init(arena, 2 * static_cast<size_t>(kMaxWindow + 2048) * 4,
                    2, reserve)) {
    return false;
  }
  if (buffer_.size() < 2 * kMaxWindow + 1024) return false;
  for (int ch = 0; ch < 2; ++ch) {
    for (int s = 0; s < kStages; ++s) {
      Diffuser& d = ap_[ch][s];
      d.size = kApLengths[ch][s];
      d.line = arena->Allocate<float>(d.size);
      d.pos = 0;
      if (!d.line) return false;
    }
  }
  window_ = WindowSamples(0.5f);
  phase_[0] = 0.0f;
  phase_[1] = 0.5f;
  start_[0] = start_[1] = 0.0f;
  env_fast_ = env_slow_ = 0.0f;
  dry_gain_ = 1.0f;
  frozen_ = false;
  was_trigger_ = false;
  return true;
}

void BloomMode::Process(const Controls& c, const FloatFrame* in,
                        FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  if (!frozen_) buffer_.WriteBlock(in);
  const float size_f = static_cast<float>(buffer_.size());
  const float head = static_cast<float>(buffer_.write_head());

  if (c.trigger && !was_trigger_) {
    phase_[0] = 1.0f;  // both restart below
    phase_[1] = 0.5f;
    start_[1] = head - static_cast<float>(n) - 0.5f * window_;
  }
  was_trigger_ = c.trigger;

  // Block-rate parameters.
  const float w_target = WindowSamples(c.size);
  const float ratio = SemitonesToRatio(Clamp(c.pitch, -12.0f, 12.0f));
  const float bloom = c.density * 1.2f;
  const float diffusion = c.texture * 0.72f;
  // Transient softener: slow envelope attack 1 ms ... 2 s.
  const float swell_s = 0.001f * exp2f(c.position * 10.97f);
  const float slow_a = 1.0f - expf(-1.0f / (swell_s * kSampleRate));
  const float fast_a = 1.0f - expf(-1.0f / (0.001f * kSampleRate));
  const float release = 1.0f - expf(-1.0f / (0.15f * kSampleRate));
  const float dry_target = frozen_ ? 0.0f : 1.0f;

  for (size_t i = 0; i < n; ++i) {
    // The window glides (the dry delay bends a little while SIZE moves).
    window_ += 0.0005f * (w_target - window_);
    const float now = frozen_
        ? head - 1.0f
        : head - static_cast<float>(n) + static_cast<float>(i);

    // ---- Delayed dry, with the transient softener.
    float pos = now - 2.0f * window_;
    if (pos < 0.0f) pos += size_f;
    float dl = buffer_.Read(0, pos);
    float dr = buffer_.Read(1, pos);
    const float a = 0.5f * (fabsf(dl) + fabsf(dr));
    env_fast_ += (a > env_fast_ ? fast_a : release) * (a - env_fast_);
    env_slow_ += (a > env_slow_ ? slow_a : release) * (a - env_slow_);
    float g = env_fast_ > 1e-6f ? env_slow_ / env_fast_ : 1.0f;
    if (g > 1.0f) g = 1.0f;
    dry_gain_ += 0.0005f * (dry_target - dry_gain_);
    g *= dry_gain_;

    // ---- Reverse heads.
    float rev[2] = { 0.0f, 0.0f };
    const float inc = 1.0f / window_;
    for (int h = 0; h < 2; ++h) {
      phase_[h] += inc;
      if (phase_[h] >= 1.0f) {
        phase_[h] -= 1.0f;
        start_[h] = now;  // read backwards from the newest sample
      }
      const float ph = phase_[h];
      float p = start_[h] - ratio * ph * window_;
      while (p < 0.0f) p += size_f;
      while (p >= size_f) p -= size_f;
      // Rising envelope, quick fade at the very end of the chunk.
      float w = ph < 0.88f ? (ph / 0.88f) : (1.0f - ph) / 0.12f;
      w = w * w;
      rev[0] += buffer_.Read(0, p) * w;
      rev[1] += buffer_.Read(1, p) * w;
    }

    // ---- Diffusion (allpass chain per side).
    for (int ch = 0; ch < 2; ++ch) {
      float x = rev[ch];
      if (diffusion > 0.01f) {
        for (int s = 0; s < kStages; ++s) {
          Diffuser& d = ap_[ch][s];
          const float delayed = d.line[d.pos];
          const float v = x + diffusion * delayed;
          d.line[d.pos] = v;
          if (++d.pos >= d.size) d.pos = 0;
          x = delayed - diffusion * v;
        }
      }
      rev[ch] = x * bloom;
    }

    out[i].l = dl * g + rev[0];
    out[i].r = dr * g + rev[1];
  }
}

}  // namespace monsoon

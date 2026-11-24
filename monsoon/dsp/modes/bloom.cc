// Mode 27 - BLOOM (see bloom.h).

#include "monsoon/dsp/modes/bloom.h"

#include "monsoon/dsp/modes/bal_knob.h"

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
  rng_.Seed(0x5eed1u);
  haze_[0] = haze_[1] = 0.0f;
  for (int h = 0; h < 2; ++h) {
    next_forward_[h] = next_fall_[h] = false;
    off_[h] = 0.0f;
    dir_[h] = 1.0f;
    rise_[h] = 0.88f;
    fade_[h] = 0.12f;
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

// BAL: the next chunk's direction and envelope (no random draws at noon).
void BloomMode::PickChunk(int h, float bal) {
  const bool pick = bal != 0.0f && rng_.Float() < fabsf(bal);
  next_forward_[h] = pick && bal > 0.0f;
  next_fall_[h] = pick && bal < 0.0f;
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

  const float bal = BalKnob(c.bal);

  // Block-rate parameters.
  const float w_target = WindowSamples(c.size);
  const float ratio = SemitonesToRatio(Clamp(c.pitch, -12.0f, 12.0f));
  const float bloom = c.density * 1.2f;
  // RVB: the diffusion chain becomes a loop (gain up to 0.85, damped),
  // so the bloom's haze rings on as a tail; it brings its own diffusion.
  const float haze = 0.85f * Clamp(c.rvb, 0.0f, 1.0f);
  float diffusion = c.texture * 0.72f;
  if (haze > 0.0f && diffusion < 0.6f) diffusion = 0.6f;
  // Transient softener: slow envelope attack 1 ms ... 2 s.
  const float swell_s = 0.001f * exp2f(c.position * 10.97f);
  const float slow_a = 1.0f - expf(-1.0f / (swell_s * kSampleRate));
  const float fast_a = 1.0f - expf(-1.0f / (0.001f * kSampleRate));
  const float release = 1.0f - expf(-1.0f / (0.15f * kSampleRate));
  const float dry_target = frozen_ ? 0.0f : 1.0f;

  int picked = 0;
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
        // BAL: this chunk's direction and envelope (picked in advance).
        off_[h] = next_forward_[h] ? 1.0f : 0.0f;
        dir_[h] = next_forward_[h] ? -1.0f : 1.0f;
        rise_[h] = next_fall_[h] ? 0.12f : 0.88f;
        fade_[h] = next_fall_[h] ? 0.88f : 0.12f;
        picked |= 1 << h;
      }
      const float ph = phase_[h];
      // Forward chunks read the same stretch of buffer the other way.
      float p = start_[h] - ratio * (off_[h] + dir_[h] * ph) * window_;
      while (p < 0.0f) p += size_f;
      while (p >= size_f) p -= size_f;
      // Rising envelope, quick fade at the very end of the chunk (or, for
      // BAL's falling chunks, the mirror image).
      float w = ph < rise_[h] ? (ph / rise_[h]) : (1.0f - ph) / fade_[h];
      w = w * w;
      rev[0] += buffer_.Read(0, p) * w;
      rev[1] += buffer_.Read(1, p) * w;
    }

    // ---- Diffusion (allpass chain per side). RVB closes it into a loop.
    for (int ch = 0; ch < 2; ++ch) {
      float x = rev[ch] + haze * haze_[ch];
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
      haze_[ch] += 0.5f * (x - haze_[ch]);  // ~5.5 kHz damping
      rev[ch] = x * bloom;
    }

    out[i].l = dl * g + rev[0];
    out[i].r = dr * g + rev[1];
  }
  if (picked & 1) PickChunk(0, bal);
  if (picked & 2) PickChunk(1, bal);
}

}  // namespace monsoon

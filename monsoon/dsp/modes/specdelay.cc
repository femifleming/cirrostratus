// Mode 23 - SPECDELAY (see specdelay.h).

#include "monsoon/dsp/modes/specdelay.h"

namespace monsoon {

bool SpecDelayMode::Init(Arena* arena) {
  // The delay memory first: it takes most of the biggest region.
  const size_t frame_bytes = kBins * 2 * sizeof(int16_t);
  size_t fit = (arena->largest_free() - 64) / frame_bytes;
  frames_ = fit < kMaxFrames ? fit : kMaxFrames;
  if (frames_ < 8) return false;
  memory_ = arena->Allocate<int16_t>(frames_ * kBins * 2);
  scale_ = arena->Allocate<float>(frames_);
  log_pos_ = arena->Allocate<float>(kBins);
  if (!memory_ || !scale_ || !log_pos_) return false;
  if (!stft_.Init(arena, kFftSize, kHop, 2, this)) return false;
  const float inv = 1.0f / log2f(static_cast<float>(kBins - 1));
  log_pos_[0] = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    log_pos_[k] = log2f(static_cast<float>(k)) * inv;
  }
  write_ = 0;
  memset(&ctl_, 0, sizeof(ctl_));
  ctl_.position = 0.5f;
  ctl_.texture = 0.5f;
  clear_ = false;
  return true;
}

void SpecDelayMode::Process(const Controls& c, const FloatFrame* in,
                            FloatFrame* out, size_t n) {
  ctl_ = c;
  if (c.trigger) clear_ = true;
  stft_.Process(in, out, n);
}

inline void SpecDelayMode::Read(size_t frame, size_t k, float* re,
                                float* im) const {
  const int16_t* p = memory_ + (frame * kBins + k) * 2;
  const float s = scale_[frame];
  *re = p[0] * s;
  *im = p[1] * s;
}

void SpecDelayMode::Transform(float* in, float* out[2], size_t fft_size) {
  (void)fft_size;
  const Controls c = ctl_;
  if (clear_) {
    memset(memory_, 0, frames_ * kBins * 2 * sizeof(int16_t));
    memset(scale_, 0, frames_ * sizeof(float));
    clear_ = false;
  }
  const size_t h = kFftSize / 2;
  float* in_re = in;
  float* in_im = in + h;
  float* l_re = out[0];
  float* l_im = out[0] + h;
  float* r_re = out[1];
  float* r_im = out[1] + h;

  // Delay curve: lows last (POSITION 0) ... flat ... highs last (1).
  const float tilt = (c.position - 0.5f) * 2.0f;
  const float span = c.size * static_cast<float>(frames_ - 2);
  // Feedback and its tilt; frozen = lossless.
  const bool freeze = c.freeze;
  float fb = freeze ? 1.0f : c.density * 0.98f;
  const float fb_tilt = (c.texture - 0.5f) * 2.0f;
  // Spectral shift per pass. Shifting up maps one source bin to ~r target
  // bins, so the feedback is scaled by 1/sqrt(r) to keep the energy level.
  const float ratio = SemitonesToRatio(Clamp(c.pitch, -24.0f, 24.0f));
  const float inv_ratio = 1.0f / ratio;
  const bool shift = fabsf(c.pitch) > 0.05f;
  if (ratio > 1.0f) fb *= 1.0f / sqrtf(ratio);

  // 1. Read the delayed bins: left on its curve, right one frame later on
  //    odd bins.
  for (size_t k = 1; k < h; ++k) {
    float curve = 0.5f + 0.5f * tilt * (2.0f * log_pos_[k] - 1.0f);
    size_t d = 1 + static_cast<size_t>(curve * span + 0.5f);
    if (d > frames_ - 1) d = frames_ - 1;
    size_t dr = d + (k & 1);
    size_t fl = (write_ + frames_ - d) % frames_;
    size_t fr = (write_ + frames_ - dr) % frames_;
    Read(fl, k, &l_re[k], &l_im[k]);
    Read(fr, k, &r_re[k], &r_im[k]);
  }

  // 2. The new frame (built in place over the input spectrum) = input +
  //    shifted, tilted feedback of the left read.
  float peak = 0.0f;
  for (size_t k = 1; k < h; ++k) {
    size_t src = k;
    if (shift) {
      src = static_cast<size_t>(static_cast<float>(k) * inv_ratio + 0.5f);
    }
    float fb_re = 0.0f, fb_im = 0.0f;
    if (src >= 1 && src < h) {
      float g = fb * (1.0f + fb_tilt * (2.0f * log_pos_[k] - 1.0f) * 0.5f);
      if (!freeze && g > 0.985f) g = 0.985f;
      if (g > 1.0f) g = 1.0f;
      fb_re = l_re[src] * g;
      fb_im = l_im[src] * g;
    }
    float re = (freeze ? 0.0f : in_re[k]) + fb_re;
    float im = (freeze ? 0.0f : in_im[k]) + fb_im;
    in_re[k] = re;
    in_im[k] = im;
    float a = fabsf(re) > fabsf(im) ? fabsf(re) : fabsf(im);
    if (a > peak) peak = a;
  }

  // 3. Store it: int16 with a per-frame scale.
  int16_t* dst = memory_ + write_ * kBins * 2;
  dst[0] = dst[1] = 0;
  if (peak < 1e-12f) {
    memset(dst, 0, kBins * 2 * sizeof(int16_t));
    scale_[write_] = 0.0f;
  } else {
    const float q = 32767.0f / peak;
    scale_[write_] = peak / 32767.0f;
    for (size_t k = 1; k < h; ++k) {
      float re = in_re[k] * q, im = in_im[k] * q;
      dst[2 * k] = static_cast<int16_t>(re >= 0.0f ? re + 0.5f : re - 0.5f);
      dst[2 * k + 1] =
          static_cast<int16_t>(im >= 0.0f ? im + 0.5f : im - 0.5f);
    }
  }
  if (++write_ >= frames_) write_ = 0;
}

}  // namespace monsoon

// Mode 16 — QUELL: resonance suppressor. See quell.h.

#include "monsoon/dsp/modes/quell.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

bool QuellMode::Init(Arena* arena) {
  if (!stft_.Init(arena, kFftSize, kHop, this)) return false;
  log_mag_ = arena->Allocate<float>(kBins);
  prefix_ = arena->Allocate<float>(kBins + 1);
  cut_ = arena->Allocate<float>(kBins + 1);
  weight_ = arena->Allocate<float>(kBins);
  if (!log_mag_ || !prefix_ || !cut_ || !weight_) return false;
  focus_built_ = -1.0f;
  position_ = 0.5f;
  size_ = 0.3f;
  density_ = 0.0f;
  texture_ = 0.5f;
  freeze_ = false;
  delta_ = false;
  prev_trigger_ = false;
  trim_ = 1.0f;
  return true;
}

void QuellMode::Process(const Controls& c, const FloatFrame* in,
                        FloatFrame* out, size_t n) {
  position_ = c.position;
  size_ = c.size;
  density_ = c.density;
  texture_ = c.texture;
  freeze_ = c.freeze;
  if (c.trigger && !prev_trigger_) delta_ = !delta_;
  prev_trigger_ = c.trigger;
  stft_.Process(in, out, n);
  // PITCH: output trim, +/- 12 dB.
  const float target = exp2f(c.pitch * (1.0f / 24.0f));
  const float inc = (target - trim_) / static_cast<float>(n);
  for (size_t i = 0; i < n; ++i) {
    trim_ += inc;
    out[i].l *= trim_;
    out[i].r *= trim_;
  }
}

float QuellMode::cut_db(float hz) const {
  size_t k = static_cast<size_t>(hz * kFftSize / kSampleRate + 0.5f);
  if (k < 1) k = 1;
  if (k >= kBins) k = kBins - 1;
  return cut_[k] * 6.0206f;
}

void QuellMode::Transform(float* spectrum[2], size_t fft_size) {
  (void)fft_size;
  const size_t h = kBins;
  float* re0 = spectrum[0];
  float* im0 = spectrum[0] + h;
  float* re1 = spectrum[1];
  float* im1 = spectrum[1] + h;

  // Focus weighting (rebuilt only when POSITION moves).
  const float position = position_;
  if (fabsf(position - focus_built_) > 0.002f) {
    const float fc = 100.0f * FastExp2(position * 7.32f);  // 100 Hz .. 16 kHz
    const float bin_hz = kSampleRate / kFftSize;
    for (size_t k = 1; k < h; ++k) {
      const float d = FastLog2(static_cast<float>(k) * bin_hz / fc) * 0.8f;
      weight_[k] = 0.25f + 0.75f / (1.0f + d * d);
    }
    focus_built_ = position;
  }

  // Log magnitude of the linked stereo power and its prefix sums.
  float frame_max = 0.0f;
  prefix_[0] = 0.0f;
  prefix_[1] = 0.0f;
  log_mag_[0] = 0.0f;
  for (size_t k = 1; k < h; ++k) {
    const float p = re0[k] * re0[k] + im0[k] * im0[k] +
        re1[k] * re1[k] + im1[k] * im1[k];
    if (p > frame_max) frame_max = p;
    log_mag_[k] = 0.5f * FastLog2(p + 1e-24f);
    prefix_[k + 1] = prefix_[k] + log_mag_[k];
  }
  const float floor_log = 0.5f * FastLog2(frame_max + 1e-24f) - 13.3f;  // -80 dB

  // Cut targets and their smoothing (unless frozen).
  if (!freeze_) {
    const float octaves = 1.0f / 12.0f + size_ * (1.0f - 1.0f / 12.0f);
    const float half = FastExp2(octaves * 0.5f) - 1.0f;
    const float depth = density_ * 4.0f;  // log2 units: 4 = 24 dB
    // TEXTURE: attack 50 -> 3 ms, release 400 -> 40 ms (per 5.3 ms hop).
    const float hop_s = static_cast<float>(kHop) / kSampleRate;
    const float t = texture_;
    const float att_s = 0.05f * FastExp2(-t * 4.06f);
    const float rel_s = 0.4f * FastExp2(-t * 3.32f);
    const float att = 1.0f - FastExp2(-hop_s / att_s * 1.4427f);
    const float rel = 1.0f - FastExp2(-hop_s / rel_s * 1.4427f);
    for (size_t k = 1; k < h; ++k) {
      int w = static_cast<int>(static_cast<float>(k) * half);
      if (w < 2) w = 2;
      int lo = static_cast<int>(k) - w;
      int hi = static_cast<int>(k) + w;
      if (lo < 1) lo = 1;
      if (hi > static_cast<int>(h) - 1) hi = static_cast<int>(h) - 1;
      const float env = (prefix_[hi + 1] - prefix_[lo]) /
          static_cast<float>(hi - lo + 1);
      float target = 0.0f;
      if (log_mag_[k] > floor_log) {
        float excess = log_mag_[k] - env - 0.5f;  // above 3 dB
        if (excess > 0.0f) {
          target = excess * weight_[k] * 1.5f;
          if (target > depth) target = depth;
        }
      }
      float& cut = cut_[k];
      cut += (target > cut ? att : rel) * (target - cut);
    }
  }

  // Apply, smoothing the curve across neighbouring bins.
  const bool delta = delta_;
  float prev = cut_[1];
  for (size_t k = 1; k < h; ++k) {
    const float next = k + 1 < h ? cut_[k + 1] : cut_[k];
    const float c = 0.25f * prev + 0.5f * cut_[k] + 0.25f * next;
    prev = cut_[k];
    float g = FastExp2(-c);
    if (delta) g = 1.0f - g;
    re0[k] *= g; im0[k] *= g;
    re1[k] *= g; im1[k] *= g;
  }
}

}  // namespace monsoon

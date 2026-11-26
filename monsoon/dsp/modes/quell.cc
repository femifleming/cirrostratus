// Mode 16 — QUELL: resonance suppressor. See quell.h.

#include "monsoon/dsp/modes/quell.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

__attribute__((cold)) bool QuellMode::Init(Arena* arena) {
  kFftSize = FftSize();
  kHop = kFftSize / 4;
  kBins = kFftSize / 2;
  // log2(N / 1024), exactly (0 at the native size).
  int octaves = 0;
  for (size_t n = kFftSize; n > 1024; n >>= 1) ++octaves;
  for (size_t n = kFftSize; n < 1024; n <<= 1) --octaves;
  level_log2_ = static_cast<float>(octaves);
  if (!stft_.Init(arena, kFftSize, kHop, this)) return false;
  log_mag_ = arena->Allocate<float>(kBins);
  prefix_ = arena->Allocate<float>(kBins + 1);
  cut_ = arena->Allocate<float>(kBins + 1);
  weight_ = arena->Allocate<float>(kBins);
  halo_ = arena->Allocate<float>(kBins);
  if (!log_mag_ || !prefix_ || !cut_ || !weight_ || !halo_) return false;
  focus_built_ = -1.0f;
  position_ = 0.5f;
  size_ = 0.3f;
  density_ = 0.0f;
  texture_ = 0.5f;
  bal_ = 0.5f;
  fbk_ = 0.0f;
  rvb_ = 0.0f;
  freeze_ = false;
  halo_live_ = false;
  rng_.Seed(0x9E11);
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
  bal_ = c.bal;
  fbk_ = c.fbk;
  rvb_ = c.rvb;
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
  // BAL: mid (left) or side (right) focus. The detector blends from the
  // linked L+R power toward 2 |M|^2 or 2 |S|^2 (same scale for mono).
  const float bal = KnobBipolar(bal_);
  const float focus = fabsf(bal);
  const float ms_sign = bal < 0.0f ? 1.0f : -1.0f;  // M = L+R, S = L-R
  for (size_t k = 1; k < h; ++k) {
    float p = re0[k] * re0[k] + im0[k] * im0[k] +
        re1[k] * re1[k] + im1[k] * im1[k];
    if (focus > 0.0f) {
      const float fr = re0[k] + ms_sign * re1[k];
      const float fi = im0[k] + ms_sign * im1[k];
      p += focus * (0.5f * (fr * fr + fi * fi) - p);
    }
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
    // FBK: hard threshold, 0 dBFS (a full-scale sine is ~8.85 here) down
    // to -72 dBFS; off at zero.
    const float fbk = fbk_;
    const bool hard = fbk > 0.0f;
    const float hard_thr = 9.0f - 12.0f * fbk + level_log2_;
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
        if (hard) {
          float over = (log_mag_[k] - hard_thr) * weight_[k] * 1.5f;
          if (over > depth) over = depth;
          if (over > target) target = over;
        }
      }
      float& cut = cut_[k];
      cut += (target > cut ? att : rel) * (target - cut);
    }
  }

  // RVB: halo decay per hop (T60 0.3 s -> ~5 s) and level.
  const float rvb = rvb_;
  const bool halo = rvb > 0.0f;
  float halo_decay = 0.0f, halo_gain = 0.0f;
  if (halo) {
    if (!halo_live_) {
      memset(halo_, 0, kBins * sizeof(float));
      halo_live_ = true;
    }
    // Per hop: 1 - hop / tau, T60 = 6.9 tau = 0.3 s + 4.7 s x RVB^2.
    halo_decay = 1.0f - (6.9f * static_cast<float>(kHop) / kSampleRate) /
        (0.3f + 4.7f * rvb * rvb);
    halo_gain = rvb * (1.0f / 128.0f);  // the random bytes below are +/-128
  } else {
    halo_live_ = false;
  }

  // Apply, smoothing the curve across neighbouring bins.
  const bool delta = delta_;
  float prev = cut_[1];
  for (size_t k = 1; k < h; ++k) {
    const float next = k + 1 < h ? cut_[k + 1] : cut_[k];
    const float c = 0.25f * prev + 0.5f * cut_[k] + 0.25f * next;
    prev = cut_[k];
    float g = FastExp2(-c);
    const float removed = 1.0f - g;
    // BAL: the focused part (M or S) gets the cut, the other part less of
    // it. As a L/R matrix; at noon a = g, x = 0 (v1.0 exactly).
    float go = focus > 0.0f ? 1.0f + (g - 1.0f) * (1.0f - focus) : g;
    if (delta) { g = 1.0f - g; go = 1.0f - go; }
    const float a = 0.5f * (g + go);
    const float x = 0.5f * ms_sign * (g - go);
    const float r0 = re0[k], i0 = im0[k], r1 = re1[k], i1 = im1[k];
    re0[k] = a * r0 + x * r1;
    im0[k] = a * i0 + x * i1;
    re1[k] = x * r0 + a * r1;
    im1[k] = x * i0 + a * i1;
    if (halo) {
      // The removed amplitude (|re| + |im| of both sides, roughly the
      // magnitude) feeds a decaying per-bin level that comes back with
      // fresh random phases on each side every hop: four random bytes
      // are a random point in the square for each side.
      float& hk = halo_[k];
      hk = halo_decay * hk + (1.0f - halo_decay) * removed *
          (fabsf(r0) + fabsf(i0) + fabsf(r1) + fabsf(i1));
      const float amp = hk * halo_gain;
      const uint32_t rnd = rng_.Next();
      re0[k] += amp * static_cast<float>(static_cast<int8_t>(rnd));
      im0[k] += amp * static_cast<float>(static_cast<int8_t>(rnd >> 8));
      re1[k] += amp * static_cast<float>(static_cast<int8_t>(rnd >> 16));
      im1[k] += amp * static_cast<float>(static_cast<int8_t>(rnd >> 24));
    }
  }
}

}  // namespace monsoon

// Mode 29 - MIRROR (see mirror.h).

#include "monsoon/dsp/modes/mirror.h"

#include "monsoon/dsp/modes/bal_knob.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;


// The warp runs once per partial, so it can afford tighter approximations
// than spectral_util's (pitch errors there were ~0.7 %).
static inline float Log2(float x) {
  union { float f; uint32_t i; } u = { x };
  const float e = static_cast<float>(static_cast<int32_t>((u.i >> 23) & 255) - 127);
  u.i = (u.i & 0x007FFFFF) | 0x3F800000;  // m in [1, 2)
  const float t = (u.f - 1.0f) / (u.f + 1.0f);
  const float t2 = t * t;
  return e + 2.8853900818f * t *
      (1.0f + t2 * (0.3333333f + t2 * (0.2f + t2 * 0.1428571f)));
}

static inline float Exp2(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 126.0f) x = 126.0f;
  int32_t i = static_cast<int32_t>(x);
  if (x < static_cast<float>(i)) --i;
  const float y = (x - static_cast<float>(i)) * 0.69314718f;
  const float p = 1.0f + y * (1.0f + y * (0.5f + y * (0.16666667f + y *
      (0.04166667f + y * (0.00833333f + y * 0.00138889f)))));
  union { float f; uint32_t i; } u;
  u.i = static_cast<uint32_t>(i + 127) << 23;
  return u.f * p;
}

bool MirrorMode::Init(Arena* arena) {
  kFftSize = FftSize();
  kHop = kFftSize / 4;
  kBins = kFftSize / 2;
  kBinHz = kSampleRate / static_cast<float>(kFftSize);
  frame_ratio_ = static_cast<float>(kHop) / 256.0f;
  if (!stft_.Init(arena, kFftSize, kHop, 1, this)) return false;
  mag_ = arena->Allocate<float>(kBins);
  freq_ = arena->Allocate<float>(kBins);
  an_phase_ = arena->Allocate<float>(kBins);
  syn_phase_ = arena->Allocate<float>(kBins);
  out_mag_ = arena->Allocate<float>(kBins);
  best_ = arena->Allocate<float>(kBins);
  pfreq_ = arena->Allocate<float>(kBins);
  off_ = arena->Allocate<int16_t>(kBins);
  fb_mag_ = arena->Allocate<float>(2 * kBins);
  if (!mag_ || !freq_ || !an_phase_ || !syn_phase_ || !out_mag_ || !best_ ||
      !pfreq_ || !off_ || !fb_mag_) {
    return false;
  }
  fb_freq_ = fb_mag_ + kBins;
  memset(out_mag_, 0, sizeof(float) * kBins);
  src_energy_ = 0.0f;
  ref_energy_ = 0.0f;
  for (size_t k = 0; k < kBins; ++k) freq_[k] = static_cast<float>(k);
  memset(&ctl_, 0, sizeof(ctl_));
  grab_ = false;
  return true;
}

void MirrorMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  ctl_ = c;
  if (c.trigger) grab_ = true;
  stft_.Process(in, out, n);
}

float MirrorMode::Warp(float f, float anchor, float position, float amount) {
  if (f < 0.5f) f = 0.5f;
  const float x = f / anchor;
  // stretch
  const float g1 = anchor * Exp2(1.6f * Log2(x));
  // log mirror
  const float g2 = anchor / x;
  // fold (triangle, period 2A)
  float y = x - 2.0f * static_cast<float>(static_cast<int32_t>(x * 0.5f));
  if (y > 1.0f) y = 2.0f - y;
  const float g3 = anchor * y;
  float g;
  if (position < 0.5f) {
    g = g1 + (g2 - g1) * (position * 2.0f);
  } else {
    g = g2 + (g3 - g2) * ((position - 0.5f) * 2.0f);
  }
  return f + (g - f) * amount;
}

// FBK: per bin, the louder of the new input and the last output (scaled
// so that the warp, tilt and overlaps never add energy) is what gets
// warped. Under a sustained input the trails may add at most +3 dB; once
// the input stops, that allowance decays with the trails themselves.
void MirrorMode::Feedback(float fbk) {
  const size_t h = kBins;
  float in_e = 0.0f;
  for (size_t k = 1; k < h; ++k) in_e += mag_[k] * mag_[k];
  if (fbk <= 0.0f) {
    src_energy_ = ref_energy_ = in_e;
    return;
  }
  float out_e = 0.0f;
  for (size_t k = 1; k < h; ++k) out_e += out_mag_[k] * out_mag_[k];
  // Per frame: the same trail times at every FFT size.
  float g = PerFrame(0.95f * fbk, frame_ratio_);
  const float g2 = frame_ratio_ == 1.0f ? 0.9025f * fbk * fbk : g * g;
  if (out_e > src_energy_) g *= sqrtf(src_energy_ / out_e);
  float e = 0.0f;
  fb_mag_[0] = fb_freq_[0] = 0.0f;
  for (size_t k = 1; k < h; ++k) {
    const float f = g * out_mag_[k];
    const bool fed = f > mag_[k];
    fb_mag_[k] = fed ? f : mag_[k];
    fb_freq_[k] = fed ? pfreq_[k] : freq_[k];
    e += fb_mag_[k] * fb_mag_[k];
  }
  ref_energy_ = in_e > ref_energy_ * g2 ? in_e : ref_energy_ * g2;
  if (e > 2.0f * ref_energy_) {
    const float k = sqrtf(2.0f * ref_energy_ / e);
    for (size_t j = 1; j < h; ++j) fb_mag_[j] *= k;
    e = 2.0f * ref_energy_;
  }
  src_energy_ = e;
}

void MirrorMode::Transform(float* in, float* out[2], size_t fft_size) {
  (void)fft_size;
  const Controls c = ctl_;
  const size_t h = kBins;
  float* re = in;
  float* im = in + h;
  float* ore = out[0];
  float* oim = out[0] + h;
  const bool hold = c.freeze && !grab_;
  grab_ = false;

  // ---- Analysis.
  const float expected = static_cast<float>(kHop) / kFftSize;  // cycles/bin
  const float to_bins = static_cast<float>(kFftSize) / kHop;
  float retain = c.density * 0.97f;
  if (c.density > 0.5f) retain = 0.485f + (c.density - 0.5f) * 1.01f;
  if (retain > 0.995f) retain = 0.995f;
  retain = PerFrame(retain, frame_ratio_);
  for (size_t k = 1; k < h; ++k) {
    float r = re[k], i = im[k];
    float m = sqrtf(r * r + i * i);
    float phase = FastAtan2Cycles(-i, r);
    float dev = WrapCycles(phase - an_phase_[k] -
                           expected * static_cast<float>(k));
    an_phase_[k] = phase;
    if (!hold) {
      mag_[k] = retain * mag_[k] + (1.0f - retain) * m;
      freq_[k] = static_cast<float>(k) + dev * to_bins;
    }
  }
  mag_[0] = 0.0f;

  // ---- FBK (see Feedback()).
  const float fbk = Clamp(c.fbk, 0.0f, 1.0f);
  Feedback(fbk);
  const float* src_mag = fbk > 0.0f ? fb_mag_ : mag_;
  const float* src_freq = fbk > 0.0f ? fb_freq_ : freq_;

  // ---- Warp: move every peak region rigidly.
  memset(out_mag_, 0, sizeof(float) * h);
  memset(best_, 0, sizeof(float) * h);
  const float anchor = 261.63f * SemitonesToRatio(c.pitch) / kBinHz;
  const float amount = c.size;
  const float position = c.position;
  const float tilt_oct = (c.texture - 0.5f) * 18.0f / 6.0206f;
  const float log_1k = FastLog2(1000.0f / kBinHz);
  // BAL: linear shift in bins (+/-1 kHz, square law), through zero.
  const float bal = BalKnob(c.bal);
  const float shift_bins = bal * fabsf(bal) * (1000.0f / kBinHz);
  ForEachPeakRegion(src_mag, h, [&](size_t lo, size_t hi, size_t p) {
    // (Warp() is never negative, so at noon this is exactly v1.0.)
    const float fw = fabsf(Warp(src_freq[p], anchor, position, amount) +
                           shift_bins);
    const int32_t shift = RoundToInt(fw - static_cast<float>(p));
    float g = 1.0f;
    if (tilt_oct != 0.0f && fw > 1.0f) {
      g = FastExp2(tilt_oct * (FastLog2(fw) - log_1k));
      if (g > 8.0f) g = 8.0f;
    }
    for (size_t k = lo; k <= hi; ++k) {
      const int32_t j = static_cast<int32_t>(k) + shift;
      if (j < 1 || j >= static_cast<int32_t>(h) - 1) continue;
      const float m = src_mag[k] * g;
      if (m > best_[j]) {
        best_[j] = m;
        off_[j] = static_cast<int16_t>(static_cast<int32_t>(k) -
                                       static_cast<int32_t>(p));
        pfreq_[j] = fw;
      }
      out_mag_[j] += m;
    }
  });

  // ---- Synthesis. Peaks advance by their warped frequency; the other bins
  // of a region stay locked to their peak (half a cycle per bin).
  for (size_t j = 1; j < h - 1; ++j) {
    if (out_mag_[j] > 0.0f && off_[j] == 0) {
      syn_phase_[j] = WrapCycles(syn_phase_[j] + pfreq_[j] * expected);
    }
  }
  ore[0] = oim[0] = 0.0f;
  ore[h - 1] = oim[h - 1] = 0.0f;

  for (size_t j = 1; j < h - 1; ++j) {
    const float m = out_mag_[j];
    if (m <= 0.0f) {
      ore[j] = 0.0f;
      oim[j] = 0.0f;
      continue;
    }
    float ph;
    const int32_t o = off_[j];
    if (o == 0) {
      ph = syn_phase_[j];
    } else {
      int32_t peak = static_cast<int32_t>(j) - o;
      if (peak < 1) peak = 1;
      if (peak > static_cast<int32_t>(h) - 2) peak = h - 2;
      ph = WrapCycles(syn_phase_[peak] + 0.5f * static_cast<float>(o));
    }
    ph = SafeCycles(ph);
    ore[j] = m * FastCos(ph);
    oim[j] = -m * FastSin(ph);
  }
}

}  // namespace monsoon

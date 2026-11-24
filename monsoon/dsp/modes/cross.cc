// Mode 6 — CROSS: L/R spectral morph.

#include "monsoon/dsp/modes/cross.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

bool CrossMode::Init(Arena* arena) {
  if (!stft_.Init(arena, kFftSize, kHop, this)) return false;
  float** arrays[] = {
    &env_l_, &env_r_, &car_mag_, &car_freq_, &car_phase_, &syn_phase_,
    &out_mag_, &out_freq_, &smooth_l_, &smooth_r_
  };
  for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i) {
    *arrays[i] = arena->Allocate<float>(kBins);
    if (!*arrays[i]) return false;
  }
  prefix_ = arena->Allocate<float>(kBins + 1);
  if (!prefix_) return false;
  for (size_t k = 0; k < kBins; ++k) car_freq_[k] = static_cast<float>(k);
  memset(&ctl_, 0, sizeof(ctl_));
  ctl_.texture = 0.5f;
  ctl_.density = 0.5f;
  ctl_.size = 0.5f;
  hold_carrier_ = false;
  return true;
}

void CrossMode::Process(const Controls& c, const FloatFrame* in,
                        FloatFrame* out, size_t n) {
  ctl_ = c;
  if (c.trigger) hold_carrier_ = !hold_carrier_;
  stft_.Process(in, out, n);
}

void CrossMode::SmoothBins(const float* in, float* out, float q) {
  prefix_[0] = 0.0f;
  prefix_[1] = 0.0f;
  for (size_t k = 1; k < kBins; ++k) prefix_[k + 1] = prefix_[k] + in[k];
  for (size_t k = 1; k < kBins; ++k) {
    int32_t w = 1 + static_cast<int32_t>(static_cast<float>(k) * q);
    int32_t lo = static_cast<int32_t>(k) - w;
    int32_t hi = static_cast<int32_t>(k) + w + 1;
    if (lo < 1) lo = 1;
    if (hi > static_cast<int32_t>(kBins)) hi = kBins;
    out[k] = (prefix_[hi] - prefix_[lo]) / static_cast<float>(hi - lo);
  }
}

void CrossMode::Transform(float* spectrum[2], size_t fft_size) {
  (void)fft_size;
  const Controls c = ctl_;
  const bool freeze_mod = c.freeze;
  const bool hold_car = hold_carrier_;
  float* lre = spectrum[0];
  float* lim = spectrum[0] + kFftSize / 2;
  float* rre = spectrum[1];
  float* rim = spectrum[1] + kFftSize / 2;

  // Envelope follower speeds (per 10.7 ms frame), from TEXTURE.
  const float t = Clamp(c.texture, 0.0f, 1.0f);
  const float a_up = 1.0f - t * 0.9f;
  const float a_dn = 1.0f - (0.3f + 0.69f * t);
  const float expected = static_cast<float>(kHop) / kFftSize;
  const float to_bins = static_cast<float>(kFftSize) / kHop;

  // ---- Analysis.
  for (size_t k = 1; k < kBins; ++k) {
    if (!freeze_mod) {
      float m = sqrtf(lre[k] * lre[k] + lim[k] * lim[k]);
      float e = env_l_[k];
      env_l_[k] = e + (m > e ? a_up : a_dn) * (m - e);
    }
    float r = rre[k], i = rim[k];
    float ph = FastAtan2Cycles(-i, r);
    float dphi = WrapCycles(ph - car_phase_[k] - expected * k);
    car_phase_[k] = ph;
    if (!hold_car) {
      car_mag_[k] = sqrtf(r * r + i * i);
      car_freq_[k] = static_cast<float>(k) + dphi * to_bins;
    }
  }

  // ---- Carrier transposition (PITCH).
  const float ratio = SemitonesToRatio(Clamp(c.pitch, -48.0f, 48.0f));
  const bool direct_phase = !hold_car && fabsf(ratio - 1.0f) < 1e-3f;
  memset(out_mag_, 0, kBins * sizeof(float));
  memset(out_freq_, 0, kBins * sizeof(float));
  float* out_mag = out_mag_;
  float* out_freq = out_freq_;
  const float* car_mag = car_mag_;
  const float* car_freq = car_freq_;
  // Peak regions move as blocks, so each carrier partial keeps its lobe.
  ForEachPeakRegion(car_mag_, kBins, [&](size_t lo, size_t hi, size_t pk) {
    const float f = car_freq[pk] * ratio;
    const int32_t d = RoundToInt(static_cast<float>(pk) * ratio) -
        static_cast<int32_t>(pk);
    for (size_t k = lo; k <= hi; ++k) {
      int32_t dst = static_cast<int32_t>(k) + d;
      if (dst < 1 || dst >= static_cast<int32_t>(kBins) - 1) continue;
      out_mag[dst] += car_mag[k];
      out_freq[dst] += car_mag[k] * f;
    }
  });

  // Output phases: the carrier's own (untransposed, live), or locked to the
  // peaks of the transposed / held carrier.
  if (direct_phase) {
    for (size_t k = 1; k < kBins; ++k) syn_phase_[k] = car_phase_[k];
  } else {
    float* syn = syn_phase_;
    ForEachPeakRegion(out_mag_, kBins, [&](size_t lo, size_t hi, size_t pk) {
      float m = out_mag[pk];
      float f = m > 1e-12f ? out_freq[pk] / m : static_cast<float>(pk);
      LockPhases(lo, hi, pk, syn[pk] + f * expected, syn);
    });
  }

  // ---- Spectral envelopes (DENSITY = resolution).
  const float q = 0.01f + Clamp(c.density, 0.0f, 1.0f) * 0.25f;
  SmoothBins(env_l_, smooth_l_, q);
  SmoothBins(out_mag_, smooth_r_, q);
  float mean_r = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    float e = env_r_[k];
    float m = smooth_r_[k];
    env_r_[k] = e + (m > e ? a_up : a_dn) * (m - e);
    mean_r += env_r_[k];
  }
  const float eps = mean_r * (0.01f / kBins) + 1e-9f;

  // ---- Combine. POSITION: vocoder -> geometric morph -> L mags.
  const float shift = FastExp2((Clamp(c.size, 0.0f, 1.0f) - 0.5f) * 2.0f);
  const float inv_shift = 1.0f / shift;
  const float p = Clamp(c.position, 0.0f, 1.0f);
  for (size_t k = 1; k < kBins; ++k) {
    // Formant-shifted modulator envelope.
    float src = static_cast<float>(k) * inv_shift;
    float w_l = 0.0f;
    if (src < static_cast<float>(kBins - 1)) {
      int32_t i0 = static_cast<int32_t>(src);
      float fr = src - static_cast<float>(i0);
      if (i0 < 1) { i0 = 1; fr = 0.0f; }
      w_l = smooth_l_[i0] + fr * (smooth_l_[i0 + 1] - smooth_l_[i0]);
    }
    float car = out_mag_[k];
    float voc = w_l * car / (env_r_[k] + eps);
    float l = env_l_[k];
    float geo = sqrtf(l * car);
    float m;
    if (p < 0.5f) {
      m = voc + (p * 2.0f) * (geo - voc);
    } else {
      m = geo + (p - 0.5f) * 2.0f * (l - geo);
    }

    const float u = SafeCycles(syn_phase_[k]);
    float re = m * FastCos(u);
    float im = -m * FastSin(u);
    lre[k] = re;
    lim[k] = im;
  }
  // Mono out: same spectrum on both sides.
  memcpy(spectrum[1], spectrum[0], kFftSize * sizeof(float));
}

}  // namespace monsoon

// Mode 6 — CROSS: L/R spectral morph.
//
// Cost notes: three FFTs per hop (two analyses, one mono synthesis). With
// the carrier live and untransposed, the output reuses the carrier's own
// phases (a real gain per bin, no trig); otherwise the transposed / held
// carrier is resynthesised with one sin/cos pair per partial (phase-locked
// peak regions). atan2 runs only on the carrier's local maxima.

#include "monsoon/dsp/modes/cross.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

static const float kExpected =
    static_cast<float>(CrossMode::kHop) / CrossMode::kFftSize;
static const float kToBins =
    static_cast<float>(CrossMode::kFftSize) / CrossMode::kHop;

bool CrossMode::Init(Arena* arena) {
  if (!stft_.Init(arena, LeanStft::JOINT_MONO_OUT, this)) return false;
  car_prev_ = arena->Allocate<float>(kFftSize);
  if (!car_prev_) return false;
  float** arrays[] = {
    &env_l_, &env_r_, &car_mag_, &car_freq_, &syn_phase_,
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
  // Half-width grows with k (constant Q); tracked incrementally.
  const int32_t n = static_cast<int32_t>(kBins);
  float wf = 1.0f + q;
  for (int32_t k = 1; k < n; ++k, wf += q) {
    const int32_t w = static_cast<int32_t>(wf);
    int32_t lo = k - w;
    int32_t hi = k + w + 1;
    if (lo < 1) lo = 1;
    if (hi > n) hi = n;
    out[k] = (prefix_[hi] - prefix_[lo]) / static_cast<float>(hi - lo);
  }
}

void CrossMode::TransformJoint(float* l, float* r) {
  const Controls c = ctl_;
  const bool freeze_mod = c.freeze;
  const bool hold_car = hold_carrier_;

  // Envelope follower speeds (per 21 ms frame), from TEXTURE.
  const float t = Clamp(c.texture, 0.0f, 1.0f);
  const float a_up = 1.0f - t * 0.85f;
  const float a_dn = 1.0f - (0.45f + 0.53f * t);

  // ---- Analysis.
  for (size_t k = 1; k < kBins; ++k) {
    if (!freeze_mod) {
      const float lr = l[2 * k], li = l[2 * k + 1];
      const float m = sqrtf(lr * lr + li * li);
      const float e = env_l_[k];
      env_l_[k] = e + (m > e ? a_up : a_dn) * (m - e);
    }
    if (!hold_car) {
      const float rr = r[2 * k], ri = r[2 * k + 1];
      car_mag_[k] = sqrtf(rr * rr + ri * ri);
    }
  }
  if (!hold_car) {
    const float* cm = car_mag_;
    for (size_t k = 1; k + 1 < kBins; ++k) {
      const float m = cm[k];
      if (m > cm[k - 1] && m >= cm[k + 1]) {
        const float re = r[2 * k], im = r[2 * k + 1];
        const float pr = car_prev_[2 * k], pi = car_prev_[2 * k + 1];
        float d = FastAtan2Cycles(im * pr - re * pi, re * pr + im * pi);
        car_freq_[k] = static_cast<float>(k) +
            WrapCycles(d - kExpected * static_cast<float>(k)) * kToBins;
      } else {
        car_freq_[k] = static_cast<float>(k);
      }
    }
    memcpy(car_prev_, r, kFftSize * sizeof(float));
  }

  // ---- Carrier transposition (PITCH).
  const float ratio = SemitonesToRatio(Clamp(c.pitch, -48.0f, 48.0f));
  const bool direct_phase = !hold_car && fabsf(ratio - 1.0f) < 1e-3f;
  float* out_mag = out_mag_;
  float* out_freq = out_freq_;
  if (direct_phase) {
    memcpy(out_mag, car_mag_, kBins * sizeof(float));
  } else {
    memset(out_mag, 0, kBins * sizeof(float));
    memset(out_freq, 0, kBins * sizeof(float));
    const float* car_mag = car_mag_;
    const float* car_freq = car_freq_;
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
  }

  // ---- Spectral envelopes (DENSITY = resolution).
  const float q = 0.01f + Clamp(c.density, 0.0f, 1.0f) * 0.25f;
  SmoothBins(env_l_, smooth_l_, q);
  SmoothBins(out_mag, smooth_r_, q);
  float mean_r = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    const float e = env_r_[k];
    const float m = smooth_r_[k];
    env_r_[k] = e + (m > e ? a_up : a_dn) * (m - e);
    mean_r += env_r_[k];
  }
  const float eps = mean_r * (0.01f / kBins) + 1e-9f;

  // ---- Combine. POSITION: vocoder -> geometric morph -> L magnitudes.
  const float shift = FastExp2((Clamp(c.size, 0.0f, 1.0f) - 0.5f) * 2.0f);
  const float inv_shift = 1.0f / shift;
  const float p = Clamp(c.position, 0.0f, 1.0f);
  const bool first_half = p < 0.5f;
  const float tm = first_half ? p * 2.0f : (p - 0.5f) * 2.0f;
  float* m_out = prefix_;  // free again after the smoothing
  m_out[0] = 0.0f;
  float src = inv_shift;
  for (size_t k = 1; k < kBins; ++k, src += inv_shift) {
    float w_l = 0.0f;
    if (src < static_cast<float>(kBins - 1)) {
      int32_t i0 = static_cast<int32_t>(src);
      float fr = src - static_cast<float>(i0);
      if (i0 < 1) { i0 = 1; fr = 0.0f; }
      w_l = smooth_l_[i0] + fr * (smooth_l_[i0 + 1] - smooth_l_[i0]);
    }
    const float car = out_mag[k];
    const float l_mag = env_l_[k];
    const float geo = sqrtf(l_mag * car);
    float m;
    if (first_half) {
      const float voc = w_l * car / (env_r_[k] + eps);
      m = voc + tm * (geo - voc);
    } else {
      m = geo + tm * (l_mag - geo);
    }
    m_out[k] = m;
  }

  // ---- Output phases.
  if (direct_phase) {
    // The live carrier's own phases: a real gain per bin.
    for (size_t k = 1; k < kBins; ++k) {
      const float cm = car_mag_[k];
      const float g = cm > 1e-20f ? m_out[k] / cm : 0.0f;
      l[2 * k] = r[2 * k] * g;
      l[2 * k + 1] = r[2 * k + 1] * g;
    }
    return;
  }
  memset(l, 0, kFftSize * sizeof(float));
  float* syn = syn_phase_;
  ForEachPeakRegion(out_mag, kBins, [&](size_t lo, size_t hi, size_t pk) {
    const float mp = out_mag[pk];
    const float f = mp > 1e-12f ? out_freq[pk] / mp : static_cast<float>(pk);
    const float ph = WrapCycles(syn[pk] + f * kExpected);
    const float opposite = WrapCycles(ph + 0.5f);
    const float u = SafeCycles(ph);
    const float cs = FastCos(u);
    const float sn = FastSin(u);
    for (size_t k = lo; k <= hi; ++k) {
      const bool odd = ((k - pk) & 1) != 0;
      const float mk = odd ? -m_out[k] : m_out[k];
      l[2 * k] = mk * cs;
      l[2 * k + 1] = mk * sn;
      syn[k] = odd ? opposite : ph;
    }
  });
}

}  // namespace monsoon

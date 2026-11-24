// Small helpers shared by the FFT-based modes (spectral, cross, convolve).
//
// ShyFFT layout for an N-point real transform: x[0] = DC, x[N/2] = Nyquist
// (both real), re[k] = x[k], im[k] = x[N/2 + k] for k = 1 .. N/2-1.
// ShyFFT's imaginary part has the opposite sign of the usual e^{-jwn}
// convention, so phases here are computed as atan2(-im, re) and written
// back as im = -m * sin(phi). The forward + inverse round trip gains N, and
// both transforms use their input as scratch (they destroy it).

#ifndef MONSOON_DSP_MODES_SPECTRAL_UTIL_H_
#define MONSOON_DSP_MODES_SPECTRAL_UTIL_H_

#include "monsoon/dsp/core.h"

namespace monsoon {
namespace spectral_util {

// Round to nearest integer without libm (VCVT on the M4).
inline int32_t RoundToInt(float x) {
  return static_cast<int32_t>(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

// Wrap a phase in cycles to [-0.5, 0.5).
inline float WrapCycles(float x) {
  return x - static_cast<float>(RoundToInt(x));
}

// Phase in cycles mapped into [0, 1). Needed before FastSin/FastCos:
// core.h's FastSin computes x - floorf(x), which rounds to exactly 1.0f for
// tiny negative x and then reads one past the end of kSineTable.
inline float SafeCycles(float x) {
  float u = x - static_cast<float>(static_cast<int32_t>(x));
  if (u < 0.0f) u += 1.0f;
  if (u >= 1.0f) u -= 1.0f;
  return u;
}

// log2 with ~0.01 accuracy (bit trick + quadratic), x > 0.
inline float FastLog2(float x) {
  union { float f; uint32_t i; } u = { x };
  float e = static_cast<float>(static_cast<int32_t>((u.i >> 23) & 255) - 128);
  u.i = (u.i & 0x007FFFFF) | 0x3F800000;  // mantissa in [1, 2)
  float m = u.f;
  return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

// 2^x with ~0.1% accuracy, |x| < 126.
inline float FastExp2(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 126.0f) x = 126.0f;
  int32_t i = static_cast<int32_t>(x);
  if (x < static_cast<float>(i)) --i;  // floor
  float f = x - static_cast<float>(i);
  float p = 1.0f + f * (0.6960656f + f * (0.2245734f + f * 0.0794004f));
  union { float f; uint32_t i; } u;
  u.i = static_cast<uint32_t>(i + 127) << 23;
  return u.f * p;
}

// Splits bins 1 .. n-2 into peak regions (each local maximum owns the bins
// down to the minimum between it and its neighbouring peaks) and calls
// fn(lo, hi, peak) for each, lo <= peak <= hi. Used for peak-locked
// shifting and phase locking (Laroche & Dolson), which keeps every
// partial's window lobe coherent.
template<typename F>
inline void ForEachPeakRegion(const float* mag, size_t n, F fn) {
  const size_t last = n - 2;
  size_t lo = 1;
  size_t p = 0;
  // First peak.
  for (size_t k = 1; k <= last; ++k) {
    if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1]) { p = k; break; }
  }
  if (p == 0) return;
  while (true) {
    size_t q = 0;
    for (size_t k = p + 1; k <= last; ++k) {
      if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1]) { q = k; break; }
    }
    if (q == 0) {
      fn(lo, last, p);
      return;
    }
    size_t b = p + 1;
    for (size_t k = p + 1; k < q; ++k) if (mag[k] < mag[b]) b = k;
    if (b >= q) b = q - 1;
    fn(lo, b, p);
    lo = b + 1;
    p = q;
  }
}

// Rigid phase locking: the peak advances by its own frequency, every other
// bin in its region keeps the half-cycle-per-bin relationship of a
// (sqrt-)Hann window lobe.
inline void LockPhases(size_t lo, size_t hi, size_t p, float peak_phase,
                       float* phase) {
  for (size_t k = lo; k <= hi; ++k) {
    phase[k] = WrapCycles(peak_phase + 0.5f * static_cast<float>(
        static_cast<int32_t>(k) - static_cast<int32_t>(p)));
  }
}

}  // namespace spectral_util
}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SPECTRAL_UTIL_H_

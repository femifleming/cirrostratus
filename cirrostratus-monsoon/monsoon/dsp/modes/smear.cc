// SPECTRAL option 2 — SMEAR (see smear.h).

#include "monsoon/dsp/modes/smear.h"

#include <string.h>

namespace monsoon {

namespace {

// The Wingie's approximations (Sleepcousonique engine/scq_math.h and the
// smear in engine/scq_proc_time.inc), kept so the two sound alike.
inline float SqFloor(float x) {
  int32_t i = static_cast<int32_t>(x);
  return static_cast<float>(i - (x < static_cast<float>(i)));
}
inline float SqSin2Pi(float p) {         // sin(2 pi p), error ~0.001
  p -= SqFloor(p);
  float x = p - 0.5f;
  float ax = x < 0.0f ? -x : x;
  float y = 8.0f * x - 16.0f * x * ax;
  float ay = y < 0.0f ? -y : y;
  y = 0.225f * (y * ay - y) + y;
  return -y;
}
inline float SqAtan2Turns(float y, float x) {   // -0.5 .. 0.5
  float ax = fabsf(x), ay = fabsf(y), mx = ax > ay ? ax : ay;
  if (!(mx > 1e-30f)) return 0.0f;
  float a = (ax < ay ? ax : ay) / mx, s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0.0f) r = 3.14159274f - r;
  if (y < 0.0f) r = -r;
  return r * 0.159154943f;
}
inline float SqExp2(float x) {
  if (!(x > -60.0f)) return 0.0f;
  if (x > 24.0f) x = 24.0f;
  float fl = SqFloor(x);
  float f = x - fl;
  float p = 1.0f + f * (0.6960656f + f * (0.2244943f + f * 0.0794402f));
  union { float f; int32_t i; } u;
  u.f = p;
  u.i += static_cast<int32_t>(fl) * (1 << 23);   // (the same as << 23, defined for negatives)
  return u.f;
}

}  // namespace

bool SmearEngine::Init(Arena* arena, size_t size) {
  kSize = size;
  kHop = size / 4;
  kRing = kSize + 2 * kHop;
  kBins = kSize / 2;
  in_ = arena->Allocate<float>(kRing);
  out_ = arena->Allocate<float>(kRing);
  mag_ = arena->Allocate<float>(kBins);
  ur_ = arena->Allocate<float>(kBins);
  ui_ = arena->Allocate<float>(kBins);
  prev_ = arena->Allocate<float>(kBins);
  acc_ = arena->Allocate<float>(kBins);
  if (!in_ || !out_ || !mag_ || !ur_ || !ui_ || !prev_ || !acc_) return false;
  memset(in_, 0, kRing * sizeof(float));
  memset(out_, 0, kRing * sizeof(float));
  memset(mag_, 0, kBins * sizeof(float));
  memset(ur_, 0, kBins * sizeof(float));
  memset(ui_, 0, kBins * sizeof(float));
  memset(prev_, 0, kBins * sizeof(float));
  memset(acc_, 0, kBins * sizeof(float));
  win_ = arena->Allocate<float>(kSize);
  temp_ = arena->Allocate<float>(kSize);
  spec_ = arena->Allocate<float>(kSize);
  if (!win_ || !temp_ || !spec_) return false;
  for (size_t j = 0; j < kSize; ++j) {       // Hann
    float s = sinf(kPi * static_cast<float>(j) / kSize);
    win_[j] = s * s;
  }
  pos_ = 0;
  hop_counter_ = 0;
  hops_ready_ = 0;
  hops_done_ = 0;
  blur_ = 0.0f;
  hold_ = false;
  return true;
}

// Audio interrupt: samples in and out of the rings.
void SmearEngine::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  blur_ = Clamp(c.density, 0.0f, 1.0f);
  hold_ = c.freeze;
  float* o = out_;
  for (size_t i = 0; i < n; ++i) {
    in_[pos_] = Clamp(0.5f * (in[i].l + in[i].r), -4.0f, 4.0f);
    out[i].l = out[i].r = o[pos_];
    o[pos_] = 0.0f;
    if (++pos_ >= kRing) pos_ = 0;
    if (++hop_counter_ >= kHop) {
      hop_counter_ = 0;
      ++hops_ready_;
    }
  }
}

// The bins: analysis as the Wingie's sp_smear_bins, resynthesis phase-locked
// to the peaks (see smear.h).
void SmearEngine::Bins(float* x) {
  float* mag = mag_;
  float* ur = ur_;
  float* ui = ui_;
  float* prev = prev_;
  float* acc = acc_;
  const float blur = blur_;
  float c = 1.0f;
  if (blur > 0.001f) {   // scq_coef_time(0.015 * 2^(8.5 blur), 48000 / 128)
    const float frames = kSampleRate / static_cast<float>(kHop);
    c = 1.0f - SqExp2(-1.442695f / (0.015f * SqExp2(blur * 8.5f) * frames));
  }
  if (hold_) c = 0.0f;                         // FREEZE: the blur stops
  const float q = 0.3f * blur;
  const bool thru = c >= 0.999f;
  for (size_t k = 1; k < kBins; ++k) {
    const float re = x[2 * k], im = x[2 * k + 1];
    const float m = sqrtf(re * re + im * im);
    const float ph = SqAtan2Turns(im, re);
    float d = ph - prev[k] - static_cast<float>(k & 3) * 0.25f;
    d -= SqFloor(d + 0.5f);
    prev[k] = ph;
    mag[k] += c * (m - mag[k]);
    ur[k] += c * (m * SqSin2Pi(d + 0.25f) - ur[k]);
    ui[k] += c * (m * SqSin2Pi(d) - ui[k]);
    if (thru) acc[k] = ph;                     // no blur: as it was
  }
  if (thru) return;

  // The leaned magnitudes, and the peaks among them (scratch in temp_, free
  // between the two transforms).
  float* mo = temp_;
  uint16_t* peaks = reinterpret_cast<uint16_t*>(temp_ + kBins);
  mo[0] = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    const float lo = k > 1 ? mag[k - 1] : mag[k];
    const float hi = k < kBins - 1 ? mag[k + 1] : mag[k];
    mo[k] = mag[k] + q * (0.5f * (lo + hi) - mag[k]);
  }
  size_t n_peaks = 0;
  for (size_t k = 1; k < kBins; ++k) {
    const float lo = k > 1 ? mo[k - 1] : 0.0f;
    const float hi = k < kBins - 1 ? mo[k + 1] : 0.0f;
    if (mo[k] > lo && mo[k] >= hi && mo[k] > 1e-9f) {
      peaks[n_peaks++] = static_cast<uint16_t>(k);
    }
  }
  x[0] = 0.0f;   // (nothing to sing at DC or Nyquist)
  x[1] = 0.0f;
  if (!n_peaks) {
    for (size_t k = 1; k < kBins; ++k) x[2 * k] = x[2 * k + 1] = 0.0f;
    return;
  }
  size_t from = 1;
  for (size_t j = 0; j < n_peaks; ++j) {
    const size_t p = peaks[j];
    const size_t to = j + 1 < n_peaks ? (p + peaks[j + 1]) / 2 : kBins - 1;
    float a = acc[p] + static_cast<float>(p & 3) * 0.25f +
        SqAtan2Turns(ui[p], ur[p]);
    a -= SqFloor(a);
    const float cp = SqSin2Pi(a + 0.25f);
    const float sp = SqSin2Pi(a);
    const float a_odd = a + 0.5f - SqFloor(a + 0.5f);
    for (size_t k = from; k <= to; ++k) {
      // a Hann lobe: neighbouring bins of one partial are half a turn apart
      const bool odd = (k ^ p) & 1;
      const float g = odd ? -mo[k] : mo[k];
      x[2 * k] = g * cp;
      x[2 * k + 1] = g * sp;
      acc[k] = odd ? a_odd : a;
    }
    from = to + 1;
  }
}

// Main loop: the transforms of every hop that came in.
void SmearEngine::Prepare() {
  while (hops_done_ != hops_ready_) {
    if (hops_ready_ - hops_done_ > 2) hops_done_ = hops_ready_ - 1;
    const size_t end = (kHop * (hops_done_ + 1)) % kRing;
    ++hops_done_;
    const size_t start = (end + kRing - kSize) % kRing;
    const size_t out_start = (end + 2 * kHop) % kRing;
    // Hann in and out at 4x overlap sum to 1.5 (the inverse has the 1/N).
    const float kGain = 2.0f / 3.0f;
    for (size_t j = 0; j < kSize; ++j) {
      size_t a = start + j;
      if (a >= kRing) a -= kRing;
      temp_[j] = in_[a] * win_[j];
    }
    monsoon_cmsis_rfft(temp_, spec_, kSize, 0);
    Bins(spec_);
    monsoon_cmsis_rfft(spec_, temp_, kSize, 1);
    for (size_t j = 0; j < kSize; ++j) {
      size_t a = out_start + j;
      if (a >= kRing) a -= kRing;
      out_[a] += temp_[j] * win_[j] * kGain;
    }
  }
}

}  // namespace monsoon

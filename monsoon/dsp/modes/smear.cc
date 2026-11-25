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

bool SmearEngine::Init(Arena* arena) {
  in_ = arena->Allocate<float>(kRing);
  out_ = arena->Allocate<float>(kRing);
  mag_ = arena->Allocate<float>(kBins);
  dev_ = arena->Allocate<float>(kBins);
  prev_ = arena->Allocate<float>(kBins);
  acc_ = arena->Allocate<float>(kBins);
  if (!in_ || !out_ || !mag_ || !dev_ || !prev_ || !acc_) return false;
  memset(in_, 0, kRing * sizeof(float));
  memset(out_, 0, kRing * sizeof(float));
  memset(mag_, 0, kBins * sizeof(float));
  memset(dev_, 0, kBins * sizeof(float));
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

// The bins, as the Wingie's sp_smear_bins does them.
void SmearEngine::Bins(float* x) {
  float* mag = mag_;
  float* dev = dev_;
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
    float re = x[2 * k], im = x[2 * k + 1];
    float m = sqrtf(re * re + im * im);   // (the Wingie: two Newton steps, the same to float precision)
    float ph = SqAtan2Turns(im, re);
    float d = ph - prev[k] - static_cast<float>(k & 3) * 0.25f;
    d -= SqFloor(d + 0.5f);
    prev[k] = ph;
    mag[k] += c * (m - mag[k]);
    dev[k] += c * (d - dev[k]);
    if (thru) { acc[k] = ph; continue; }      // no blur: as it was
    float lo = k > 1 ? mag[k - 1] : mag[k];
    float hi = k < kBins - 1 ? mag[k + 1] : mag[k];
    float mo = mag[k] + q * (0.5f * (lo + hi) - mag[k]);
    float a = acc[k] + static_cast<float>(k & 3) * 0.25f + dev[k];
    a -= SqFloor(a);
    acc[k] = a;
    x[2 * k] = mo * SqSin2Pi(a + 0.25f);
    x[2 * k + 1] = mo * SqSin2Pi(a);
  }
  if (!thru) {   // (nothing to sing at DC or Nyquist; untouched without blur)
    x[0] = 0.0f;
    x[1] = 0.0f;
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

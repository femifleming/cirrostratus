// Mode 10 - GENDY (see gendy.h and docs/DESIGN.md).

#include "monsoon/dsp/modes/gendy.h"

namespace monsoon {

static const uint32_t kGendyMagic = 0x59444E47;  // "GNDY"
static const int kFirCenter = 23;                // 47-tap half-band

// Natural log without libm: exponent from the float bits + atanh series.
static inline float FastLn(float x) {
  union { float f; uint32_t i; } u = { x };
  float e = static_cast<float>(static_cast<int32_t>((u.i >> 23) & 0xff) - 127);
  u.i = (u.i & 0x007fffff) | 0x3f800000;  // mantissa in [1, 2)
  float m = u.f;
  // ln(m) = 2 atanh((m-1)/(m+1)), series to t^7 (|t| <= 1/3, err < 1e-5).
  float t = (m - 1.0f) / (m + 1.0f);
  float t2 = t * t;
  float ln_m = 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * 0.14285714f)));
  return e * 0.69314718f + ln_m;
}

static inline float Reflect(float x, float lo, float hi) {
  for (int k = 0; k < 4; ++k) {
    if (x > hi) x = 2.0f * hi - x;
    else if (x < lo) x = 2.0f * lo - x;
    else return x;
  }
  return Clamp(x, lo, hi);
}

bool GendyMode::Init(Arena* arena) {
  state_ = arena->Allocate<State>(1);
  history_ = arena->Allocate<float>(kHistorySize);
  fir_ring_[0] = arena->Allocate<float>(kFirRing);
  fir_ring_[1] = arena->Allocate<float>(kFirRing);
  if (!state_ || !history_ || !fir_ring_[0] || !fir_ring_[1]) return false;

  rng_.Seed(0x6E9D1);
  state_->magic = kGendyMagic;
  state_->num_breakpoints = 12;
  for (int v = 0; v < 2; ++v) {
    for (int i = 0; i < kMaxBreakpoints; ++i) {
      // Start from a sine-like shape (slightly different per side).
      state_->amplitude[v][i] =
          0.8f * FastSin(static_cast<float>(i) / 12.0f + 0.02f * v);
      state_->duration[v][i] = 1.0f;
    }
    voice_[v].index = 0;
    voice_[v].position = 0.0f;
    voice_[v].length = 10.0f;
    voice_[v].from = 0.0f;
    voice_[v].to = state_->amplitude[v][0];
    voice_[v].fir_write = 0;
    dc_[v].Init();
  }
  history_write_ = 0;

  // Blackman-windowed half-band low-pass (cutoff fs/4 at 96 kHz).
  // Odd-offset taps only (even ones are zero), normalised to unity DC gain.
  float sum = 0.0f;
  for (int k = 0; k < 12; ++k) {
    int off = 2 * k + 1;
    float x = 0.5f * kPi * off;
    float sinc = sinf(x) / x;
    float n = static_cast<float>(kFirCenter + off);
    float w = 0.42f - 0.5f * cosf(kTwoPi * n / 46.0f) +
        0.08f * cosf(2.0f * kTwoPi * n / 46.0f);
    halfband_[k] = 0.5f * sinc * w;
    sum += 2.0f * halfband_[k];
  }
  for (int k = 0; k < 12; ++k) halfband_[k] *= 0.5f / sum;

  mean_length_ = 20.0f;
  amp_step_ = dur_step_ = 0.0f;
  barrier_ = 0.1f;
  dist_zone_ = 0.0f;
  period_1x_ = 100.0f;
  frozen_ = false;
  return true;
}

float GendyMode::Distribution(float u) const {
  u = Clamp(u, 0.004f, 0.996f);
  int zone = static_cast<int>(dist_zone_);
  if (zone > 2) zone = 2;
  float frac = dist_zone_ - zone;
  float v[2];
  for (int k = 0; k < 2; ++k) {
    switch (zone + k) {
      case 0: {  // Cauchy
        float x = 0.5f * (u - 0.5f);
        v[k] = Clamp(0.08f * FastSin(x) / FastCos(x), -1.0f, 1.0f);
        break;
      }
      case 1:  // logistic
        v[k] = 0.18f * FastLn(u / (1.0f - u));
        break;
      case 2: {  // hyperbolic cosine
        float x = 0.25f * u;
        v[k] = Clamp(0.25f * FastLn(FastSin(x) / FastCos(x)), -1.0f, 1.0f);
        break;
      }
      default:  // arcsine
        v[k] = 0.9f * FastSin(0.5f * (u - 0.5f));
        break;
    }
  }
  return v[0] + (v[1] - v[0]) * frac;
}

void GendyMode::Step(int voice, int index) {
  float& a = state_->amplitude[voice][index];
  float& d = state_->duration[voice][index];
  a = Reflect(a + amp_step_ * Distribution(rng_.Float()), -1.0f, 1.0f);
  d = Reflect(d + dur_step_ * Distribution(rng_.Float()),
              1.0f - barrier_, 1.0f + barrier_);
}

float GendyMode::Render96k(int v) {
  Voice& vo = voice_[v];
  float y = vo.from + (vo.to - vo.from) * (vo.position / vo.length);
  vo.position += 1.0f;
  if (vo.position >= vo.length) {
    vo.position -= vo.length;
    const int k = static_cast<int>(state_->num_breakpoints);
    vo.index = vo.index + 1 >= k ? 0 : vo.index + 1;
    int next = vo.index + 1 >= k ? 0 : vo.index + 1;
    if (!frozen_) Step(v, next);
    vo.from = vo.to;
    vo.to = state_->amplitude[v][next];
    float len = state_->duration[v][vo.index] * mean_length_;
    vo.length = len < 1.0f ? 1.0f : len;
    if (vo.position >= vo.length) vo.position = 0.0f;
  }
  return y;
}

void GendyMode::Reseed() {
  // The last period of the input becomes the new breakpoint shape.
  size_t period = static_cast<size_t>(period_1x_);
  if (period < 8) period = 8;
  if (period > kHistorySize - 1) period = kHistorySize - 1;
  size_t start = (history_write_ + kHistorySize - period) % kHistorySize;
  float peak = 0.0f;
  for (size_t i = 0; i < period; ++i) {
    float a = fabsf(history_[(start + i) % kHistorySize]);
    if (a > peak) peak = a;
  }
  if (peak < 1e-4f) return;
  const int k = static_cast<int>(state_->num_breakpoints);
  for (int v = 0; v < 2; ++v) {
    size_t offset = v * period / 7;  // the right voice starts elsewhere
    for (int i = 0; i < k; ++i) {
      size_t j = (offset + i * period / k) % period;
      state_->amplitude[v][i] = history_[(start + j) % kHistorySize] / peak;
      state_->duration[v][i] = 1.0f;
    }
  }
}

void GendyMode::Process(const Controls& c, const FloatFrame* in,
                        FloatFrame* out, size_t n) {
  frozen_ = c.freeze;

  // Block-rate parameters.
  const float f = Clamp(65.41f * SemitonesToRatio(c.pitch), 8.0f, 3000.0f);
  period_1x_ = kSampleRate / f;
  int k = kMinBreakpoints +
      static_cast<int>(Clamp(c.density, 0.0f, 1.0f) *
                       (kMaxBreakpoints - kMinBreakpoints) + 0.5f);
  // Keep at least ~2 samples (96 kHz) per segment.
  int k_max = static_cast<int>(2.0f * kSampleRate / (f * 2.0f));
  if (k > k_max) k = k_max;
  if (k < kMinBreakpoints) k = kMinBreakpoints;
  if (static_cast<uint32_t>(k) != state_->num_breakpoints) {
    state_->num_breakpoints = k;
    for (int v = 0; v < 2; ++v) {
      if (voice_[v].index >= k) voice_[v].index = 0;
    }
  }
  mean_length_ = 2.0f * kSampleRate / (f * k);
  const float t = Clamp(c.texture, 0.0f, 1.0f);
  amp_step_ = 0.3f * t * t;
  dur_step_ = 0.35f * t * t;
  const float sz = Clamp(c.size, 0.0f, 1.0f);
  barrier_ = 0.02f + 0.9f * sz * sz;
  dist_zone_ = Clamp(c.position, 0.0f, 1.0f) * 3.0f;

  for (size_t s = 0; s < n; ++s) {
    history_[history_write_] = 0.5f * (in[s].l + in[s].r);
    history_write_ = (history_write_ + 1) % kHistorySize;
  }
  if (c.trigger) Reseed();

  for (size_t s = 0; s < n; ++s) {
    float y[2];
    for (int v = 0; v < 2; ++v) {
      float* ring = fir_ring_[v];
      size_t& w = voice_[v].fir_write;
      ring[w] = Render96k(v);
      w = (w + 1) & (kFirRing - 1);
      ring[w] = Render96k(v);
      w = (w + 1) & (kFirRing - 1);
      // Newest sample is at w - 1; centre tap 23 samples earlier.
      size_t center = (w + kFirRing - 1 - kFirCenter) & (kFirRing - 1);
      float acc = 0.5f * ring[center];
      for (int j = 0; j < 12; ++j) {
        size_t off = 2 * j + 1;
        acc += halfband_[j] * (ring[(center + off) & (kFirRing - 1)] +
                               ring[(center + kFirRing - off) & (kFirRing - 1)]);
      }
      y[v] = dc_[v].Process(acc * 0.45f);
    }
    out[s].l = y[0];
    out[s].r = y[1];
  }
}

size_t GendyMode::GetPersistentData(PersistentChunk* chunks) {
  chunks[0].data = state_;
  chunks[0].size = sizeof(State);
  return 1;
}

bool GendyMode::LoadPersistentData(const uint8_t* data, size_t size) {
  if (size < sizeof(State)) return false;
  State s;
  memcpy(&s, data, sizeof(State));
  if (s.magic != kGendyMagic || s.num_breakpoints < kMinBreakpoints ||
      s.num_breakpoints > kMaxBreakpoints) {
    return false;
  }
  for (int v = 0; v < 2; ++v) {
    for (int i = 0; i < kMaxBreakpoints; ++i) {
      if (!(fabsf(s.amplitude[v][i]) <= 1.0f) ||
          !(s.duration[v][i] > 0.0f && s.duration[v][i] < 4.0f)) {
        return false;
      }
    }
  }
  *state_ = s;
  for (int v = 0; v < 2; ++v) {
    voice_[v].index = 0;
    voice_[v].position = 0.0f;
  }
  return true;
}

}  // namespace monsoon

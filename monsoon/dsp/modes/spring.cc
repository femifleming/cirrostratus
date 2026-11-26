// Mode 1 - SPRING (see spring.h).

#include "monsoon/dsp/modes/spring.h"

#include <string.h>

namespace monsoon {

// The three springs are a little different, as in a real tank.
static const float kLength[SpringMode::kSprings] = { 1.0f, 1.137f, 0.871f };
static const float kSpread[SpringMode::kSprings] = { 1.0f, 0.93f, 1.06f };

bool SpringMode::Init(Arena* arena) {
  for (int s = 0; s < kSprings; ++s) {
    Spring& sp = spring_[s];
    sp.line = arena->Allocate<float>(kLineSize);
    if (!sp.line) return false;
    memset(sp.line, 0, kLineSize * sizeof(float));
    memset(sp.v, 0, sizeof(sp.v));
    sp.write = 0;
    sp.lp1 = sp.lp2 = sp.hp = 0.0f;
    trip_[s] = 0.0f;
  }
  a_ = -0.475f;
  in_hp_ = 0.0f;
  knock_ = 0.0f;
  rng_ = 0x5eed5eedu;
  k_ = 0;
  frozen_ = false;
  return true;
}

// One sample through one spring: read the trip, disperse it, band-limit it,
// and write the input plus the decayed echo back. Returns the echo.
inline float SpringMode::Run(Spring* s, float x, float trip, float g,
                             float lp_coef, float a, int k) {
  // the trip (fractional: SIZE and PITCH glide)
  float rd = static_cast<float>(s->write) - trip;
  if (rd < 0.0f) rd += static_cast<float>(kLineSize);
  size_t i0 = static_cast<size_t>(rd);
  const float f = rd - static_cast<float>(i0);
  size_t i1 = i0 + 1;
  if (i1 >= kLineSize) i1 = 0;
  float y = s->line[i0] + (s->line[i1] - s->line[i0]) * f;
  // the dispersion: 32 allpasses on z^-4 (one multiply-add pair each)
  for (int m = 0; m < kStages; ++m) {
    const float old = s->v[m][k];
    const float v = y - a * old;
    s->v[m][k] = v;
    y = a * v + old;
  }
  // the band limit, and a high-pass at ~40 Hz so nothing builds up
  s->lp1 += lp_coef * (y - s->lp1);
  s->lp2 += lp_coef * (s->lp1 - s->lp2);
  s->hp += 0.005f * (s->lp2 - s->hp);
  const float echo = s->lp2 - s->hp;
  s->line[s->write] = SoftClip(x + g * echo);
  if (++s->write >= kLineSize) s->write = 0;
  return echo;
}

void SpringMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  const int springs = c.option == 0 ? 2 : (c.option == 1 ? 3 : 1);

  // SIZE and PITCH: the trip time, glided per block.
  const float base = (0.025f + 0.055f * Clamp(c.size, 0.0f, 1.0f)) *
      kSampleRate;
  const float tension = SemitonesToRatio(-Clamp(c.pitch, -48.0f, 48.0f));
  float target[kSprings];
  for (int s = 0; s < kSprings; ++s) {
    target[s] = Clamp(base * kLength[s] * tension, 0.005f * kSampleRate,
                      static_cast<float>(kLineSize - 2));
    if (trip_[s] <= 0.0f) trip_[s] = target[s];
  }
  const float glide = 0.08f;   // per block: ~8 ms
  // TEXTURE: dispersion. A negative coefficient delays the lows most (at
  // a = -0.75: ~28 samples per allpass at DC, under 1 at 6 kHz; 18 ms of
  // spread per trip): every echo a falling chirp, as on a real spring.
  const float a_target = -(0.2f + 0.55f * Clamp(c.texture, 0.0f, 1.0f));
  a_ += (a_target - a_) * glide;
  // DENSITY: T60 0.4 -> 8 s; the loop gain per trip.
  const float t60 = 0.4f * powf(20.0f, Clamp(c.density, 0.0f, 1.0f));
  // POSITION: band limit 1.2 -> 8 kHz (a one-pole pair).
  const float fc = 1200.0f * powf(6.667f, Clamp(c.position, 0.0f, 1.0f));
  float lp_coef = 1.0f - expf(-kTwoPi * fc / kSampleRate);
  if (frozen_) lp_coef = 1.0f;
  // RVB: dwell.
  const float rvb = Clamp(c.rvb, 0.0f, 1.0f);
  const float dwell = 1.0f + 5.0f * rvb * rvb;
  const float makeup = 1.0f / (1.0f + 1.5f * rvb);
  // BAL: width.
  const float width = 2.0f * Clamp(c.bal, 0.0f, 1.0f);   // 0 mono, 1, 2
  if (c.trigger) knock_ = 1.0f;

  float g[kSprings];
  float inc[kSprings];
  for (int s = 0; s < kSprings; ++s) {
    const float next = trip_[s] + (target[s] - trip_[s]) * glide;
    inc[s] = (next - trip_[s]) / static_cast<float>(n);
    // gain per trip for the T60, at this block's trip time
    g[s] = frozen_ ? 1.0f : powf(10.0f, -3.0f * next / (t60 * kSampleRate));
  }
  const float a = a_;
  const float in_gain = frozen_ ? 0.0f : 1.0f;
  const float knock_decay = 1.0f - 1.0f / (0.008f * kSampleRate);

  for (size_t i = 0; i < n; ++i) {
    // the driver: mono, high-passed at ~120 Hz (a tank has no bass),
    // driven by the dwell into a soft clip
    float x = 0.5f * (in[i].l + in[i].r) * in_gain;
    in_hp_ += 0.0156f * (x - in_hp_);
    x = SoftClip((x - in_hp_) * dwell) * makeup;
    if (knock_ > 1e-4f) {    // TRIG: a short noise knock
      rng_ = rng_ * 1664525u + 1013904223u;
      x += knock_ * static_cast<float>(static_cast<int32_t>(rng_)) *
          4.6566e-10f * 2.5f;
      knock_ *= knock_decay;
    }
    float e[kSprings] = { 0.0f, 0.0f, 0.0f };
    for (int s = 0; s < springs; ++s) {
      trip_[s] += inc[s];
      e[s] = Run(&spring_[s], x * kSpread[s], trip_[s], g[s], lp_coef, a, k_);
    }
    if (++k_ >= kStretch) k_ = 0;
    float l, r;
    if (springs == 1) {
      l = r = e[0];
    } else {
      l = e[0] + 0.7f * e[2];
      r = e[1] + 0.7f * e[2];
    }
    // width: mid/side
    const float mid = 0.5f * (l + r);
    const float side = 0.5f * (l - r) * width;
    out[i].l = 0.8f * (mid + side);
    out[i].r = 0.8f * (mid - side);
  }
  for (int s = springs; s < kSprings; ++s) trip_[s] = target[s];
}

}  // namespace monsoon

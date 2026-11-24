// Mode 17: LEITNER (see leitner.h).

#include "monsoon/dsp/modes/leitner.h"

namespace monsoon {

namespace {

const float kSpeedOfSound = 343.0f;
const float kMaxItd = 0.00066f * kSampleRate;   // ~32 samples
const float kMaxDoppler = 0.030f * kSampleRate; // 30 ms
const float kMinDelay = 3.0f;
const float kMaxSlope = 16.0f;                  // samples per block

inline float SoftLimit(float x, float limit) {
  return x / (1.0f + x / limit);
}

}  // namespace

bool LeitnerMode::Init(Arena* arena) {
  line_ = arena->Allocate<float>(kLineSize);
  if (!line_) return false;
  write_ = 0;
  phase_ = 0.0f;
  path_ = 0;
  x_ = 1.0f;
  y_ = 0.0f;
  flight_from_[0] = flight_to_[0] = 2.0f;
  flight_from_[1] = flight_to_[1] = 0.0f;
  for (int i = 0; i < kParams; ++i) value_[i] = 0.0f;
  value_[0] = value_[1] = kMinDelay;
  for (int k = 0; k < kReflections; ++k) value_[4 + 3 * k] = kMinDelay;
  lp_coef_[0] = lp_coef_[1] = 1.0f;
  lp_[0] = lp_[1] = 0.0f;
  refl_lp_[0] = refl_lp_[1] = 0.0f;
  rng_.Seed(0x1E17);
  frozen_ = false;
  return true;
}

void LeitnerMode::PathPoint(float phase, float radius, float centre,
                            float* x, float* y) {
  float u, v;
  const float R = radius;
  switch (path_) {
    case 0:  // circle, offset so the source sweeps close and far
      u = 0.35f * R + R * FastCos(phase);
      v = R * FastSin(phase);
      break;
    case 1:  // figure-8 around a point in front of the centre direction
      u = 0.5f * R + 0.9f * R * FastCos(phase);
      v = 0.6f * R * FastSin(2.0f * phase);
      break;
    case 2:  // fly-by: back and forth along a line, closest at 0.5 R
      u = 0.5f * R;
      v = 3.0f * R * FastSin(phase);
      break;
    case 3: {  // spiral: four turns while breathing in and out
      float rho = R * (0.2f + 0.8f * (0.5f + 0.5f * FastCos(phase)));
      u = rho * FastCos(4.0f * phase);
      v = rho * FastSin(4.0f * phase);
      break;
    }
    default: {  // random flight between random points (world coords)
      float t = phase * phase * (3.0f - 2.0f * phase);
      *x = Crossfade(flight_from_[0], flight_to_[0], t);
      *y = Crossfade(flight_from_[1], flight_to_[1], t);
      return;
    }
  }
  const float cs = FastCos(centre), sn = FastSin(centre);
  *x = u * cs - v * sn;
  *y = u * sn + v * cs;
}

inline float LeitnerMode::Tap(float delay) const {
  float pos = static_cast<float>(write_) - delay + kLineSize;
  int32_t i = static_cast<int32_t>(pos);
  float t = pos - static_cast<float>(i);
  const size_t m = kLineSize - 1;
  return Hermite(line_[(i - 1) & m], line_[i & m], line_[(i + 1) & m],
                 line_[(i + 2) & m], t);
}

void LeitnerMode::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  if (c.trigger) path_ = (path_ + 1) % kPaths;
  frozen_ = c.freeze;

  const float radius = 1.0f + 9.0f * Clamp(c.size, 0.0f, 1.0f);
  const float centre = Clamp(c.position, 0.0f, 1.0f) - 0.5f;  // cycles
  const float speed = 0.02f * powf(200.0f, Clamp(c.density, 0.0f, 1.0f));

  // Advance along the path.
  if (!frozen_) {
    phase_ += speed * static_cast<float>(n) / kSampleRate;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
      // New random-flight leg.
      flight_from_[0] = flight_to_[0];
      flight_from_[1] = flight_to_[1];
      float r = radius * (0.3f + 0.7f * sqrtf(rng_.Float()));
      float a = centre + (rng_.Float() - 0.5f) * 0.6f;
      flight_to_[0] = r * FastCos(a);
      flight_to_[1] = r * FastSin(a);
    }
  }
  PathPoint(phase_, radius, centre, &x_, &y_);

  // --- Direct sound.
  const float dist_raw = sqrtf(x_ * x_ + y_ * y_);
  const float dist = dist_raw > 0.3f ? dist_raw : 0.3f;
  const float sin_az = dist_raw > 1e-4f ? y_ / dist_raw : 0.0f;  // + = left
  const float cos_az = dist_raw > 1e-4f ? x_ / dist_raw : 1.0f;  // + = front
  const float p = Clamp(c.pitch / 48.0f, -1.0f, 1.0f);
  const float doppler = p < 0.0f ? 1.0f + p : 1.0f + 2.0f * p;
  const float prop = SoftLimit(doppler * dist / kSpeedOfSound * kSampleRate,
                               kMaxDoppler);
  const float itd = kMaxItd * sin_az;
  float target[kParams];
  target[0] = kMinDelay + prop + (itd < 0.0f ? -itd : 0.0f);  // left ear
  target[1] = kMinDelay + prop + (itd > 0.0f ? itd : 0.0f);   // right ear
  const float dist_gain = 2.0f / (1.0f + dist);
  const float shadow = fabsf(sin_az);
  const float near_gain = 1.0f + 0.15f * shadow;
  const float far_gain = 1.0f - 0.45f * shadow;
  target[2] = dist_gain * (sin_az >= 0.0f ? near_gain : far_gain);
  target[3] = dist_gain * (sin_az >= 0.0f ? far_gain : near_gain);

  // Ear filters: head shadow on the far ear, darker behind, air absorption.
  const float air = 20000.0f / (1.0f + dist / 6.0f);
  const float rear = cos_az < 0.0f ? 1.0f + 0.35f * cos_az : 1.0f;
  for (int ear = 0; ear < 2; ++ear) {
    bool far_ear = ear == 0 ? sin_az < 0.0f : sin_az > 0.0f;
    float f = far_ear ? 20000.0f * (1.0f - 0.85f * shadow) : 20000.0f;
    f = (f < air ? f : air) * rear;
    lp_coef_[ear] = 1.0f - expf(-kTwoPi * Clamp(f, 200.0f, 20000.0f) / kSampleRate);
  }

  // --- Early reflections: image sources in a square room.
  const float texture = Clamp(c.texture, 0.0f, 1.0f);
  const float wall = 2.0f + 10.0f * texture;
  const float refl_amount = texture * 0.7f;
  const float images[kReflections][2] = {
    { 2.0f * wall - x_, y_ }, { -2.0f * wall - x_, y_ },
    { x_, 2.0f * wall - y_ }, { x_, -2.0f * wall - y_ },
  };
  for (int k = 0; k < kReflections; ++k) {
    float ix = images[k][0], iy = images[k][1];
    float di = sqrtf(ix * ix + iy * iy);
    float extra = (di - dist) / kSpeedOfSound * kSampleRate;
    target[4 + 3 * k] = kMinDelay + prop + (extra > 0.0f ? extra : 0.0f);
    float g = refl_amount * 2.0f / (1.0f + di);
    float s = iy / (di + 1e-6f);
    target[5 + 3 * k] = g * sqrtf(0.5f * (1.0f + s));
    target[6 + 3 * k] = g * sqrtf(0.5f * (1.0f - s));
  }

  // Per-sample ramps; delays are slew-limited so path jumps swoop
  // instead of clicking.
  float inc[kParams];
  const float inv_n = 1.0f / static_cast<float>(n);
  for (int i = 0; i < kParams; ++i) {
    float t = target[i];
    bool is_delay = i < 2 || (i >= 4 && (i - 4) % 3 == 0);
    if (is_delay) {
      float delta = Clamp(t - value_[i], -kMaxSlope, kMaxSlope);
      t = value_[i] + delta;
      t = Clamp(t, kMinDelay, static_cast<float>(kLineSize - 8));
    }
    inc[i] = (t - value_[i]) * inv_n;
  }
  const float refl_coef = 1.0f - expf(-kTwoPi * 5000.0f / kSampleRate);

  for (size_t s = 0; s < n; ++s) {
    line_[write_] = 0.5f * (in[s].l + in[s].r);
    for (int i = 0; i < kParams; ++i) value_[i] += inc[i];

    float l = Tap(value_[0]) * value_[2];
    float r = Tap(value_[1]) * value_[3];
    lp_[0] += lp_coef_[0] * (l - lp_[0]);
    lp_[1] += lp_coef_[1] * (r - lp_[1]);

    float rl = 0.0f, rr = 0.0f;
    if (refl_amount > 0.0f) {
      for (int k = 0; k < kReflections; ++k) {
        float v = Tap(value_[4 + 3 * k]);
        rl += v * value_[5 + 3 * k];
        rr += v * value_[6 + 3 * k];
      }
    }
    refl_lp_[0] += refl_coef * (rl - refl_lp_[0]);
    refl_lp_[1] += refl_coef * (rr - refl_lp_[1]);

    out[s].l = lp_[0] + refl_lp_[0];
    out[s].r = lp_[1] + refl_lp_[1];
    write_ = (write_ + 1) & (kLineSize - 1);
  }
}

}  // namespace monsoon

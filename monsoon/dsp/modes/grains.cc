// Mode 1: GRAINS - trajectory grains (see grains.h, docs/DESIGN.md).

#include "monsoon/dsp/modes/grains.h"

namespace monsoon {

static const size_t kMaxBufferBytes = 118 * 1024;

bool GrainsMode::Init(Arena* arena) {
  grains_ = arena->Allocate<Grain>(kMaxGrains);
  if (!grains_) return false;
  const int channels = init_option_ >= 4 ? 1 : 2;
  if (!buffer_.Init(arena, kMaxBufferBytes, channels)) return false;
  for (int i = 0; i < kMaxGrains; ++i) grains_[i].active = false;
  rng_.Seed(0x6a09e667u);
  shape_ = SHAPE_CIRCLE;
  theta_ = 0.0f;
  walk_x_ = walk_y_ = 0.0f;
  walk_vx_ = walk_vy_ = 0.0f;
  spawn_acc_ = 0.0f;
  next_interval_ = 0.0f;
  frozen_ = false;
  previous_trigger_ = false;
  return true;
}

int GrainsMode::active_grains() const {
  int n = 0;
  for (int i = 0; i < kMaxGrains; ++i) n += grains_[i].active ? 1 : 0;
  return n;
}

float GrainsMode::Quantize(float semitones, int scale) {
  if (scale == 3) return semitones;
  if (scale == 0) return floorf(semitones + 0.5f);
  static const float kPentatonic[] = { 0, 3, 5, 7, 10, 12 };
  static const float kFifths[] = { 0, 7, 12 };
  const float* set = scale == 1 ? kPentatonic : kFifths;
  const int count = scale == 1 ? 6 : 3;
  float octave = floorf(semitones / 12.0f);
  float r = semitones - octave * 12.0f;
  float best = set[0];
  for (int i = 1; i < count; ++i) {
    if (fabsf(set[i] - r) < fabsf(best - r)) best = set[i];
  }
  return octave * 12.0f + best;
}

void GrainsMode::TrajectoryPoint(float* x, float* y) {
  const float t = theta_;
  switch (shape_) {
    case SHAPE_CIRCLE:
      *x = FastCos(t);
      *y = FastSin(t);
      break;
    case SHAPE_LINE: {
      float f = t - floorf(t);
      *x = 4.0f * fabsf(f - 0.5f) - 1.0f;
      *y = 0.0f;
      break;
    }
    case SHAPE_FIGURE_8:
      *x = FastSin(t);
      *y = FastSin(2.0f * t);
      break;
    case SHAPE_SPIRAL: {
      float q = t * 0.25f;
      float r = q - floorf(q);
      *x = r * FastCos(t);
      *y = r * FastSin(t);
      break;
    }
    default:
      *x = walk_x_;
      *y = walk_y_;
      break;
  }
}

inline float GrainsMode::Window(float u, float texture) const {
  float hann = 0.5f - 0.5f * FastCos(u);
  if (texture < 0.5f) {
    float perc = u < 0.03f ? u * (1.0f / 0.03f) : 1.0f - (u - 0.03f) * (1.0f / 0.97f);
    if (u >= 0.03f) perc = perc * perc * perc;
    return Crossfade(perc, hann, texture * 2.0f);
  }
  float tukey = 1.0f;
  if (u < 0.15f) tukey = 0.5f - 0.5f * FastCos(u * (1.0f / 0.3f));
  else if (u > 0.85f) tukey = 0.5f - 0.5f * FastCos((1.0f - u) * (1.0f / 0.3f));
  return Crossfade(hann, tukey, texture * 2.0f - 1.0f);
}

void GrainsMode::Spawn(const Controls& c, float rate, float length) {
  Grain* g = NULL;
  for (int i = 0; i < kMaxGrains; ++i) {
    if (!grains_[i].active) { g = &grains_[i]; break; }
  }
  if (!g) return;

  float semitones = Quantize(c.pitch, c.option & 3);
  if (c.texture > 0.75f) {
    semitones += rng_.Bipolar() * (c.texture - 0.75f) * 4.0f;
  }
  const float ratio = SemitonesToRatio(semitones);
  const float len = length * kSampleRate;
  const float travel = len * ratio;
  const float filled = static_cast<float>(buffer_.filled());

  // BAL: left of noon scatters grains around the path (spray), right of
  // noon plays a growing share of them backwards. No random numbers are
  // drawn at noon, so the v1.0 cloud is reproduced exactly.
  const float bal = KnobBipolar(c.bal);
  const bool reverse = bal > 0.0f && rng_.Float() < bal;

  // Range of delays (behind the write head) the whole grain can be read
  // from. A reversed grain starts at the delay and reads away from the
  // write head, so it only needs room behind it.
  float d_lo, d_hi;
  if (frozen_) {
    d_lo = reverse ? 4.0f : travel + 4.0f;
    d_hi = filled - 8.0f - (reverse ? travel : 0.0f);
  } else if (reverse) {
    d_lo = 96.0f;
    d_hi = filled - 96.0f - travel - len;
  } else {
    d_lo = (ratio > 1.0f ? len * (ratio - 1.0f) : 0.0f) + 96.0f;
    d_hi = filled - 96.0f - (ratio < 1.0f ? len * (1.0f - ratio) : 0.0f);
  }
  if (d_hi <= d_lo + 1.0f) return;
  const float span = d_hi - d_lo;

  const float extent = c.size < 0.08f ? 0.0f : (c.size - 0.08f) / 0.92f;
  float x = 0.0f, y = 0.0f;
  if (extent > 0.0f) TrajectoryPoint(&x, &y);
  float d = d_lo + c.position * span + x * extent * 0.25f * span;
  // Small jitter so dense static clouds don't comb-filter.
  float jitter = len * 0.25f;
  if (jitter > 2400.0f) jitter = 2400.0f;
  d += rng_.Bipolar() * jitter;
  if (bal < 0.0f) d -= rng_.Bipolar() * bal * 0.5f * span;  // spray
  // Reflect into range.
  if (d < d_lo) d = d_lo + (d_lo - d);
  if (d > d_hi) d = d_hi - (d - d_hi);
  d = Clamp(d, d_lo, d_hi);

  // Pan follows the path's y coordinate (the v1.0 width at noon).
  float pan = Clamp((y + 0.25f * rng_.Bipolar()) * 0.5f, -1.0f, 1.0f);
  float gl, gr;
  if (buffer_.channels() == 1) {
    float a = (pan + 1.0f) * 0.125f;  // 0..0.25 cycles
    gl = FastCos(a) * 1.41421356f;
    gr = FastSin(a) * 1.41421356f;
  } else {
    gl = pan > 0.0f ? 1.0f - pan : 1.0f;
    gr = pan < 0.0f ? 1.0f + pan : 1.0f;
  }
  float overlap = rate * length;
  float norm = 1.0f / sqrtf(overlap * 0.6f > 1.0f ? overlap * 0.6f : 1.0f);

  g->position = static_cast<float>(buffer_.write_head()) - d;
  g->increment = reverse ? -ratio : ratio;
  g->phase = 0.0f;
  g->phase_inc = 1.0f / len;
  g->gain_l = gl * norm;
  g->gain_r = gr * norm;
  g->texture = c.texture;
  g->active = true;
}

void GrainsMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  if (!frozen_) {
    for (size_t i = 0; i + kBlockSize <= n; i += kBlockSize) {
      buffer_.WriteBlock(in + i);
    }
  }
  if (c.trigger && !previous_trigger_) {
    shape_ = static_cast<Shape>((shape_ + 1) % SHAPE_LAST);
  }
  previous_trigger_ = c.trigger;

  // Trajectory motion.
  const float dt = static_cast<float>(n) / kSampleRate;
  const float extent = c.size < 0.08f ? 0.0f : (c.size - 0.08f) / 0.92f;
  const float speed = 0.05f * exp2f(extent * 5.321928f);  // 0.05 .. 2 Hz
  theta_ += speed * dt;
  if (theta_ >= 4.0f) theta_ -= 4.0f;
  if (shape_ == SHAPE_RANDOM_WALK) {
    float step = speed * dt * 4.0f;
    walk_vx_ = 0.97f * walk_vx_ + 0.03f * rng_.Bipolar();
    walk_vy_ = 0.97f * walk_vy_ + 0.03f * rng_.Bipolar();
    walk_x_ += walk_vx_ * step * 8.0f;
    walk_y_ += walk_vy_ * step * 8.0f;
    if (walk_x_ > 1.0f) { walk_x_ = 2.0f - walk_x_; walk_vx_ = -walk_vx_; }
    if (walk_x_ < -1.0f) { walk_x_ = -2.0f - walk_x_; walk_vx_ = -walk_vx_; }
    if (walk_y_ > 1.0f) { walk_y_ = 2.0f - walk_y_; walk_vy_ = -walk_vy_; }
    if (walk_y_ < -1.0f) { walk_y_ = -2.0f - walk_y_; walk_vy_ = -walk_vy_; }
  }

  // Grain scheduling.
  const float rate = exp2f(c.density * 7.228819f);  // 1 .. 150 grains/s
  float length = 4.0f / rate;
  length = Clamp(length, 0.025f, 0.6f);
  spawn_acc_ += static_cast<float>(n);
  int spawned = 0;
  while (spawn_acc_ >= next_interval_ && spawned < 8) {
    spawn_acc_ -= next_interval_;
    Spawn(c, rate, length);
    next_interval_ = kSampleRate / rate * (0.7f + 0.6f * rng_.Float());
    ++spawned;
  }
  if (spawn_acc_ > next_interval_) spawn_acc_ = 0.0f;

  // Rendering.
  for (size_t i = 0; i < n; ++i) {
    out[i].l = 0.0f;
    out[i].r = 0.0f;
  }
  const bool mono = buffer_.channels() == 1;
  for (int k = 0; k < kMaxGrains; ++k) {
    Grain& g = grains_[k];
    if (!g.active) continue;
    float pos = g.position;
    float phase = g.phase;
    for (size_t i = 0; i < n; ++i) {
      float w = Window(phase, g.texture);
      float l = buffer_.Read(0, pos);
      float r = mono ? l : buffer_.Read(1, pos);
      out[i].l += l * w * g.gain_l;
      out[i].r += r * w * g.gain_r;
      pos += g.increment;
      phase += g.phase_inc;
      if (phase >= 1.0f) {
        g.active = false;
        break;
      }
    }
    // Keep the position bounded (wrapping is handled by the buffer reads).
    const float size = static_cast<float>(buffer_.size());
    if (pos > size) pos -= size;
    if (pos < -size) pos += size;
    g.position = pos;
    g.phase = phase;
  }
}

}  // namespace monsoon

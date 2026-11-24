// Mode 11: SHIMMER (see shimmer.h).

#include "monsoon/dsp/modes/shimmer.h"

namespace monsoon {

const float ShimmerShifter::kWindow = 1800.0f;  // 37.5 ms grains

static const size_t kBaseLengths[ShimmerMode::kLines] = {
  2851, 3769, 4513, 5591
};
static const size_t kApLengths[4] = { 142, 107, 379, 277 };
static const float kLineScale = 16384.0f;
static const float kLineInv = 1.0f / 16384.0f;

// Harmonic sets (ratio of each of the 3 shifters; 1 = no shift).
static const int kNumSets = 6;
static const float kSets[kNumSets][ShimmerMode::kShifters] = {
  { 1.0f, 1.0f, 1.0f },        // plain
  { 2.0f, 1.0f, 2.0f },        // octave
  { 1.5f, 1.0f, 1.5f },        // fifth
  { 2.0f, 1.0f, 3.0f },        // octave + fifth (octave+fifth = x3)
  { 1.75f, 1.125f, 1.5f },     // Oneirophone ratios 7/4, 9/8, 3/2
  { 0.5f, 1.0f, 0.5f },        // octave down
};

bool ShimmerMode::Init(Arena* arena) {
  for (int i = 0; i < kLines; ++i) {
    length_[i] = kBaseLengths[i] + 8;
    line_[i] = arena->Allocate<int16_t>(length_[i]);
    if (!line_[i]) return false;
  }
  for (int i = 0; i < kShifters; ++i) {
    float* b = arena->Allocate<float>(ShimmerShifter::kLength);
    if (!b) return false;
    shifter_[i].Init(b);
    shift_mix_[i] = 0.0f;
  }
  for (int i = 0; i < 4; ++i) {
    ap_buffer_[i] = arena->Allocate<float>(kApLengths[i]);
    if (!ap_buffer_[i]) return false;
    ap_pos_[i] = 0;
  }
  dc_[0].Init();
  dc_[1].Init();
  size_smoothed_ = 0.5f;
  loop_power_ = 0.0f;
  hold_power_ = 0.0f;
  hold_gain_ = 1.0f;
  frozen_ = false;
  Clear();
  return true;
}

void ShimmerMode::Clear() {
  for (int i = 0; i < kLines; ++i) {
    memset(line_[i], 0, length_[i] * sizeof(int16_t));
    write_[i] = 0;
    lp_[i] = 0.0f;
  }
  loop_power_ = 0.0f;
  hold_power_ = 0.0f;
  for (int i = 0; i < kShifters; ++i) shifter_[i].Clear();
  for (int i = 0; i < 4; ++i) memset(ap_buffer_[i], 0, kApLengths[i] * sizeof(float));
}

inline float ShimmerMode::Diffuse(int i, float x) {
  const float g = 0.6f;
  float d = ap_buffer_[i][ap_pos_[i]];
  float v = x + g * d;
  ap_buffer_[i][ap_pos_[i]] = v;
  if (++ap_pos_[i] >= kApLengths[i]) ap_pos_[i] = 0;
  return d - g * v;
}

inline float ShimmerMode::ReadLine(int i, float delay) const {
  float pos = static_cast<float>(write_[i]) - delay;
  if (pos < 0.0f) pos += static_cast<float>(length_[i]);
  int32_t a = static_cast<int32_t>(pos);
  float t = pos - a;
  int32_t b = a + 1;
  if (b >= static_cast<int32_t>(length_[i])) b = 0;
  return (line_[i][a] + (line_[i][b] - line_[i][a]) * t) * kLineInv;
}

void ShimmerMode::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  if (c.trigger) Clear();
  // FREEZE: remember the loop's energy and hold it there (the shifters'
  // crossfades and the saturator would otherwise let it fade slowly).
  if (c.freeze && !frozen_) hold_power_ = loop_power_;
  frozen_ = c.freeze;
  if (frozen_ && loop_power_ > 1e-9f) {
    float target = sqrtf(hold_power_ / loop_power_);
    target = Clamp(target, 0.97f, 1.03f);
    hold_gain_ += 0.05f * (target - hold_gain_);
  } else {
    hold_gain_ += 0.05f * (1.0f - hold_gain_);
  }

  // Block-rate parameters.
  const float size_target = 0.2f + 0.8f * Clamp(c.size, 0.0f, 1.0f);
  const float size_start = size_smoothed_;
  size_smoothed_ += 0.08f * (size_target - size_smoothed_);
  const float size_inc = (size_smoothed_ - size_start) / static_cast<float>(n);

  const float density = Clamp(c.density, 0.0f, 1.0f);
  const bool infinite = frozen_ || density > 0.97f;
  const float t60 = 0.5f * exp2f(density * 6.0f);
  float gain[kLines];
  for (int i = 0; i < kLines; ++i) {
    float len = kBaseLengths[i] * size_smoothed_;
    gain[i] = infinite ? 1.0f : powf(10.0f, -3.0f * len / (t60 * kSampleRate));
  }
  const float damp_hz = 1500.0f * exp2f(Clamp(c.texture, 0.0f, 1.0f) * 3.6f);
  const float damp = frozen_ ? 1.0f : 1.0f - expf(-kTwoPi * damp_hz / kSampleRate);
  const float in_gain = frozen_ ? 0.0f : 0.5f;

  int set = static_cast<int>(Clamp(c.position, 0.0f, 0.999f) * kNumSets);
  const float transpose = SemitonesToRatio(c.pitch);
  float ratio[kShifters];
  float mix_target[kShifters];
  for (int k = 0; k < kShifters; ++k) {
    float r = kSets[set][k];
    bool shifting = r != 1.0f;
    ratio[k] = shifting ? Clamp(r * transpose, 0.25f, 4.0f) : 1.0f;
    mix_target[k] = shifting ? 1.0f : 0.0f;
  }

  float scale = size_start;
  for (size_t s = 0; s < n; ++s) {
    scale += size_inc;
    for (int k = 0; k < kShifters; ++k) {
      shift_mix_[k] += 0.0005f * (mix_target[k] - shift_mix_[k]);
    }
    float il = Diffuse(1, Diffuse(0, in[s].l * in_gain));
    float ir = Diffuse(3, Diffuse(2, in[s].r * in_gain));

    float x[kLines];
    for (int i = 0; i < kLines; ++i) {
      float v = ReadLine(i, kBaseLengths[i] * scale);
      lp_[i] += damp * (v - lp_[i]);
      x[i] = lp_[i] * gain[i];
    }
    float outl = x[0] + x[2];
    float outr = x[1] + x[3];

    for (int i = 0; i < kLines; ++i) x[i] *= hold_gain_;
    loop_power_ += 0.0002f * (x[0] * x[0] + x[1] * x[1] + x[2] * x[2] +
                              x[3] * x[3] - loop_power_);

    float h[kLines];
    h[0] = 0.5f * (x[0] + x[1] + x[2] + x[3]);
    h[1] = 0.5f * (x[0] - x[1] + x[2] - x[3]);
    h[2] = 0.5f * (x[0] + x[1] - x[2] - x[3]);
    h[3] = 0.5f * (x[0] - x[1] - x[2] + x[3]);
    for (int k = 0; k < kShifters; ++k) {
      if (shift_mix_[k] > 0.001f) {
        float shifted = shifter_[k].Process(h[k], ratio[k] > 0.0f ? ratio[k] : 1.0f);
        h[k] += shift_mix_[k] * (shifted - h[k]);
      }
    }
    for (int i = 0; i < kLines; ++i) {
      float v = SoftClip(h[i] + ((i & 1) ? ir : il)) * kLineScale;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32767.0f) v = -32767.0f;
      line_[i][write_[i]] = static_cast<int16_t>(v);
      if (++write_[i] >= length_[i]) write_[i] = 0;
    }
    out[s].l = dc_[0].Process(outl * 0.7f);
    out[s].r = dc_[1].Process(outr * 0.7f);
  }
}

}  // namespace monsoon

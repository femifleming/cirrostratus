// Mode 11: SHIMMER (see shimmer.h).

#include "monsoon/dsp/modes/shimmer.h"
#include "monsoon/dsp/modes/cold_code.h"

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

MONSOON_COLD bool ShimmerMode::Init(Arena* arena) {
  // The pre-delay first: its 48 KB need contiguous room.
  pre_ = arena->Allocate<int16_t>(2 * kPreDelay);
  if (!pre_) return false;
  pre_write_ = 0;
  pre_delay_ = 0.0f;
  for (int i = 0; i < kLines; ++i) {
    length_[i] = kBaseLengths[i] + 8;
    line_[i] = arena->Allocate<int16_t>(length_[i]);
    if (!line_[i]) return false;
  }
  for (int i = 0; i <= kShifters; ++i) {
    float* b = arena->Allocate<float>(ShimmerShifter::kLength);
    if (!b) return false;
    (i < kShifters ? shifter_[i] : mirror_).Init(b);
    mod_[i] = 0.0f;  // kShifters + 1 == kLines
  }
  memset(shift_mix_, 0, sizeof(shift_mix_));
  mirror_mix_ = 0.0f;
  mod_depth_ = 0.0f;
  mod_phase_ = 0.0f;
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
  mirror_.Clear();
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

void ShimmerMode::PreDelay(const FloatFrame* in, FloatFrame* out, size_t n,
                           float target) {
  for (size_t s = 0; s < n; ++s) {
    int16_t* w = pre_ + 2 * pre_write_;
    w[0] = static_cast<int16_t>(Clamp(in[s].l, -1.99f, 1.99f) * kLineScale);
    w[1] = static_cast<int16_t>(Clamp(in[s].r, -1.99f, 1.99f) * kLineScale);
    // ~0.26 s glide; never faster than the write head.
    pre_delay_ += 0.00008f * (target - pre_delay_);
    float pos = static_cast<float>(pre_write_) - pre_delay_;
    if (pos < 0.0f) pos += kPreDelay;
    size_t a = static_cast<size_t>(pos);
    float t = pos - a;
    const int16_t* p0 = pre_ + 2 * a;
    const int16_t* p1 = a + 1 >= kPreDelay ? pre_ : p0 + 2;
    out[s].l = (p0[0] + (p1[0] - p0[0]) * t) * kLineInv;
    out[s].r = (p0[1] + (p1[1] - p0[1]) * t) * kLineInv;
    if (++pre_write_ >= kPreDelay) pre_write_ = 0;
  }
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
  // BAL: left scales the shifted share down, right mirrors the first
  // interval on the fourth line.
  const float bal = KnobBipolar(c.bal);
  const float share = bal < 0.0f ? 1.0f + bal : 1.0f;
  float ratio[kShifters];
  float mix_target[kShifters];
  for (int k = 0; k < kShifters; ++k) {
    float r = kSets[set][k];
    bool shifting = r != 1.0f;
    ratio[k] = shifting ? Clamp(r * transpose, 0.25f, 4.0f) : 1.0f;
    mix_target[k] = shifting ? share : 0.0f;
  }
  const float mirror_ratio = Clamp(1.0f / ratio[0], 0.25f, 4.0f);
  const float mirror_target =
      bal > 0.0f && kSets[set][0] != 1.0f ? bal : 0.0f;

  // FBK: tank modulation. Two slow LFOs (0.53 and 0.89 Hz), each driving
  // two lines in antiphase; per-block targets ramped across the block.
  // At zero everything stays exactly 0 (the v1.0 delays).
  const float fbk = Clamp(c.fbk, 0.0f, 1.0f);
  mod_depth_ += 0.05f * (32.0f * fbk - mod_depth_);
  if (fbk == 0.0f && mod_depth_ < 0.005f) mod_depth_ = 0.0f;
  mod_phase_ += static_cast<float>(n) / kSampleRate;
  if (mod_phase_ >= 100.0f) mod_phase_ -= 100.0f;
  float lfo[2];
  for (int k = 0; k < 2; ++k) {
    float p = mod_phase_ * (k ? 0.89f : 0.53f);
    p = 2.0f * (p - static_cast<float>(static_cast<int32_t>(p))) - 1.0f;
    lfo[k] = 4.0f * p * (1.0f - fabsf(p));  // parabolic sine
  }
  const float ma = mod_depth_ * lfo[0];
  const float mb = mod_depth_ * lfo[1];
  const float mod_target[kLines] = { mod_depth_ + ma, mod_depth_ + mb,
                                     mod_depth_ - ma, mod_depth_ - mb };
  float mod_inc[kLines];
  for (int i = 0; i < kLines; ++i) {
    mod_inc[i] = (mod_target[i] - mod_[i]) / static_cast<float>(n);
  }

  // RVB: pre-delay. Runs only while in use; it starts from zero delay
  // and glides slowly enough that it only ever reads what it wrote.
  FloatFrame delayed[kBlockSize];
  const FloatFrame* src = in;
  const float pre_target = Clamp(c.rvb, 0.0f, 1.0f) * (kPreDelay - 4);
  if ((pre_target > 0.0f || pre_delay_ > 0.5f) && n <= kBlockSize) {
    PreDelay(in, delayed, n, pre_target);
    src = delayed;
  }

  float scale = size_start;
  for (size_t s = 0; s < n; ++s) {
    scale += size_inc;
    for (int k = 0; k < kShifters; ++k) {
      shift_mix_[k] += 0.0005f * (mix_target[k] - shift_mix_[k]);
    }
    mirror_mix_ += 0.0005f * (mirror_target - mirror_mix_);
    float il = Diffuse(1, Diffuse(0, src[s].l * in_gain));
    float ir = Diffuse(3, Diffuse(2, src[s].r * in_gain));

    float x[kLines];
    for (int i = 0; i < kLines; ++i) {
      mod_[i] += mod_inc[i];
      float v = ReadLine(i, kBaseLengths[i] * scale - mod_[i]);
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
    if (mirror_mix_ > 0.001f) {
      float shifted = mirror_.Process(h[3], mirror_ratio);
      h[3] += mirror_mix_ * (shifted - h[3]);
    }
    for (int i = 0; i < kLines; ++i) {
      float v = SoftClip(h[i] + ((i & 1) ? ir : il)) * kLineScale;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32767.0f) v = -32767.0f;
      line_[i][write_[i]] = static_cast<int16_t>(v);
      if (++write_[i] >= length_[i]) write_[i] = 0;
    }
    out[s].l = dc_[0].Process(outl * 0.95f);
    out[s].r = dc_[1].Process(outr * 0.95f);
  }
  if (mod_depth_ == 0.0f) memset(mod_, 0, sizeof(mod_));  // land exactly
}

}  // namespace monsoon

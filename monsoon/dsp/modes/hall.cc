// Mode 26: HALL (see hall.h).

#include "monsoon/dsp/modes/hall.h"

namespace monsoon {

namespace {

const size_t kLengths[HallMode::kLines] = {
  3203, 3907, 4561, 5113, 5689, 6337, 7019, 7759
};
const size_t kApLengths[HallMode::kLines] = {
  113, 149, 191, 227, 263, 307, 353, 401
};
const size_t kInApLengths[4] = { 142, 107, 379, 277 };
const size_t kModHeadroom = 48;
const size_t kPreSize = 12000 + 64;       // 250 ms
const float kLineScale = 16384.0f;
const float kLineInv = 1.0f / 16384.0f;
const float kLineApGain = 0.55f;
const float kInApGain = 0.65f;

}  // namespace

bool HallMode::Init(Arena* arena) {
  // Biggest first so the two RAM regions pack well.
  pre_ = arena->Allocate<int16_t>(kPreSize);
  if (!pre_) return false;
  for (int i = kLines - 1; i >= 0; --i) {
    line_size_[i] = kLengths[i] + kModHeadroom;
    line_[i] = arena->Allocate<int16_t>(line_size_[i]);
    if (!line_[i]) return false;
  }
  for (int i = 0; i < kLines; ++i) {
    ap_[i] = arena->Allocate<float>(kApLengths[i]);
    if (!ap_[i]) return false;
  }
  for (int i = 0; i < 4; ++i) {
    in_ap_[i] = arena->Allocate<float>(kInApLengths[i]);
    if (!in_ap_[i]) return false;
  }
  scale_ = 0.7f;
  pre_delay_ = 0.0f;
  lfo_phase_ = 0.0f;
  frozen_ = false;
  clear_pending_ = false;
  Clear();
  return true;
}

void HallMode::Prepare() {
  if (clear_pending_) {
    Clear();
    clear_pending_ = false;
  }
}

void HallMode::Clear() {
  dc_[0].Init();
  dc_[1].Init();
  memset(pre_, 0, kPreSize * sizeof(int16_t));
  pre_pos_ = 0;
  for (int i = 0; i < kLines; ++i) {
    memset(line_[i], 0, line_size_[i] * sizeof(int16_t));
    memset(ap_[i], 0, kApLengths[i] * sizeof(float));
    write_[i] = 0;
    ap_pos_[i] = 0;
    lp_[i] = 0.0f;
  }
  for (int i = 0; i < 4; ++i) {
    memset(in_ap_[i], 0, kInApLengths[i] * sizeof(float));
    in_ap_pos_[i] = 0;
  }
}

inline float HallMode::ReadLine(int i, float delay) const {
  float pos = static_cast<float>(write_[i]) - delay;
  if (pos < 0.0f) pos += static_cast<float>(line_size_[i]);
  int32_t a = static_cast<int32_t>(pos);
  float t = pos - static_cast<float>(a);
  int32_t b = a + 1;
  if (b >= static_cast<int32_t>(line_size_[i])) b = 0;
  const int16_t* l = line_[i];
  return (l[a] + (l[b] - l[a]) * t) * kLineInv;
}

inline float HallMode::LineAllpass(int i, float x) {
  float* buf = ap_[i];
  float d = buf[ap_pos_[i]];
  float v = x + kLineApGain * d;
  buf[ap_pos_[i]] = v;
  if (++ap_pos_[i] >= kApLengths[i]) ap_pos_[i] = 0;
  return d - kLineApGain * v;
}

inline float HallMode::InputAllpass(int i, float x) {
  float* buf = in_ap_[i];
  float d = buf[in_ap_pos_[i]];
  float v = x + kInApGain * d;
  buf[in_ap_pos_[i]] = v;
  if (++in_ap_pos_[i] >= kInApLengths[i]) in_ap_pos_[i] = 0;
  return d - kInApGain * v;
}

void HallMode::Process(const Controls& c, const FloatFrame* in,
                       FloatFrame* out, size_t n) {
  if (c.trigger) clear_pending_ = true;
  frozen_ = c.freeze;
  if (clear_pending_) {
    // Silent until the main loop has wiped the tank.
    for (size_t s = 0; s < n; ++s) out[s].l = out[s].r = 0.0f;
    return;
  }

  // Size glides (changing line lengths bends pitch, so keep it slow).
  float scale_target = 0.35f + 0.65f * Clamp(c.size, 0.0f, 1.0f);
  float scale_start = scale_;
  scale_ += 0.03f * (scale_target - scale_);
  const float scale_inc = (scale_ - scale_start) / static_cast<float>(n);

  float pre_target = Clamp(c.position, 0.0f, 1.0f) * 12000.0f;
  float pre_start = pre_delay_;
  pre_delay_ += 0.05f * (pre_target - pre_delay_);
  const float pre_inc = (pre_delay_ - pre_start) / static_cast<float>(n);

  const float density = Clamp(c.density, 0.0f, 1.0f);
  const float t60 = 0.4f * powf(100.0f, density);  // damping shortens it to ~30 s
  float gain[kLines];
  for (int i = 0; i < kLines; ++i) {
    float loop = kLengths[i] * scale_ + kApLengths[i];
    gain[i] = frozen_ ? 1.0f : powf(10.0f, -3.0f * loop / (t60 * kSampleRate));
  }
  const float damp_hz = 1200.0f * powf(13.33f, Clamp(c.texture, 0.0f, 1.0f));
  const float damp = frozen_ ? 1.0f : 1.0f - expf(-kTwoPi * damp_hz / kSampleRate);
  const float in_gain = frozen_ ? 0.0f : 0.35f;

  // PITCH -> modulation depth (samples) and rate.
  const float m = Clamp((c.pitch + 48.0f) / 96.0f, 0.0f, 1.0f);
  const float mod_depth = 40.0f * m * m;
  const float lfo_inc = (0.1f + 2.9f * m * m) / kSampleRate;

  float scale = scale_start;
  float pre = pre_start;
  for (size_t s = 0; s < n; ++s) {
    scale += scale_inc;
    pre += pre_inc;

    // Pre-delay (mono, 16-bit), then input diffusion.
    float mono = 0.5f * (in[s].l + in[s].r);
    float v = Clamp(mono, -1.99f, 1.99f) * kLineScale;
    pre_[pre_pos_] = static_cast<int16_t>(v);
    float rp = static_cast<float>(pre_pos_) - pre;
    if (rp < 0.0f) rp += static_cast<float>(kPreSize);
    int32_t ia = static_cast<int32_t>(rp);
    float ft = rp - static_cast<float>(ia);
    int32_t ib = ia + 1 >= static_cast<int32_t>(kPreSize) ? 0 : ia + 1;
    float x = (pre_[ia] + (pre_[ib] - pre_[ia]) * ft) * kLineInv;
    if (++pre_pos_ >= kPreSize) pre_pos_ = 0;
    float early = InputAllpass(1, InputAllpass(0, x));
    x = InputAllpass(3, InputAllpass(2, early));
    // Early diffuse sound fills the gap before the tank's first return.
    const float early_l = early * in_gain * 0.5f;
    const float early_r = x * in_gain * 0.5f;
    x *= in_gain;

    lfo_phase_ += lfo_inc;
    if (lfo_phase_ >= 1.0f) lfo_phase_ -= 1.0f;
    const float m0 = mod_depth * (1.0f + FastSin(lfo_phase_));
    const float m1 = mod_depth * (1.0f + FastCos(lfo_phase_));

    float y[kLines];
    for (int i = 0; i < kLines; ++i) {
      float d = kLengths[i] * scale;
      if (i == 0 || i == 5) d += m0;
      if (i == 2 || i == 7) d += m1;
      float r = LineAllpass(i, ReadLine(i, d));
      lp_[i] += damp * (r - lp_[i]);
      y[i] = lp_[i] * gain[i];
    }
    float outl = y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] - y[5]);
    float outr = y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] - y[6]);

    // Fast Walsh-Hadamard transform, normalised.
    for (int h = 1; h < kLines; h <<= 1) {
      for (int i = 0; i < kLines; i += h << 1) {
        for (int j = i; j < i + h; ++j) {
          float a = y[j], b = y[j + h];
          y[j] = a + b;
          y[j + h] = a - b;
        }
      }
    }
    for (int i = 0; i < kLines; ++i) {
      float w = y[i] * 0.35355339f + ((i & 1) ? -x : x);
      w *= kLineScale;
      if (w > 32767.0f) w = 32767.0f;
      if (w < -32767.0f) w = -32767.0f;
      line_[i][write_[i]] = static_cast<int16_t>(w);
      if (++write_[i] >= line_size_[i]) write_[i] = 0;
    }
    out[s].l = dc_[0].Process(outl * 0.45f + early_l);
    out[s].r = dc_[1].Process(outr * 0.45f + early_r);
  }
}

}  // namespace monsoon

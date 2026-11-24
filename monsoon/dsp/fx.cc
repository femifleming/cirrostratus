// Output-section effects (see fx.h).

#include "monsoon/dsp/fx.h"

namespace monsoon {

static const size_t kFdnLengths[Fdn::kLines] = {
  1031, 1327, 1523, 1801, 2053, 2311, 2591, 2833
};
static const size_t kModHeadroom = 24;
static const float kLineScale = 16384.0f;       // 6 dB headroom in int16
static const float kLineInvScale = 1.0f / 16384.0f;

size_t Fdn::MemoryBytes() {
  size_t total = 0;
  for (int i = 0; i < kLines; ++i) {
    total += ((kFdnLengths[i] + kModHeadroom) * sizeof(int16_t) + 7) & ~7u;
  }
  return total;
}

bool Fdn::Init(Arena* arena) {
  for (int i = 0; i < kLines; ++i) {
    length_[i] = kFdnLengths[i] + kModHeadroom;
    line_[i] = arena->Allocate<int16_t>(length_[i]);
    if (!line_[i]) return false;
  }
  in_ap_[0].Init(142, 0.62f);
  in_ap_[1].Init(107, 0.62f);
  in_ap_[2].Init(149, 0.62f);
  in_ap_[3].Init(113, 0.62f);
  dc_[0].Init();
  dc_[1].Init();
  lfo_phase_[0] = 0.0f;
  lfo_phase_[1] = 0.37f;
  amount_smoothed_ = 0.0f;
  Clear();
  return true;
}

void Fdn::Clear() {
  for (int i = 0; i < kLines; ++i) {
    memset(line_[i], 0, length_[i] * sizeof(int16_t));
    write_[i] = 0;
    lp_[i] = 0.0f;
  }
}

inline float Fdn::ReadLine(int i, float delay) const {
  float pos = static_cast<float>(write_[i]) - delay;
  const float n = static_cast<float>(length_[i]);
  if (pos < 0.0f) pos += n;
  int32_t a = static_cast<int32_t>(pos);
  float t = pos - a;
  int32_t b = a + 1;
  if (b >= static_cast<int32_t>(length_[i])) b = 0;
  return (line_[i][a] + (line_[i][b] - line_[i][a]) * t) * kLineInvScale;
}

void Fdn::Process(float amount, bool freeze, FloatFrame* io, size_t n) {
  // Block-rate parameter computation.
  amount_smoothed_ += 0.2f * (amount - amount_smoothed_);
  const float a = amount_smoothed_;
  if (a < 0.002f && !freeze) {
    return;  // Fully dry: skip the CPU.
  }
  const float t60 = 0.5f + a * a * 16.0f;
  for (int i = 0; i < kLines; ++i) {
    gain_[i] = freeze ? 1.0f :
        powf(10.0f, -3.0f * kFdnLengths[i] / (t60 * kSampleRate));
  }
  // Darker as the tail gets longer; no damping when frozen.
  damp_coef_ = freeze ? 1.0f : 0.75f - 0.35f * a;
  const float in_gain = freeze ? 0.0f : 0.35f;
  const float wet = a < 0.5f ? a * 1.2f : 0.6f + (a - 0.5f) * 0.5f;
  const float lfo_inc = 0.6f / kSampleRate * static_cast<float>(1);

  for (size_t s = 0; s < n; ++s) {
    float il = in_ap_[1].Process(in_ap_[0].Process(io[s].l * in_gain));
    float ir = in_ap_[3].Process(in_ap_[2].Process(io[s].r * in_gain));

    lfo_phase_[0] += lfo_inc;
    lfo_phase_[1] += lfo_inc * 1.31f;
    if (lfo_phase_[0] >= 1.0f) lfo_phase_[0] -= 1.0f;
    if (lfo_phase_[1] >= 1.0f) lfo_phase_[1] -= 1.0f;

    float x[kLines];
    for (int i = 0; i < kLines; ++i) {
      float d = static_cast<float>(kFdnLengths[i]);
      if (i == 0) d += 10.0f * (1.0f + FastSin(lfo_phase_[0]));
      if (i == 5) d += 10.0f * (1.0f + FastSin(lfo_phase_[1]));
      float v = ReadLine(i, d);
      lp_[i] += damp_coef_ * (v - lp_[i]);
      x[i] = lp_[i] * gain_[i];
    }

    float outl = x[0] - x[2] + x[4] - x[6];
    float outr = x[1] - x[3] + x[5] - x[7];

    // Fast Walsh-Hadamard transform, normalised by 1/sqrt(8).
    for (int h = 1; h < kLines; h <<= 1) {
      for (int i = 0; i < kLines; i += h << 1) {
        for (int j = i; j < i + h; ++j) {
          float u = x[j], v = x[j + h];
          x[j] = u + v;
          x[j + h] = u - v;
        }
      }
    }
    const float norm = 0.35355339f;
    for (int i = 0; i < kLines; ++i) {
      float v = x[i] * norm + ((i & 1) ? ir : il);
      v *= kLineScale;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32767.0f) v = -32767.0f;
      line_[i][write_[i]] = static_cast<int16_t>(v);
      if (++write_[i] >= length_[i]) write_[i] = 0;
    }

    io[s].l += dc_[0].Process(outl * 0.5f) * wet;
    io[s].r += dc_[1].Process(outr * 0.5f) * wet;
  }
}

}  // namespace monsoon

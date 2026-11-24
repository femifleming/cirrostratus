// Output-section effects (see fx.h).

#include "monsoon/dsp/fx.h"

namespace monsoon {

static const uint32_t kFdnLengths[Fdn::kLines] = {
  1031, 1327, 1523, 1801, 2053, 2311, 2591, 2833
};
static const uint32_t kModHeadroom = 24;
static const float kLineScale = 16384.0f;       // 6 dB headroom in int16
static const float kLineInvScale = 1.0f / 16384.0f;

bool Fdn::Init(Arena* arena) {
  buffer_ = arena->Allocate<int16_t>(kBufferSize);
  if (!buffer_) return false;
  uint32_t base = 0;
  for (int i = 0; i < kLines; ++i) {
    base_[i] = base;
    base += kFdnLengths[i] + kModHeadroom;
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
  memset(buffer_, 0, kBufferSize * sizeof(int16_t));
  write_ptr_ = 0;
  for (int i = 0; i < kLines; ++i) lp_[i] = 0.0f;
}

void Fdn::Process(float amount, bool freeze, FloatFrame* io, size_t n) {
  amount_smoothed_ += 0.2f * (amount - amount_smoothed_);
  const float a = amount_smoothed_;
  if (a < 0.002f && !freeze) {
    return;  // Fully dry: skip the CPU.
  }
  const float t60 = 0.5f + a * a * 16.0f;
  // Hadamard 1/sqrt(8) and the int16 scale folded into the line gains.
  const float norm = 0.35355339f;
  float g[kLines];
  for (int i = 0; i < kLines; ++i) {
    float gain = freeze ? 1.0f :
        powf(10.0f, -3.0f * kFdnLengths[i] / (t60 * kSampleRate));
    g[i] = gain * norm * kLineInvScale;
  }
  const float damp = freeze ? 1.0f : 0.75f - 0.35f * a;
  const float in_gain = (freeze ? 0.0f : 0.35f) * kLineScale;
  const float wet = (a < 0.5f ? a * 1.2f : 0.6f + (a - 0.5f) * 0.5f) *
      0.5f / norm;

  // Modulation of lines 0 and 5, linear across the block.
  const float block_inc = 0.6f * static_cast<float>(n) / kSampleRate;
  const float m0a = 10.0f * (1.0f + FastSin(lfo_phase_[0]));
  const float m5a = 10.0f * (1.0f + FastSin(lfo_phase_[1]));
  lfo_phase_[0] += block_inc;
  lfo_phase_[1] += block_inc * 1.31f;
  if (lfo_phase_[0] >= 1.0f) lfo_phase_[0] -= 1.0f;
  if (lfo_phase_[1] >= 1.0f) lfo_phase_[1] -= 1.0f;
  const float inv_n = 1.0f / static_cast<float>(n);
  const float m0_inc = (10.0f * (1.0f + FastSin(lfo_phase_[0])) - m0a) * inv_n;
  const float m5_inc = (10.0f * (1.0f + FastSin(lfo_phase_[1])) - m5a) * inv_n;
  float m0 = m0a, m5 = m5a;

  const uint32_t mask = kBufferSize - 1;
  int16_t* const buf = buffer_;
  uint32_t wp = write_ptr_;
  float lp0 = lp_[0], lp1 = lp_[1], lp2 = lp_[2], lp3 = lp_[3];
  float lp4 = lp_[4], lp5 = lp_[5], lp6 = lp_[6], lp7 = lp_[7];

#define FDN_TAP(i) static_cast<float>(buf[(wp + base_[i] + kFdnLengths[i]) & mask])
#define FDN_TAP_MOD(i, m) ({                                          \
    float pos = static_cast<float>(kFdnLengths[i]) + (m);              \
    uint32_t ip = static_cast<uint32_t>(pos);                          \
    float t = pos - static_cast<float>(ip);                            \
    float x0 = buf[(wp + base_[i] + ip) & mask];                       \
    float x1 = buf[(wp + base_[i] + ip + 1) & mask];                   \
    x0 + (x1 - x0) * t; })
#define FDN_WRITE(i, v) do {                                           \
    float w = (v);                                                     \
    w = w > 32767.0f ? 32767.0f : (w < -32767.0f ? -32767.0f : w);     \
    buf[(wp + base_[i]) & mask] = static_cast<int16_t>(w);             \
  } while (0)

  for (size_t s = 0; s < n; ++s) {
    float il = in_ap_[1].Process(in_ap_[0].Process(io[s].l)) * in_gain;
    float ir = in_ap_[3].Process(in_ap_[2].Process(io[s].r)) * in_gain;

    lp0 += damp * (FDN_TAP_MOD(0, m0) - lp0);
    lp1 += damp * (FDN_TAP(1) - lp1);
    lp2 += damp * (FDN_TAP(2) - lp2);
    lp3 += damp * (FDN_TAP(3) - lp3);
    lp4 += damp * (FDN_TAP(4) - lp4);
    lp5 += damp * (FDN_TAP_MOD(5, m5) - lp5);
    lp6 += damp * (FDN_TAP(6) - lp6);
    lp7 += damp * (FDN_TAP(7) - lp7);
    m0 += m0_inc;
    m5 += m5_inc;

    float x0 = lp0 * g[0], x1 = lp1 * g[1], x2 = lp2 * g[2], x3 = lp3 * g[3];
    float x4 = lp4 * g[4], x5 = lp5 * g[5], x6 = lp6 * g[6], x7 = lp7 * g[7];
    const float outl = x0 - x2 + x4 - x6;
    const float outr = x1 - x3 + x5 - x7;

    float a0 = x0 + x1, a1 = x0 - x1, a2 = x2 + x3, a3 = x2 - x3;
    float a4 = x4 + x5, a5 = x4 - x5, a6 = x6 + x7, a7 = x6 - x7;
    float b0 = a0 + a2, b2 = a0 - a2, b1 = a1 + a3, b3 = a1 - a3;
    float b4 = a4 + a6, b6 = a4 - a6, b5 = a5 + a7, b7 = a5 - a7;

    // The ring moves backwards: decrementing the pointer ages every line.
    wp = (wp - 1) & mask;
    FDN_WRITE(0, (b0 + b4) * kLineScale + il);
    FDN_WRITE(1, (b1 + b5) * kLineScale + ir);
    FDN_WRITE(2, (b2 + b6) * kLineScale + il);
    FDN_WRITE(3, (b3 + b7) * kLineScale + ir);
    FDN_WRITE(4, (b0 - b4) * kLineScale + il);
    FDN_WRITE(5, (b1 - b5) * kLineScale + ir);
    FDN_WRITE(6, (b2 - b6) * kLineScale + il);
    FDN_WRITE(7, (b3 - b7) * kLineScale + ir);

    io[s].l += dc_[0].Process(outl) * wet;
    io[s].r += dc_[1].Process(outr) * wet;
  }
#undef FDN_TAP
#undef FDN_TAP_MOD
#undef FDN_WRITE
  write_ptr_ = wp;
  lp_[0] = lp0; lp_[1] = lp1; lp_[2] = lp2; lp_[3] = lp3;
  lp_[4] = lp4; lp_[5] = lp5; lp_[6] = lp6; lp_[7] = lp7;
}

}  // namespace monsoon

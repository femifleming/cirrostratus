// Mode 30 — POLISH: mastering chain. See polish.h.

#include "monsoon/dsp/modes/polish.h"

#include "monsoon/dsp/modes/bal_knob.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

// 2x oversampling: 4-coefficient polyphase IIR half-band (Niemitalo).
static const float kHb[4] = { 0.07986642623635751f, 0.5453536510711322f,
                              0.28382934487410993f, 0.8344118914807379f };

static inline float Ap1(float x, float a, float* s) {
  float y = *s + a * x;
  *s = x - a * y;
  return y;
}

void PolishMode::SvfCoef::Set(float hz, float q) {
  g = tanf(kPi * hz / kSampleRate);
  k = 1.0f / q;
  a1 = 1.0f / (1.0f + g * (g + k));
  a2 = g * a1;
  a3 = g * a2;
}

inline void PolishMode::Svf(const SvfCoef& c, SvfState* s, float x,
                            float* lp, float* bp, float* hp) {
  const float v3 = x - s->ic2;
  const float v1 = c.a1 * s->ic1 + c.a2 * v3;
  const float v2 = s->ic2 + c.a2 * s->ic1 + c.a3 * v3;
  s->ic1 = 2.0f * v1 - s->ic1;
  s->ic2 = 2.0f * v2 - s->ic2;
  *lp = v2;
  *bp = v1;
  *hp = x - c.k * v1 - v2;
}

bool PolishMode::Init(Arena* arena) {
  (void)arena;
  const float q = 0.70710678f;
  c200_.Set(200.0f, q);
  c3k_.Set(3000.0f, q);
  c120_.Set(120.0f, q);
  c6k_.Set(6000.0f, q);
  memset(air_hp_, 0, sizeof(air_hp_));
  ny_mix_ = 0.0f;
  air_mix_ = 0.0f;
  memset(s1a_, 0, sizeof(s1a_)); memset(s1l_, 0, sizeof(s1l_));
  memset(s1h_, 0, sizeof(s1h_)); memset(s2a_, 0, sizeof(s2a_));
  memset(s2l_, 0, sizeof(s2l_)); memset(s2h_, 0, sizeof(s2h_));
  memset(ap_, 0, sizeof(ap_));
  memset(&side_hp_, 0, sizeof(side_hp_));
  memset(&side_hp2_, 0, sizeof(side_hp2_));
  for (int b = 0; b < 4; ++b) {
    env_[b] = 0.0f;
    gr_db_[b] = 0.0f;
    gain_[b] = 1.0f;
  }
  tilt_lp_[0] = tilt_lp_[1] = 0.0f;
  tilt_coef_ = 1.0f - expf(-kTwoPi * 800.0f / kSampleRate);
  memset(up_state_, 0, sizeof(up_state_));
  memset(down_state_, 0, sizeof(down_state_));
  bypass_ = 0.0f;
  trim_ = 1.0f;
  auto_gain_ = auto_target_ = 1.0f;
  loud_in_ = loud_out_ = 0.0f;
  prev_trigger_ = false;
  return true;
}

// A knob amount gliding towards its target (~10 ms), exactly 0 when off.
static __attribute__((noinline)) void Glide(float* v, float target) {
  target = Clamp(target, 0.0f, 1.0f);
  *v += 0.06f * (target - *v);
  if (target == 0.0f && *v < 1e-4f) *v = 0.0f;
}

void PolishMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  const float inv_n = 1.0f / static_cast<float>(n);

  // --- Compressor gains for this block (from last block's detectors). ----
  const float d = c.density;
  const float thr = -12.0f - 24.0f * d;
  const float ratio = 1.0f + 3.0f * d;
  const float slope = 1.0f - 1.0f / ratio;
  const float makeup = 0.35f * (-thr) * slope;
  const float block_s = static_cast<float>(n) / kSampleRate;
  // Band 3 is FBK's smashed copy (see below).
  static const float kAttack[4] = { 0.020f, 0.010f, 0.005f, 0.002f };
  static const float kRelease[4] = { 0.250f, 0.150f, 0.100f, 0.120f };
  float g_start[4], g_inc[4];
  // BAL: shift the low and high bands' thresholds in opposite directions.
  const float lean = 9.0f * BalKnob(c.bal);
  // FBK: parallel compression. A smashed copy of the multiband output
  // (fast detector, 10:1 at -30 dBFS, +15 dB) is blended in; as the copy
  // is the same signal, the blend is a gain of 1 + mix * g.
  // (Blend glides ~10 ms, then ramps per sample: no zipper under CV.)
  const float ny_mix_start = ny_mix_;
  Glide(&ny_mix_, 0.5f * c.fbk);
  const bool ny = ny_mix_start > 0.0f || ny_mix_ > 0.0f;
  const float ny_mix_inc = (ny_mix_ - ny_mix_start) * inv_n;
  const int bands = ny ? 4 : 3;
  for (int b = 0; b < bands; ++b) {
    const float level = 3.0103f * FastLog2(env_[b] + 1e-12f);  // dB
    const float th = b == 3 ? -30.0f : thr + lean * static_cast<float>(1 - b);
    const float sl = b == 3 ? 0.9f : slope;
    const float over = level - th;
    float target = 0.0f;
    if (over > 3.0f) target = over * sl;
    else if (over > -3.0f) target = (over + 3.0f) * (over + 3.0f) / 12.0f * sl;
    const float tc = target > gr_db_[b] ? kAttack[b] : kRelease[b];
    gr_db_[b] += (1.0f - FastExp2(-block_s / tc * 1.4427f)) * (target - gr_db_[b]);
    const float g = FastExp2(((b == 3 ? 15.0f : makeup) - gr_db_[b]) * 0.16609640f);
    g_start[b] = gain_[b];
    g_inc[b] = (g - gain_[b]) * inv_n;
    gain_[b] = g;
  }
  const float env_a = 1.0f - expf(-1.0f / (0.01f * kSampleRate));
  const float ny_env_a = 1.0f - expf(-1.0f / (0.002f * kSampleRate));

  // RVB: air exciter amount (ramped).
  const float air_start = air_mix_;
  Glide(&air_mix_, 0.8f * c.rvb);
  const bool air = air_start > 0.0f || air_mix_ > 0.0f;
  const float air_inc = (air_mix_ - air_start) * inv_n;

  // Tilt (+/- 4 dB), saturation, width, trim, bypass.
  const float tilt = (c.position - 0.5f) * 2.0f;
  const float gh = FastExp2(tilt * 0.664f);
  const float gl = FastExp2(-tilt * 0.664f);
  const float tex = c.texture;
  const bool saturate = tex > 0.001f;
  const float drive = 1.0f + 5.0f * tex;
  const float bias = 0.15f * tex;
  const float bias_out = SoftClip(bias);
  const float inv_drive = 1.0f / drive;
  const float sat_mix = tex * 4.0f > 1.0f ? 1.0f : tex * 4.0f;
  const float width = c.size * 2.0f;
  const float trim_target = exp2f(c.pitch * (1.0f / 24.0f));
  const float trim_inc = (trim_target - trim_) * inv_n;
  const float bypass_target = c.freeze ? 1.0f : 0.0f;
  const float loud_a = 1.0f - expf(-1.0f / (0.4f * kSampleRate));

  if (c.trigger && !prev_trigger_) {
    float t = loud_out_ > 1e-9f ? sqrtf(loud_in_ / loud_out_) : 1.0f;
    auto_target_ = Clamp(t, 0.25f, 4.0f);
  }
  prev_trigger_ = c.trigger;

  for (size_t i = 0; i < n; ++i) {
    float x[2] = { in[i].l, in[i].r };
    float band[3][2];
    for (int ch = 0; ch < 2; ++ch) {
      float v = x[ch];
      tilt_lp_[ch] += tilt_coef_ * (v - tilt_lp_[ch]);
      v = tilt_lp_[ch] * gl + (v - tilt_lp_[ch]) * gh;
      float lp, bp, hp, t1, t2;
      // Split at 200 Hz (Linkwitz-Riley 4th order = Butterworth squared).
      Svf(c200_, &s1a_[ch], v, &lp, &bp, &hp);
      float low, rest;
      Svf(c200_, &s1l_[ch], lp, &low, &t1, &t2);
      Svf(c200_, &s1h_[ch], hp, &t1, &t2, &rest);
      // Split the rest at 3 kHz.
      Svf(c3k_, &s2a_[ch], rest, &lp, &bp, &hp);
      float mid, high;
      Svf(c3k_, &s2l_[ch], lp, &mid, &t1, &t2);
      Svf(c3k_, &s2h_[ch], hp, &t1, &t2, &high);
      // Low band through the 3 kHz allpass so all three bands line up.
      float al, ab, ah;
      Svf(c3k_, &ap_[ch], low, &al, &ab, &ah);
      band[0][ch] = low - 2.0f * c3k_.k * ab;
      band[1][ch] = mid;
      band[2][ch] = high;
    }
    float y[2] = { 0.0f, 0.0f };
    for (int b = 0; b < 3; ++b) {
      const float l = band[b][0], r = band[b][1];
      const float ms = l * l > r * r ? l * l : r * r;
      env_[b] += env_a * (ms - env_[b]);
      const float g = g_start[b] + g_inc[b] * static_cast<float>(i + 1);
      y[0] += l * g;
      y[1] += r * g;
    }

    if (ny) {
      const float fi = static_cast<float>(i + 1);
      const float ms = y[0] * y[0] > y[1] * y[1] ? y[0] * y[0] : y[1] * y[1];
      env_[3] += ny_env_a * (ms - env_[3]);
      const float k = 1.0f + (ny_mix_start + ny_mix_inc * fi) *
          (g_start[3] + g_inc[3] * fi);
      y[0] *= k;
      y[1] *= k;
    }

    if (saturate) {
      for (int ch = 0; ch < 2; ++ch) {
        float* us = up_state_[ch];
        float* ds = down_state_[ch];
        const float u0 = Ap1(Ap1(y[ch], kHb[0], &us[0]), kHb[2], &us[1]);
        const float u1 = Ap1(Ap1(y[ch], kHb[1], &us[2]), kHb[3], &us[3]);
        const float s1 = (SoftClip(u1 * drive + bias) - bias_out) * inv_drive;
        const float s0 = (SoftClip(u0 * drive + bias) - bias_out) * inv_drive;
        const float d0 = Ap1(Ap1(s0, kHb[0], &ds[0]), kHb[2], &ds[1]);
        const float d1 = Ap1(Ap1(s1, kHb[1], &ds[2]), kHb[3], &ds[3]);
        const float sat = 0.5f * (d0 + d1);
        y[ch] += sat_mix * (sat - y[ch]);
      }
    }

    if (air) {
      // Full-wave rectified highs: their even harmonics, above 6 kHz.
      const float amt = air_start + air_inc * static_cast<float>(i + 1);
      const float gh = g_start[2] + g_inc[2] * static_cast<float>(i + 1);
      for (int ch = 0; ch < 2; ++ch) {
        float lp, bp, hp;
        Svf(c6k_, &air_hp_[ch], fabsf(band[2][ch] * gh), &lp, &bp, &hp);
        y[ch] += amt * hp;
      }
    }

    // Width with mono bass.
    const float m = 0.5f * (y[0] + y[1]);
    float s = 0.5f * (y[0] - y[1]);
    float lp, bp, hp;
    Svf(c120_, &side_hp_, s, &lp, &bp, &hp);
    Svf(c120_, &side_hp2_, hp, &lp, &bp, &hp);
    s = hp * width;
    float ol = m + s, orr = m - s;

    // Loudness (for TRIG's auto-gain), then auto-gain and trim.
    const float mi = 0.5f * (x[0] + x[1]);
    loud_in_ += loud_a * (mi * mi - loud_in_);
    loud_out_ += loud_a * (m * m - loud_out_);
    auto_gain_ += 0.0005f * (auto_target_ - auto_gain_);
    trim_ += trim_inc;
    const float post = auto_gain_ * trim_;
    ol *= post;
    orr *= post;

    // FREEZE: bypass, 10 ms crossfade.
    bypass_ += Clamp(bypass_target - bypass_, -1.0f / 480.0f, 1.0f / 480.0f);
    out[i].l = ol + bypass_ * (x[0] - ol);
    out[i].r = orr + bypass_ * (x[1] - orr);
  }
}

}  // namespace monsoon

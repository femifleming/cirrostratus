// Mode 4 - RESONATOR (see resonator.h and docs/DESIGN.md).

#include "monsoon/dsp/modes/resonator.h"
#include "monsoon/dsp/modes/knob_fx.h"

namespace monsoon {

// Tuning sets swept by SIZE: ratios to the root for the eight strings.
static const int kNumTunings = 5;
static const float kTunings[kNumTunings][ResonatorMode::kNumStrings] = {
  // Unison / octaves, slightly detuned pairs for a chorused body.
  { 1.0f, 1.0029f, 2.0f, 1.9971f, 4.0f, 1.0f, 2.0f, 4.0f },
  // Fifths.
  { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 1.5f, 3.0f },
  // Sitar sympathetic strings (taraf): Sa Re Ga ma Pa Dha Ni Sa', just.
  { 1.0f, 1.125f, 1.25f, 1.33333f, 1.5f, 1.66667f, 1.875f, 2.0f },
  // Harmonic series.
  { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f },
  // Inharmonic (square roots of primes): bells and gongs.
  { 1.0f, 1.41421f, 1.73205f, 2.23607f, 2.64575f, 3.31662f, 3.60555f, 4.12311f },
};

// FBK: two stages of pairwise rotations (01 23 45 67, then 12 34 56 70).
static __attribute__((noinline)) void Couple(float* ys, float rc, float rs) {
  const int n = ResonatorMode::kNumStrings;
  for (int stage = 0; stage < 2; ++stage) {
    for (int i = stage; i < n; i += 2) {
      const int j = (i + 1) & (n - 1);
      const float p = ys[i], q = ys[j];
      ys[i] = rc * p - rs * q;
      ys[j] = rs * p + rc * q;
    }
  }
}

// Transparent below 1, smooth saturation towards 2 above: keeps the loops
// bounded even at infinite sustain without colouring normal playing.
static inline float StringLimit(float x) {
  float a = fabsf(x);
  if (a <= 1.0f) return x;
  float y = 1.0f + SoftClip(a - 1.0f);
  return x < 0.0f ? -y : y;
}

bool ResonatorMode::Init(Arena* arena) {
  for (int i = 0; i < kNumStrings; ++i) {
    line_[i] = arena->Allocate<float>(kLineSize);
    if (!line_[i]) return false;
    string_[i].delay = 100.0f;
    string_[i].lp = 0.0f;
    string_[i].a = 0.5f;
    string_[i].last = 0.0f;
    string_[i].env = 0.0f;
    string_[i].rattle_lp = 0.0f;
    string_[i].burst = 0;
    frequency_[i] = 100.0f;
    target_delay_[i] = 100.0f;
    gain_[i] = 0.99f;
  }
  write_ = 0;
  pending_ = 0;
  lp_coef_ = 0.5f;
  pickup_ = 0.3f;
  jawari_ = 0.0f;
  input_gain_ = 0.15f;
  frozen_ = false;
  first_block_ = true;
  rng_.Seed(0x5174A);
  dc_[0].Init();
  dc_[1].Init();
  body_[0] = arena->Allocate<float>(2 * kBodySize);
  if (!body_[0]) return false;
  body_[1] = body_[0] + kBodySize;
  body_lp_[0] = body_lp_[1] = 0.0f;
  body_pos_ = 0;
  body_mix_ = 0.0f;
  body_clean_ = true;
  return true;
}

// BAL left: per-string detune (cents at full left), alternating so that
// unison pairs beat against each other.
static const float kDetune[ResonatorMode::kNumStrings] = {
  0.0f, 25.0f, -18.0f, 11.0f, -25.0f, 18.0f, -11.0f, 7.0f
};

void ResonatorMode::ComputeTargets(const Controls& c) {
  // Skip the (libm-heavy) recomputation while the controls sit still, and
  // spread it while they move: the shared values now, the strings two a
  // block (UpdateStrings), all eight within four blocks (2.7 ms).
  const float key[6] = { c.pitch, c.size, c.position, c.density,
                         frozen_ ? 1.0f : 0.0f, c.bal };
  static const float band[6] = { kBandPitch, kBandPot, kBandPot, kBandPot,
                                 kBandSwitch, kBandPot };
  if (!KnobsMoved(key, cache_, band, 6, first_block_)) return;
  float root = 65.41f * SemitonesToRatio(c.pitch);
  root = Clamp(root, 25.0f, 1500.0f);

  float s = Clamp(c.size, 0.0f, 1.0f) * (kNumTunings - 1);
  int k = static_cast<int>(s);
  if (k >= kNumTunings - 1) k = kNumTunings - 2;
  float frac = s - k;

  // Loop filter: POSITION opens the string up (and moves the pickup
  // towards the bridge, which thins the tone).
  float pos = Clamp(c.position, 0.0f, 1.0f);
  lp_coef_ = 0.12f + 0.86f * pos * pos;
  pickup_ = 0.5f - 0.42f * pos;

  // DENSITY: decay 0.2 s -> 20 s.
  const float t60 = 0.2f * powf(100.0f, Clamp(c.density, 0.0f, 1.0f));

  const float bal = KnobBipolar(c.bal);
  const float detune = bal < 0.0f ? -bal * (1.0f / 1200.0f) : 0.0f;
  const float stretch = bal > 0.0f ? 1.0f + 0.1f * bal : 1.0f;

  tune_root_ = root;
  tune_k_ = k;
  tune_frac_ = frac;
  tune_t60_ = t60;
  tune_detune_ = detune;
  tune_stretch_ = stretch;
  tune_bal_ = bal;
  pending_ = (1u << kNumStrings) - 1;
}

void ResonatorMode::UpdateStrings(int count) {
  const float b = 1.0f - lp_coef_;
  const int k = tune_k_;
  const float frac = tune_frac_, root = tune_root_, t60 = tune_t60_;
  const float detune = tune_detune_, stretch = tune_stretch_, bal = tune_bal_;
  for (int i = 0; i < kNumStrings && count > 0; ++i) {
    if (!(pending_ & (1u << i))) continue;
    pending_ &= ~(1u << i);
    --count;
    float l0 = log2f(kTunings[k][i]);
    float l1 = log2f(kTunings[k + 1][i]);
    float l = l0 + (l1 - l0) * frac;
    if (bal != 0.0f) l = l * stretch + kDetune[i] * detune;
    float f = root * exp2f(l);
    if (f > 10000.0f) f = 10000.0f;
    frequency_[i] = f;
    string_[i].a = lp_coef_;
    // Phase delay and gain of the one-pole loop filter at f.
    const float w = kTwoPi * f / kSampleRate;
    const float re = 1.0f - b * cosf(w);
    const float im = b * sinf(w);
    const float filter_delay = atan2f(im, re) / w;
    const float filter_gain = lp_coef_ / sqrtf(re * re + im * im);
    float d = kSampleRate / f - filter_delay;
    target_delay_[i] = Clamp(d, 4.0f, static_cast<float>(kLineSize - 8));
    if (frozen_) {
      // Exactly lossless at the fundamental; harmonics still mellow out.
      gain_[i] = 1.0f / filter_gain;
    } else {
      gain_[i] = expf(-6.9078f / (t60 * f)) / filter_gain;
      // Never let the fundamental gain exceed unity.
      if (gain_[i] * filter_gain > 0.99995f) gain_[i] = 0.99995f / filter_gain;
    }
  }
}

void ResonatorMode::Process(const Controls& c, const FloatFrame* in,
                            FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  jawari_ = Clamp(c.texture, 0.0f, 1.0f);
  ComputeTargets(c);
  UpdateStrings(first_block_ ? kNumStrings : 2);

  if (c.trigger) {
    for (int i = 0; i < kNumStrings; ++i) {
      string_[i].burst = static_cast<int32_t>(target_delay_[i]);
    }
  }

  float inc[kNumStrings];
  for (int i = 0; i < kNumStrings; ++i) {
    if (first_block_) string_[i].delay = target_delay_[i];
    inc[i] = (target_delay_[i] - string_[i].delay) / static_cast<float>(n);
  }
  first_block_ = false;

  const float in_gain = frozen_ ? 0.0f : input_gain_;
  const float jaw = jawari_ * jawari_;
  // FBK: sympathetic coupling. Every sample the strings' outputs pass two
  // stages of pairwise rotations (pairs 01 23 45 67, then 12 34 56 70):
  // an orthogonal mix, so energy moves from string to string but is never
  // created. Up to 0.08 rad per period: a unison pair trades its energy in
  // ~20 periods; strings far apart in pitch barely notice each other.
  const float theta = Clamp(c.fbk, 0.0f, 1.0f) * 0.08f;
  const bool couple = theta > 0.0f;
  const float rc = couple ? cosf(theta) : 1.0f;
  const float rs = couple ? sinf(theta) : 0.0f;

  for (size_t s = 0; s < n; ++s) {
    const float excitation = 0.5f * (in[s].l + in[s].r) * in_gain;
    float left = 0.0f, right = 0.0f;
    float ys[kNumStrings];
    for (int i = 0; i < kNumStrings; ++i) {
      String& st = string_[i];
      st.delay += inc[i];
      // Jawari: displacement beyond the bridge curve shortens the
      // vibrating length (one-sided contact, as on a sitar bridge).
      // The curved bridge meets the string at a fraction of its current
      // amplitude, so the buzz is there at every playing level.
      const float bridge = 0.25f * st.env + 1e-5f;
      float contact = (st.last - bridge) / (st.env + 1e-4f);
      float shrink = 0.0f;
      if (contact > 0.0f) {
        if (contact > 1.0f) contact = 1.0f;
        shrink = jaw * contact * 4.0f;
      }
      float d = st.delay - shrink;
      if (d < 3.0f) d = 3.0f;

      const float* line = line_[i];
      float pos = static_cast<float>(write_) - d;
      int32_t ip = static_cast<int32_t>(floorf(pos));
      float t = pos - static_cast<float>(ip);
      ys[i] = Hermite(line[(ip - 1) & kLineMask], line[ip & kLineMask],
                      line[(ip + 1) & kLineMask], line[(ip + 2) & kLineMask],
                      t);
    }
    if (couple) Couple(ys, rc, rs);
    for (int i = 0; i < kNumStrings; ++i) {
      String& st = string_[i];
      const float* line = line_[i];
      const float bridge = 0.25f * st.env + 1e-5f;
      float y = ys[i];
      st.env += 0.0005f * (fabsf(y) - st.env);
      // Contact with the bridge: past the contact point the string is
      // flattened against the curved bridge (one-sided).
      float hit = 0.0f;
      if (y > bridge) {
        hit = y - bridge;
        y = bridge + hit * (1.0f - 0.5f * jawari_);
      }
      // Each time the string lands on the bridge it snaps: a short click
      // proportional to its amplitude, once per period -> the buzz.
      float rattle = 0.0f;
      if (hit > 0.0f && st.rattle_lp <= 0.0f) {
        rattle = jaw * st.env * 1.5f;
      }
      st.rattle_lp = hit;
      st.lp += st.a * (y - st.lp);
      // The click re-enters the string scaled by period so that short
      // (high) strings never gain energy from it.
      float re_enter = st.delay * (1.0f / 2000.0f);
      if (re_enter > 1.0f) re_enter = 1.0f;
      float v = st.lp * gain_[i] + excitation + rattle * re_enter;
      if (st.burst > 0) {
        v += rng_.Bipolar() * 0.35f;
        --st.burst;
      }
      v = StringLimit(v);
      line_[i][write_] = v;
      st.last = y;

      // Pickup comb: difference of two points along the string.
      int32_t pk = static_cast<int32_t>(write_) -
          static_cast<int32_t>(st.delay * pickup_);
      float o = y - line[pk & kLineMask] + rattle;
      if (i & 1) {
        right += o * 0.75f;
        left += o * 0.25f;
      } else {
        left += o * 0.75f;
        right += o * 0.25f;
      }
    }
    write_ = (write_ + 1) & kLineMask;
    out[s].l = dc_[0].Process(left * 0.3f);
    out[s].r = dc_[1].Process(right * 0.3f);
  }

  Body(c.rvb, out, n);
}

// RVB: the body. Each channel: two diffusing allpasses into a damped
// feedback delay of ~8.5 ms (box resonances every ~120 Hz, tail up to
// ~0.35 s). Costs nothing while RVB is (and has settled at) zero.
void ResonatorMode::Body(float rvb, FloatFrame* out, size_t n) {
  const float target = Clamp(rvb, 0.0f, 1.0f);
  if (target <= 0.0f && body_mix_ < 1e-4f) {
    if (!body_clean_) {
      memset(body_[0], 0, 2 * kBodySize * sizeof(float));
      body_lp_[0] = body_lp_[1] = 0.0f;
      body_mix_ = 0.0f;
      body_clean_ = true;
    }
    return;
  }
  body_clean_ = false;
  body_mix_ += 0.15f * (target - body_mix_);
  const float mix = body_mix_;
  const float fb = 0.55f + 0.3f * mix;
  const float dry = 1.0f - 0.5f * mix;
  const float wet = 1.7f * mix;  // the damped box loses ~4 dB
  static const uint16_t kTaps[2][3] = { { 113, 271, 397 }, { 127, 293, 433 } };
  for (int ch = 0; ch < 2; ++ch) {
    float* ap1 = body_[ch];           // 1024
    float* ap2 = body_[ch] + 1024;    // 1024
    float* box = body_[ch] + 2048;    // 2048
    const uint16_t* tap = kTaps[ch];
    float lp = body_lp_[ch];
    uint32_t pos = body_pos_;
    float* o = &out[0].l + ch;
    for (size_t i = 0; i < n; ++i, ++pos, o += 2) {
      const float x = *o;
      float d = ap1[(pos - tap[0]) & 1023];
      float v = x + 0.6f * d;
      ap1[pos & 1023] = v;
      float y = d - 0.6f * v;
      d = ap2[(pos - tap[1]) & 1023];
      v = y + 0.55f * d;
      ap2[pos & 1023] = v;
      y = d - 0.55f * v;
      const float bx = box[(pos - tap[2]) & 2047];
      lp += 0.35f * (bx - lp);
      box[pos & 2047] = (1.0f - fb) * y + fb * lp;
      *o = x * dry + bx * wet;
    }
    body_lp_[ch] = lp;
  }
  body_pos_ += static_cast<uint32_t>(n);
}


}  // namespace monsoon

// Mode 28 - GAMELAN (see gamelan.h).

#include "monsoon/dsp/modes/gamelan.h"
#include "monsoon/dsp/modes/knob_fx.h"

#include "monsoon/dsp/modes/bal_knob.h"

namespace monsoon {

static const float kTuningCents[2][GamelanMode::kKeys] = {
  { 0.0f, 120.0f, 258.0f, 539.0f, 675.0f, 785.0f, 943.0f },     // pelog
  { 0.0f, 231.0f, 474.0f, 717.0f, 955.0f, 1200.0f, 1431.0f },   // slendro
};

static const int kInstruments = 3;
static const float kRatios[kInstruments][GamelanMode::kPartials] = {
  { 1.0f, 2.76f, 5.40f, 8.93f },   // saron: free bar
  { 1.0f, 1.48f, 2.79f, 3.64f },   // bonang: kettle
  { 1.0f, 1.44f, 2.00f, 2.99f },   // gong
};
static const float kAmps[kInstruments][GamelanMode::kPartials] = {
  { 1.0f, 0.45f, 0.22f, 0.1f },
  { 1.0f, 0.6f, 0.38f, 0.2f },
  { 1.0f, 0.5f, 0.45f, 0.22f },
};
static const float kDecayScale[kInstruments] = { 1.0f, 1.5f, 3.0f };

// Tutup: 60 dB over 90 blocks (60 ms) at full FBK: a per-block gain of
// 1 - fbk * rate on the damped key (1 - rate = 10^(-3 / 90)).
static const int kChokeBlocks = 90;
static const float kChokeRate = 0.0738f;

// Transparent below 0.9, smooth saturation above (safety only).
static inline float Safe(float x) {
  float a = fabsf(x);
  if (a <= 0.9f) return x;
  float y = 0.9f + 0.1f * SoftClip((a - 0.9f) * 10.0f);
  return x < 0.0f ? -y : y;
}

bool GamelanMode::Init(Arena* arena) {
  res_ = arena->Allocate<Resonator>(kResonators);
  if (!res_) return false;
  for (int i = 0; i < 6; ++i) cache_[i] = -1000.0f;
  mallet_ = 0.0f;
  prev_key_ = -1;
  choke_key_ = -1;
  choke_left_ = 0;
  for (int k = 0; k < kKeys; ++k) key_hz_[k] = 261.63f;
  first_ = true;
  frozen_ = false;
  dc_[0].Init();
  dc_[1].Init();
  return true;
}

void GamelanMode::Update(const Controls& c) {
  const float key[6] = { c.pitch, c.size, c.density, c.texture,
                         static_cast<float>(c.option & 1),
                         frozen_ ? 1.0f : 0.0f };
  static const float band[6] = { kBandPitch, kBandPot, kBandPot, kBandPot,
                                 kBandSwitch, kBandSwitch };
  const bool moved = KnobsMoved(key, cache_, band, 6, first_);
  first_ = false;
  if (!moved) return;

  const float t = Clamp(c.texture, 0.0f, 1.0f) * (kInstruments - 1);
  int i0 = static_cast<int>(t);
  if (i0 >= kInstruments - 1) i0 = kInstruments - 2;
  const float frac = t - i0;
  const float decay_scale = Crossfade(kDecayScale[i0], kDecayScale[i0 + 1], frac);
  // The gong end of the morph drops two octaves.
  const float gongness = Clamp(Clamp(c.texture, 0.0f, 1.0f) * 2.0f - 1.0f, 0.0f, 1.0f);
  const float root = 261.63f * SemitonesToRatio(c.pitch - 24.0f * gongness);
  const float ombak = 10.0f * Clamp(c.size, 0.0f, 1.0f);
  const float t60 = 0.3f * powf(66.67f, Clamp(c.density, 0.0f, 1.0f)) *
      decay_scale;
  const int tuning = c.option & 1;

  float ratio[kPartials], amp[kPartials];
  for (int p = 0; p < kPartials; ++p) {
    float l0 = log2f(kRatios[i0][p]), l1 = log2f(kRatios[i0 + 1][p]);
    ratio[p] = exp2f(l0 + (l1 - l0) * frac);
    amp[p] = Crossfade(kAmps[i0][p], kAmps[i0 + 1][p], frac);
  }

  for (int k = 0; k < kKeys; ++k) {
    const float f_key = Clamp(root * exp2f(kTuningCents[tuning][k] / 1200.0f),
                              20.0f, 8000.0f);
    key_hz_[k] = f_key;
    const float pan = -0.6f + 1.2f * static_cast<float>(k) / (kKeys - 1);
    for (int p = 0; p < kPartials; ++p) {
      for (int twin = 0; twin < 2; ++twin) {
        Resonator& r = res_[(k * kPartials + p) * 2 + twin];
        float f = f_key * ratio[p];
        if (twin) f *= 1.0f + ombak / f_key;   // ombak Hz on the fundamental
        r.amp = amp[p] * (twin ? 0.9f : 1.0f);
        r.wl = r.amp * (1.0f - pan) * 0.5f;
        r.wr = r.amp * (1.0f + pan) * 0.5f;
        if (f > 0.24f * kSampleRate) {
          r.cr = r.ci = 0.0f;
          r.in_gain = 0.0f;
          r.re = r.im = 0.0f;
          continue;
        }
        // Higher partials die faster.
        float t60p = t60 / (1.0f + 0.6f * static_cast<float>(p));
        float x = 6.9078f / (t60p * kSampleRate);
        float radius = frozen_ ? 1.0f : 1.0f - x + 0.5f * x * x;
        float s = FastSin(f / kSampleRate);
        float co = sqrtf(1.0f - s * s);
        r.cr = radius * co;
        r.ci = radius * s;
        r.in_gain = frozen_ ? 0.0f : (1.0f - radius) * 6.0f * r.amp;
      }
    }
  }
}

void GamelanMode::Strike(int key, float amp) {
  if (key < 0) key = 0;
  if (key >= kKeys) key = kKeys - 1;
  for (int j = 0; j < kPartials * 2; ++j) {
    Resonator& r = res_[key * kPartials * 2 + j];
    // BAL: mallet hardness tilts the upper partials (2^0 = 1 at noon).
    r.re += amp * r.amp * exp2f(mallet_ * static_cast<float>(j >> 1));
  }
}

void GamelanMode::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  Update(c);
  if (c.trigger) {
    const int key = static_cast<int>(Clamp(c.position, 0.0f, 0.999f) * kKeys);
    mallet_ = 0.8f * BalKnob(c.bal);
    Strike(key, 0.5f);
    // FBK (tutup): the other hand damps the key struck before.
    if (key == choke_key_) choke_left_ = 0;
    if (c.fbk > 0.0f && prev_key_ >= 0 && prev_key_ != key) {
      choke_key_ = prev_key_;
      choke_left_ = kChokeBlocks;
    }
    prev_key_ = static_cast<int8_t>(key);
  }
  // While choking, the damped key loses FBK x 60 dB over 60 ms.
  if (choke_left_ > 0) {
    --choke_left_;
    const float g = 1.0f - Clamp(c.fbk, 0.0f, 1.0f) * kChokeRate;
    Resonator* q = res_ + choke_key_ * kPartials * 2;
    for (int j = 0; j < kPartials * 2; ++j) {
      q[j].re *= g;
      q[j].im *= g;
    }
  }
  for (size_t s = 0; s < n; ++s) {
    const float x = 0.5f * (in[s].l + in[s].r);
    float l = 0.0f, r = 0.0f;
    Resonator* q = res_;
    for (int i = 0; i < kResonators; ++i) {
      float nre = q[i].cr * q[i].re - q[i].ci * q[i].im + x * q[i].in_gain;
      float nim = q[i].ci * q[i].re + q[i].cr * q[i].im;
      q[i].re = nre;
      q[i].im = nim;
      l += nim * q[i].wl;
      r += nim * q[i].wr;
    }
    out[s].l = Safe(dc_[0].Process(l * 0.3f));
    out[s].r = Safe(dc_[1].Process(r * 0.3f));
  }
  // Keep a frozen, repeatedly struck bank bounded.
  if (frozen_) {
    for (int i = 0; i < kResonators; ++i) {
      float e = res_[i].re * res_[i].re + res_[i].im * res_[i].im;
      if (e > 4.0f) {
        float g = 2.0f / sqrtf(e);
        res_[i].re *= g;
        res_[i].im *= g;
      }
    }
  }
}

}  // namespace monsoon

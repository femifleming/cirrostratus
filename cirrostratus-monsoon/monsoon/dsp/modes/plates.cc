// Mode 13 - PLATES (see plates.h and docs/DESIGN.md).

#include "monsoon/dsp/modes/plates.h"
#include "monsoon/dsp/modes/knob_fx.h"
#include "monsoon/dsp/modes/cold_code.h"

namespace monsoon {

static const int kMaxIndex = 7;  // m, n in 1..7 -> 49 candidate modes

// Ratio of mode (m, n) to mode (1, 1) for aspect ratio a.
static inline float PlateRatio(int m, int n, float a) {
  float r = (m * m + a * a * n * n) / (1.0f + a * a);
  // Break exact degeneracies (square plate: (m,n) == (n,m)) with the tiny
  // asymmetry every real plate has.
  return r * (1.0f + 0.0035f * static_cast<float>(m - n) /
                     static_cast<float>(m + n));
}

MONSOON_COLD bool PlatesMode::Init(Arena* arena) {
  modes_ = arena->Allocate<Resonator>(kNumModes);
  if (!modes_) return false;
  bal_used_ = 0.0f;
  selected_size_ = -1.0f;
  for (int i = 0; i < 7; ++i) cache_[i] = -1.0f;
  energy_ = 0.0f;
  bend_ = 0.0f;
  bal_ = 0.0f;
  first_block_ = true;
  frozen_ = false;
  dc_[0].Init();
  dc_[1].Init();
  SelectModes(1.0f);
  return true;
}

MONSOON_COLD void PlatesMode::SelectModes(float aspect) {
  // Keep the 24 lowest of the 49 (m, n) modes (insertion into a sorted
  // list; runs only when SIZE moves).
  float ratio[kNumModes];
  uint8_t mm[kNumModes], nn[kNumModes];
  int count = 0;
  for (int m = 1; m <= kMaxIndex; ++m) {
    for (int n = 1; n <= kMaxIndex; ++n) {
      float r = PlateRatio(m, n, aspect);
      if (count == kNumModes && r >= ratio[kNumModes - 1]) continue;
      int j = count < kNumModes ? count++ : kNumModes - 1;
      while (j > 0 && ratio[j - 1] > r) {
        ratio[j] = ratio[j - 1];
        mm[j] = mm[j - 1];
        nn[j] = nn[j - 1];
        --j;
      }
      ratio[j] = r;
      mm[j] = static_cast<uint8_t>(m);
      nn[j] = static_cast<uint8_t>(n);
    }
  }
  for (int i = 0; i < kNumModes; ++i) {
    // A different mode now sits in this slot: let the old one ring out
    // only if it is the same (m, n), otherwise keep its state (it simply
    // retunes, which sounds like the plate being bent).
    modes_[i].m = mm[i];
    modes_[i].n = nn[i];
    modes_[i].hz = ratio[i];  // temporarily the ratio
    // Pickup weights (fixed points) only depend on (m, n).
    // sin(k*pi*x) = FastSin(k*x/2) with FastSin taking cycles.
    const float m = mm[i], n = nn[i];
    modes_[i].pl = FastSin(0.5f * m * 0.23f) * FastSin(0.5f * n * 0.31f);
    modes_[i].pr = FastSin(0.5f * m * 0.67f) * FastSin(0.5f * n * 0.74f);
  }
}

void PlatesMode::UpdateCoefficients(const Controls& c) {
  const float key[7] = { c.pitch, c.size, c.position, c.density, c.texture,
                         frozen_ ? 1.0f : 0.0f, bal_ };
  // Retunes while BAL is (or was) in use keep each mode's amplitude.
  const bool keep = bal_ != 0.0f || cache_[6] != 0.0f;
  static const float band[7] = { kBandPitch, kBandPot, kBandPot, kBandPot,
                                 kBandPot, kBandSwitch, 0.0f };
  if (!KnobsMoved(key, cache_, band, 7, first_block_)) return;
  const bool size_changed = first_block_ || fabsf(c.size - selected_size_) > kBandPot;
  if (size_changed) selected_size_ = c.size;

  // SIZE: square plate (a = 1) -> long bar (a = 0.22).
  const float aspect = 1.0f - 0.78f * Clamp(c.size, 0.0f, 1.0f);
  if (size_changed) SelectModes(aspect);

  float root = Clamp(65.41f * SemitonesToRatio(c.pitch), 20.0f, 4000.0f);

  // POSITION: strike point travels across the plate.
  const float p = Clamp(c.position, 0.0f, 1.0f);
  const float sx = 0.11f + 0.76f * p;
  const float sy = 0.19f + 0.53f * p;

  // DENSITY: base decay 0.3 s -> 30 s.
  const float t60_base = 0.3f * powf(100.0f, Clamp(c.density, 0.0f, 1.0f));
  // TEXTURE: material. Metal rings long and bright, glass is in between,
  // wood loses its highs fast and is shorter overall.
  const float t = Clamp(c.texture, 0.0f, 1.0f);
  const float corner = 8000.0f * powf(600.0f / 8000.0f, t);
  const float slope = 1.0f + t;
  const float material = 1.0f - 0.6f * t * t;
  // BAL: left pulls each mode onto the nearest harmonic, right stretches
  // the series (the (1,1) root stays put in both).
  const float bal = bal_;
  const float stretch = 1.0f + 0.6f * bal;

  for (int i = 0; i < kNumModes; ++i) {
    Resonator& r = modes_[i];
    float ratio = PlateRatio(r.m, r.n, aspect);
    if (bal < 0.0f) {
      ratio -= bal * (FastFloor(ratio + 0.5f) - ratio);
    } else if (bal > 0.0f) {
      ratio = powf(ratio, stretch);
    }
    float f = root * ratio;
    r.hz = f;
    if (f > 0.45f * kSampleRate) {
      r.a1 = r.a2 = r.b = 0.0f;
      r.strike = 0.0f;
      r.y1 = r.y2 = 0.0f;
      r.sin_w = 0.0f;
      r.cos_w = r.two_r = 0.0f;
      continue;
    }
    const float w = kTwoPi * f / kSampleRate;
    const float t60 = t60_base * material / (1.0f + powf(f / corner, slope));
    const float radius = frozen_ ? 1.0f : expf(-6.9078f / (t60 * kSampleRate));
    const float sin_w = sinf(w);
    const float cos_w = cosf(w);
    if (!keep) {
      r.cos_w = cos_w;
      r.sin_b = sin_w;
    }
    r.two_r = 2.0f * radius;
    r.a1 = r.two_r * cos_w;
    r.a2 = radius * radius;
    // Constant peak gain: 1/((1-r) 2 sin w) at resonance -> normalise.
    // Frozen: radius = 1, b = 0, so the input is cut automatically.
    r.b = (1.0f - radius) * 2.0f * sin_w;
    r.sin_w = sin_w;
    r.strike = FastSin(0.5f * r.m * sx) * FastSin(0.5f * r.n * sy);
  }
  // a1 was just rebuilt without the tension bend: put it back (and, with
  // BAL in use, carry each mode's amplitude over to its new tuning).
  if (keep || bend_ != 0.0f) ApplyTension(bend_, keep);
}

// FBK: retune every mode to w * (1 + bend) (kept below 0.95 pi). With
// `rescale` the state is scaled so each mode keeps its amplitude
// (y1^2 + y2^2 - 2 cos w y1 y2 = A^2 sin^2 w): retuning a two-pole in
// place would otherwise pump energy in or out of the low modes.
MONSOON_COLD void PlatesMode::ApplyTension(float bend, bool rescale) {
  for (int i = 0; i < kNumModes; ++i) {
    Resonator& r = modes_[i];
    if (r.two_r == 0.0f) continue;
    float w = kTwoPi * r.hz / kSampleRate * (1.0f + bend);
    if (w > 0.95f * kPi) w = 0.95f * kPi;
    const float c1 = cosf(w), s1 = sinf(w);
    const float y1 = r.y1, y2 = r.y2;
    const float q0 = y1 * y1 + y2 * y2 - 2.0f * r.cos_w * y1 * y2;
    const float q1 = y1 * y1 + y2 * y2 - 2.0f * c1 * y1 * y2;
    if (rescale && q1 > 1e-20f && r.sin_b > 1e-6f) {
      const float g = Clamp(sqrtf(q0 / q1) * s1 / r.sin_b, 0.5f, 2.0f);
      r.y1 = y1 * g;
      r.y2 = y2 * g;
    }
    r.cos_w = c1;
    r.sin_b = s1;
    r.a1 = r.two_r * c1;
  }
  bend_ = bend;
}

void PlatesMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  // BAL glides (~10 ms) so CV does not jump the tuning.
  // (a target that only moves past the knob's noise, so the glide settles)
  const float bal_knob = KnobBipolar(c.bal);
  if (first_block_ || fabsf(bal_knob - bal_used_) > 2.0f * kBandPot) bal_used_ = bal_knob;
  const float bal_target = bal_used_;
  bal_ += 0.1f * (bal_target - bal_);
  if (fabsf(bal_target - bal_) < 1e-3f) bal_ = bal_target;
  UpdateCoefficients(c);
  first_block_ = false;

  // FBK: tension modulation. A loud plate goes sharp (bend follows the
  // square root of the energy left in the modes, up to +20%).
  const float tension = Clamp(c.fbk, 0.0f, 1.0f);
  float bend = 0.0f;
  if (tension > 0.0f) {
    bend = 0.2f * tension * Clamp(sqrtf(energy_) * 0.5f, 0.0f, 1.0f);
    if (fabsf(bend - bend_) < 1e-5f) bend = bend_;
  }
  if (bend != bend_) ApplyTension(bend, true);

  if (c.trigger) {
    // Strike: set every mode ringing at an amplitude given by the strike
    // point (a 2-pole started with y1 = A sin w rings at amplitude A).
    for (int i = 0; i < kNumModes; ++i) {
      Resonator& r = modes_[i];
      r.y1 += 0.5f * r.strike * r.sin_w;
    }
  }

  for (size_t s = 0; s < n; ++s) {
    const float x = 0.5f * (in[s].l + in[s].r);
    float left = 0.0f, right = 0.0f;
    for (int i = 0; i < kNumModes; ++i) {
      Resonator& r = modes_[i];
      float y = r.b * r.strike * x + r.a1 * r.y1 - r.a2 * r.y2;
      r.y2 = r.y1;
      r.y1 = y;
      left += y * r.pl;
      right += y * r.pr;
    }
    // Gentle saturation keeps a frozen, repeatedly struck plate bounded.
    out[s].l = dc_[0].Process(SoftClip(left * 0.35f));
    out[s].r = dc_[1].Process(SoftClip(right * 0.35f));
  }

  // Safety: a frozen plate struck again and again would grow without
  // bound inside the resonators; cap the energy of each mode.
  float total = 0.0f;
  for (int i = 0; i < kNumModes; ++i) {
    Resonator& r = modes_[i];
    float e = r.y1 * r.y1 + r.y2 * r.y2;
    if (e > 16.0f) {
      float g = 4.0f / sqrtf(e);
      r.y1 *= g;
      r.y2 *= g;
      e = 16.0f;
    }
    total += e;
  }
  energy_ = total;
}

}  // namespace monsoon

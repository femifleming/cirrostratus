// Helpers for the per-mode BAL / FBK / RVB code of modes 20-25.
//
// The knob features live in small out-of-line functions so that they cost
// nothing (no code in the hot loops, no extra register pressure) while the
// knobs sit at their neutral settings, and the firmware compiles them for
// size: the flash is nearly full.

#ifndef MONSOON_DSP_MODES_KNOB_FX_H_
#define MONSOON_DSP_MODES_KNOB_FX_H_

#if defined(__GNUC__) && !defined(__clang__)
#define KNOB_FX __attribute__((noinline, optimize("Os")))
#define KNOB_FX_FLAT __attribute__((noinline, flatten, optimize("Os")))
#else
#define KNOB_FX __attribute__((noinline))
#define KNOB_FX_FLAT __attribute__((noinline))
#endif

#include "monsoon/dsp/core.h"

namespace monsoon {

// KnobBipolar (BAL's mapping) as one shared, size-optimised out-of-line
// copy.
inline KNOB_FX_FLAT float KnobBal(float k) { return KnobBipolar(k); }

// The controls a mode retunes from, against the values it last retuned at:
// true (and those become the values) once any of them has moved past its
// band, or when `force`. A module's knobs are never quite still (ADC noise
// after the smoothing), and a mode that retuned on every twitch spent up to
// a third of the audio budget doing it while nobody touched anything.
inline KNOB_FX bool KnobsMoved(const float* key, float* used,
                               const float* band, int n, bool force) {
  bool moved = force;
  for (int i = 0; i < n && !moved; ++i) {
    float d = key[i] - used[i];
    if (d > band[i] || d < -band[i]) moved = true;
  }
  if (moved) for (int i = 0; i < n; ++i) used[i] = key[i];
  return moved;
}

// Bands: PITCH in semitones (5 cents: V/OCT noise reads ~3), the pots, on/off settings.
static const float kBandPitch = 0.05f;
static const float kBandPot = 0.002f;
static const float kBandSwitch = 0.5f;

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_KNOB_FX_H_

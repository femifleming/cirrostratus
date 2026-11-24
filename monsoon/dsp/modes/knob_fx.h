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

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_KNOB_FX_H_
